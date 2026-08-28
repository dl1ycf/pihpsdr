/*
 * RADE V1 correlator: acquisition time, sideband, and the raw-to-shifted
 * frame conversion.
 *
 * Everything here runs through diversity_auto.c rather than calling the
 * correlator directly, because two of the three things being checked are
 * decided there: which pilot bank the operator's sideband implies, and
 * the offset between the raw DDC frame and the frame the passband is
 * expressed in.
 *
 * 1. Acquisition time. It used to be fixed at 11.5 s for every signal,
 *    however strong, because a lock needed three consecutive full 32-pass
 *    searches. It is now a progressive search followed by a cheap
 *    confirmation, so a strong signal should be well under half of that.
 *
 * 2. Sideband. The operator's passband names the pilot bank and is the
 *    only one searched: an LSB passband looks below the tuned frequency
 *    and a USB one above it, and a modem on the other side is not found
 *    at all - a lock outside the passband is of no use to this mode. The
 *    green analysis window on the panadapter is drawn from the same
 *    answer, which is how the operator noticed it was wrong.
 *
 * 3. CTUN. With a non-zero vfo offset the tuned signal is not at zero in
 *    the raw stream, and the correlator has to shift by the right amount
 *    in the right direction to find it. The sign was wrong here, and
 *    nothing acquired under CTUN at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
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
int cw_keyer_sidetone_frequency = 800;
double div_cos = 1.0, div_sin = 0.0, div_gain = 0.0, div_phase = 0.0;
struct _vfo vfo[MAX_VFOS];
static int verbose = 0;
void t_print(const char *fmt, ...) {
  if (!verbose) { return; }

  va_list a;
  va_start(a, fmt);
  vprintf(fmt, a);
  va_end(a);
}
const char *getProperty(const char *n) { (void)n; return NULL; }
void setProperty(const char *n, const char *v) { (void)n; (void)v; }
double myatof(const char *s) { return atof(s); }

static double frand(void) { return 2.0 * ((double)rand() / RAND_MAX) - 1.0; }

#define RATE   48000
#define NFFT   4096
#define DECIM  (RATE / RADE_CORR_FS)
#define FRAMES 30                       /* modem frames in the replay buffer */

/*
 * A continuous RADE-like stream as it arrives in the tapped buffer: pilot
 * symbol then RADE_CORR_NS data symbols, repeating, at the DDC rate.
 *
 * side is where the modem is in *RF* terms - -1 below the dial, as an LSB
 * passband gives, +1 above it. The buffer is inverted with respect to RF
 * (see the frequency bookkeeping note in diversity_auto.c), so below the
 * dial is the un-mirrored, positive-frequency case here and above it is
 * the mirrored one. Getting this backwards is exactly the mistake the
 * on-air logs corrected, so it is spelled out rather than assumed.
 *
 * off_hz is vfo[0].offset: CTUN puts the tuned point at RF dial + off_hz,
 * which is -off_hz in the buffer.
 *
 * The buffer is a whole number of modem frames long so replaying it end
 * to end keeps the pilot timing valid across the wrap.
 */
static long gen(float **buf, int side, double off_hz, double noise) {
  const long nsym = (long)FRAMES * (RADE_CORR_NS + 1);
  const long n8   = (long)FRAMES * RADE_CORR_NMF;
  const long nd   = n8 * DECIM;
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
    double sr = s8[2 * (i / DECIM)];
    double si = s8[2 * (i / DECIM) + 1];

    //
    // Above the dial is the mirror image in this buffer: conj() reflects
    // every carrier about zero.
    //
    if (side > 0) { si = -si; }

    //
    // ... and the tuned point sits at -off_hz.
    //
    double th = -2.0 * M_PI * off_hz * (double)i / (double)RATE;
    double c = cos(th), s = sin(th);
    double tr = sr * c - si * s;
    double ti = sr * s + si * c;
    (*buf)[4 * i + 0] = (float)(tr + noise * frand());
    (*buf)[4 * i + 1] = (float)(ti + noise * frand());
    (*buf)[4 * i + 2] = (float)(hr * tr - hi * ti + noise * frand());
    (*buf)[4 * i + 3] = (float)(hr * ti + hi * tr + noise * frand());
  }

  free(s8);
  return nd;
}

/*
 * Run until locked or out of patience. Returns the number of blocks fed,
 * or -1 if it never locked. One block is NFFT samples, 85.3 ms.
 */
static int run(int side, int pb, double off_hz, double noise, int maxblocks, int seed) {
  rx0.sample_rate = RATE;

  if (pb < 0) {
    rx0.filter_low = -2800;
    rx0.filter_high = -200;
    vfo[0].mode = modeDIGL;
  } else {
    rx0.filter_low = 200;
    rx0.filter_high = 2800;
    vfo[0].mode = modeDIGU;
  }

  vfo[0].frequency = 7100000;
  vfo[0].offset = (long long)llround(off_hz);
  vfo[0].ctun_frequency = vfo[0].frequency + vfo[0].offset;
  div_auto_ref = DIV_REF_RADE_V1;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_tau = 2.0;
  div_auto_coherence_min = 0.1;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  srand(seed);
  float *buf = NULL;
  long pos = 0;
  long nd = gen(&buf, side, off_hz, noise);
  diversity_auto_start();
  int blocks = -1;

  for (int b = 0; b < maxblocks; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                            buf[4 * pos + 2], buf[4 * pos + 3]);
      pos = (pos + 1) % nd;
    }

    //
    // Leave the worker room to drain: feeding flat out overruns the queue
    // and a dropped block makes the correlator start again.
    //
    g_usleep(12000);

    if (rade_corr_locked) {
      blocks = b + 1;
      break;
    }
  }

  g_usleep(200000);
  diversity_auto_stop();
  free(buf);
  return blocks;
}

static double secs(int blocks) {
  return (double)blocks * (double)NFFT / (double)RATE;
}

int main(int argc, char **argv) {
  if (argc > 1) { verbose = 1; }

  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0;
  memset(vfo, 0, sizeof(vfo));
  int ok = 1;
  printf("RADE V1 correlator checks\n\n");
  //
  // The old fixed cost was 3 x 32 x 120 ms = 11.5 s before a lock could
  // be declared on any signal at all. Anything at or above that here has
  // regressed to the old scheme.
  //
  const double was = 11.5;
  //
  // sig is where the modem actually is; pb is the sideband the operator
  // has selected. The last two have them disagreeing, which must not
  // produce a lock: that modem is outside the passband.
  //
  struct {
    const char *name; int sig; int pb; double off; int want; int max;
  } cases[] = {
    { "USB passband",         +1, +1,     0.0, 1, 120 },
    { "LSB passband",         -1, -1,     0.0, 1, 120 },
    { "LSB, CTUN +5 kHz",     -1, -1,  5000.0, 1, 120 },
    { "USB, CTUN -3 kHz",     +1, +1, -3000.0, 1, 120 },
    { "modem above LSB pb",   +1, -1,     0.0, 0, 120 },
    { "modem below USB pb",   -1, +1,     0.0, 0, 120 },
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    int b = run(cases[i].sig, cases[i].pb, cases[i].off, 0.01, cases[i].max, 7 + i);

    if (!cases[i].want) {
      printf("  %-20s  %s\n", cases[i].name,
             (b < 0) ? "not found, as it should not be   OK"
             : "LOCKED OUTSIDE THE PASSBAND   FAIL");

      if (b >= 0) { ok = 0; }

      continue;
    }

    if (b < 0) {
      printf("  %-20s  DID NOT LOCK in %0.1f s   FAIL\n",
             cases[i].name, secs(cases[i].max));
      ok = 0;
      continue;
    }

    int want_mirror = (cases[i].pb > 0);
    int side_ok = (rade_corr_mirrored == want_mirror);
    printf("  %-20s  locked in %5.2f s (%2d blocks)  modem %s carrier  %s\n",
           cases[i].name, secs(b), b,
           rade_corr_mirrored ? "above" : "below",
           side_ok ? (secs(b) < was ? "OK" : "SLOW") : "WRONG SIDEBAND");

    if (!side_ok || secs(b) >= was) { ok = 0; }
  }

  //
  // Noise only. A false lock steers the null onto the wanted signal, so
  // this matters more than any of the above.
  //
  printf("\n");
  {
    rx0.sample_rate = RATE;
    rx0.filter_low = -2800;
    rx0.filter_high = -200;
    vfo[0].mode = modeDIGL;
    vfo[0].frequency = 7100000;
    vfo[0].ctun_frequency = 7100000;
    vfo[0].offset = 0;
    div_auto_ref = DIV_REF_RADE_V1;
    div_auto_mode = DIV_AUTO_SUM;
    div_auto_tau = 2.0;
    div_cos = 1.0;
    div_sin = 0.0;
    srand(99);
    diversity_auto_start();

    for (int b = 0; b < 150; b++) {
      for (int n = 0; n < NFFT; n++) {
        diversity_auto_sample(frand(), frand(), frand(), frand());
      }

      g_usleep(12000);
    }

    g_usleep(200000);
    int locked = rade_corr_locked;
    double g = div_gain, p = div_phase;
    diversity_auto_stop();
    printf("  noise only, %0.1f s        %s   weight %+0.2f dB %+0.1f deg\n",
           secs(150), locked ? "LOCKED - FAIL" : "no lock  OK", g, p);

    if (locked) { ok = 0; }

    if (fabs(g) > 0.5 || fabs(p) > 5.0) {
      printf("  FAIL: the weight moved with no signal present\n");
      ok = 0;
    }
  }
  //
  // The wideband RADE window, and with it the green overlay on the
  // panadapter, has to follow the operator's sideband. It used to be
  // placed by comparing the energy on the two sides, which on a signal
  // near the noise floor - or on no signal at all - is a coin toss, and
  // it could sit above an LSB passband indefinitely.
  //
  printf("\n");
  {
    struct { const char *name; int side; int sig; } wb[] = {
      { "LSB passband, signal",  -1, 1 },
      { "LSB passband, noise",   -1, 0 },
      { "USB passband, signal",  +1, 1 },
      { "USB passband, noise",   +1, 0 },
    };

    for (unsigned i = 0; i < sizeof(wb) / sizeof(wb[0]); i++) {
      rx0.sample_rate = RATE;

      if (wb[i].side < 0) {
        rx0.filter_low = -2800;
        rx0.filter_high = -200;
        vfo[0].mode = modeDIGL;
      } else {
        rx0.filter_low = 200;
        rx0.filter_high = 2800;
        vfo[0].mode = modeDIGU;
      }

      vfo[0].frequency = 7100000;
      vfo[0].ctun_frequency = 7100000;
      vfo[0].offset = 0;
      div_auto_ref = DIV_REF_RADE_BAND;
      div_auto_mode = DIV_AUTO_SUM;
      div_auto_tau = 1.0;
      div_auto_coherence_min = 0.1;
      div_cos = 1.0;
      div_sin = 0.0;
      srand(31 + i);
      float *buf = NULL;
      long pos = 0;
      long nd = gen(&buf, wb[i].side, 0.0, 0.05);
      diversity_auto_start();

      for (int b = 0; b < 40; b++) {
        for (int n = 0; n < NFFT; n++) {
          if (wb[i].sig) {
            diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                                  buf[4 * pos + 2], buf[4 * pos + 3]);
          } else {
            diversity_auto_sample(frand(), frand(), frand(), frand());
          }

          pos = (pos + 1) % nd;
        }

        g_usleep(12000);
      }

      g_usleep(200000);
      int got = div_rade_side_get();
      diversity_auto_stop();
      free(buf);
      printf("  window %-22s -> %s   %s\n", wb[i].name,
             (got < 0) ? "below" : "above",
             (got == wb[i].side) ? "OK" : "WRONG SIDE");

      if (got != wb[i].side) { ok = 0; }
    }
  }
  printf("\n%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
