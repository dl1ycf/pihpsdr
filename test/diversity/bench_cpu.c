/*
 * CPU cost of the diversity analysis, measured rather than estimated.
 *
 * Drives the real engine with synthetic two-antenna data and reports the
 * added process CPU time per analysis block, for each reference mode at
 * each sample rate the radio can run at.
 *
 * The figure that matters operationally is the last column: the analysis
 * runs on its own thread, so this is the fraction of one core it needs to
 * keep up with a block period that is 85.3 ms at every sample rate.
 *
 * Samples are generated once, before the clock starts, and then replayed.
 * A first version synthesised them inside the timed loop and spent most
 * of its time in cos() - it was measuring the harness, not the engine.
 *
 * RADE V1 is reported twice because its cost differs completely between
 * its two states: searching for a pilot is the most expensive thing the
 * engine does, and tracking one is nearly free. The search case is fed
 * noise so it can never lock; the locked case is given enough blocks to
 * acquire first, and asserts that it did.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <gtk/gtk.h>

#include "mode.h"
#include "receiver.h"
#include "vfo.h"
#include "diversity_auto.h"
#include "rade_correlator.h"

static RECEIVER rx0;
RECEIVER *receiver[8] = { &rx0 };
int receivers = 2;
int diversity_enabled = 1;
int radio_is_remote = 0;
double div_cos = 1.0, div_sin = 0.0, div_gain = 0.0, div_phase = 0.0;
struct _vfo vfo[MAX_VFOS];
void t_print(const char *fmt, ...) { (void)fmt; }        /* quiet */
const char *getProperty(const char *n) { (void)n; return NULL; }
void setProperty(const char *n, const char *v) { (void)n; (void)v; }
double myatof(const char *s) { return atof(s); }

static double cpu_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static double frand(void) { return 2.0 * ((double)rand() / RAND_MAX) - 1.0; }

#define BENCH_FRAMES 40      /* modem frames held in the replay buffer */

/*
 * A continuous RADE-like stream: pilot symbol then RADE_CORR_NS data
 * symbols, repeating, at the DDC rate.
 *
 * The buffer is a whole number of modem frames long so that replaying it
 * end to end is seamless and the correlator's pilot timing stays valid
 * across the wrap. Without that the correlator loses lock once per wrap
 * and the "locked" measurement never happens.
 *
 * signal == 0 gives noise only, which keeps RADE V1 searching for ever.
 */
static long gen_stream(float **buf, int decim, int signal) {
  const long nsym = (long)BENCH_FRAMES * (RADE_CORR_NS + 1);
  const long n8   = (long)BENCH_FRAMES * RADE_CORR_NMF;
  const long nd   = n8 * decim;
  const double hr = 0.62, hi = -0.48;
  const double rs = (double)RADE_CORR_FS / RADE_CORR_M;
  const int c1 = (int)lround((1500.0 - rs * RADE_CORR_NC / 2.0) / rs);
  static const double barker13[13] = { 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1 };
  double *s8 = malloc(sizeof(double) * 2 * n8);
  *buf = malloc(sizeof(float) * 4 * nd);
  long idx = 0;

  for (long sym = 0; sym < nsym; sym++) {
    int pilot = ((sym % (RADE_CORR_NS + 1)) == 0);
    double re[RADE_CORR_M], im[RADE_CORR_M];
    memset(re, 0, sizeof(re));
    memset(im, 0, sizeof(im));

    for (int c = 0; c < RADE_CORR_NC; c++) {
      double w = 2.0 * M_PI * (c1 + c) / RADE_CORR_M;
      double a = pilot ? sqrt(2.0) * barker13[c % 13] : ((rand() & 1) ? 1.0 : -1.0);
      double b = pilot ? 0.0 : ((rand() & 1) ? 1.0 : -1.0);

      for (int n = 0; n < RADE_CORR_M; n++) {
        double th = w * n;
        re[n] += (a * cos(th) - b * sin(th)) / RADE_CORR_M;
        im[n] += (a * sin(th) + b * cos(th)) / RADE_CORR_M;
      }
    }

    /* cyclic prefix, then the symbol */
    for (int n = 0; n < RADE_CORR_NCP; n++) {
      s8[2 * idx] = re[RADE_CORR_M - RADE_CORR_NCP + n];
      s8[2 * idx + 1] = im[RADE_CORR_M - RADE_CORR_NCP + n];
      idx++;
    }

    for (int n = 0; n < RADE_CORR_M; n++) {
      s8[2 * idx] = re[n];
      s8[2 * idx + 1] = im[n];
      idx++;
    }
  }

  for (long i = 0; i < nd; i++) {
    double sr = signal ? s8[2 * (i / decim)]     : 0.0;
    double si = signal ? s8[2 * (i / decim) + 1] : 0.0;
    (*buf)[4 * i + 0] = (float)(sr + 0.002 * frand());
    (*buf)[4 * i + 1] = (float)(si + 0.002 * frand());
    (*buf)[4 * i + 2] = (float)(hr * sr - hi * si + 0.002 * frand());
    (*buf)[4 * i + 3] = (float)(hr * si + hi * sr + 0.002 * frand());
  }

  free(s8);
  return nd;
}

static void feed(const float *buf, long nd, long *pos, int nfft) {
  for (int n = 0; n < nfft; n++) {
    long i = *pos;
    diversity_auto_sample(buf[4 * i + 0], buf[4 * i + 1],
                          buf[4 * i + 2], buf[4 * i + 3]);
    *pos = (i + 1) % nd;
  }

  //
  // Real operation leaves 85.3 ms between blocks. This is faster than
  // that so the benchmark finishes quickly, but it must still leave the
  // worker enough headroom to drain the queue: feeding flat out overruns
  // it, and a dropped block makes RADE V1 re-acquire, so the locked case
  // could never be reached. CPU time is what is measured, so sleeping
  // does not affect the result.
  //
  g_usleep(15000);
}

int main(void) {
  const int rates[] = { 48000, 96000, 192000, 384000 };
  struct { const char *name; int ref; int carrier; int settle; } modes[] = {
    { "Window",           DIV_REF_BAND,      1,  4 },
    { "Carrier",          DIV_REF_CARRIER,   1,  4 },
    { "RADE passband",    DIV_REF_RADE_BAND, 1,  4 },
    { "RADE V1 (search)", DIV_REF_RADE_V1,   0,  4 },
    //
    // Declaring lock needs RADE_LOCK_FRAMES consecutive evaluations, each
    // integrating RADE_ACQ_PASSES passes of one modem frame: 3 x 32 x
    // 120 ms, so about 11.5 s of signal. Because nfft scales with the
    // sample rate, a block is 85.3 ms at every rate and this settle is
    // rate-independent.
    //
    { "RADE V1 (locked)", DIV_REF_RADE_V1,   1, 200 },
  };
  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0;
  rx0.filter_low = -8000;
  rx0.filter_high = 8000;
  memset(vfo, 0, sizeof(vfo));
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  vfo[0].mode = modeAM;
  //
  // Numbers without a machine attached to them are not much use: this is
  // scalar double-precision work, so a Pi is several times slower than a
  // desktop and the percentages move accordingly.
  //
  {
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[256];

    if (f) {
      while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "model name", 10) || !strncmp(line, "Model", 5)) {
          printf("host: %s", strchr(line, ':') ? strchr(line, ':') + 2 : line);
          break;
        }
      }

      fclose(f);
    }
  }
  printf("Added CPU per analysis block, measured. Block period is 85.3 ms at every rate.\n\n");
  printf("%-18s %6s %7s %10s %11s\n", "mode", "rate", "nfft", "ms/block", "%% of a core");

  for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
      int nfft = 4096;

      while (nfft < 65536 && (double)rates[r] / nfft > 12.0) { nfft <<= 1; }

      rx0.sample_rate = rates[r];
      div_auto_ref = modes[m].ref;
      div_auto_mode = DIV_AUTO_SUM;
      div_auto_follow_filter = 1;
      div_auto_tau = 2.0;
      div_auto_coherence_min = 0.1;
      float *buf = NULL;
      long pos = 0;
      srand(4);
      long nd = gen_stream(&buf, rates[r] / RADE_CORR_FS, modes[m].carrier);
      diversity_auto_start();

      for (int b = 0; b < modes[m].settle; b++) { feed(buf, nd, &pos, nfft); }

      const int nblk = 40;
      double t0 = cpu_seconds();

      for (int b = 0; b < nblk; b++) { feed(buf, nd, &pos, nfft); }

      g_usleep(400000);
      double cpu = cpu_seconds() - t0;
      int locked = rade_corr_locked;
      diversity_auto_stop();
      free(buf);
      /* g_usleep is not CPU time, so it does not enter the measurement */
      double ms = 1000.0 * cpu / nblk;
      const char *note = "";

      if (modes[m].ref == DIV_REF_RADE_V1) {
        note = modes[m].carrier ? (locked ? "  [locked]" : "  [DID NOT LOCK]")
               : (locked ? "  [UNEXPECTED LOCK]" : "  [searching]");
      }

      printf("%-18s %5dk %7d %9.2f %10.1f%s\n",
             r ? "" : modes[m].name, rates[r] / 1000, nfft, ms,
             100.0 * ms / 85.3, note);
      fflush(stdout);
    }
  }

  return 0;
}
