/*
 * DEVELOPMENT TOOL. Not part of piHPSDR - see README.md.
 *
 * Round-trip check for the capture instrument.
 *
 * Generates a synthetic two-arm RADE signal, runs it through the real
 * auto-phasing engine with the capture armed, then replays the file that
 * comes out and checks that the correlator ends up in the same state,
 * block for block.
 *
 * This is what stops the three pieces drifting apart - the writer in
 * src/diversity_capture.c, the record layout in diversity_capture.h and
 * the replay in divcap_replay.c. It is also the same --verify path the
 * on-air captures go through, exercised where the answer is known.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "mode.h"
#include "discovered.h"
#include "receiver.h"
#include "vfo.h"
#include "diversity_auto.h"
#include "rade_correlator.h"
#include "rade_tuning.h"
#include "diversity_capture.h"
#include "divcap_replay.h"

static RECEIVER rx0;
RECEIVER *receiver[8] = { &rx0 };
int receivers = 2;
int diversity_enabled = 1;
int radio_is_remote = 0;
int cw_keyer_sidetone_frequency = 800;
double div_cos = 1.0, div_sin = 0.0, div_gain = 0.0, div_phase = 0.0;
struct _vfo vfo[MAX_VFOS];
DISCOVERED *radio = NULL;

/*
 * Declared here rather than in diversity_auto.h, as diversity_menu.c
 * does: nfft is private to diversity_auto.c, and the header is a
 * permanent file that this instrument must not touch.
 */
extern int diversity_auto_capture_start(void);

static int verbose = 0;
void t_print(const char *fmt, ...) {
  if (!verbose) { return; }

  va_list a;
  va_start(a, fmt);
  vprintf(fmt, a);
  va_end(a);
}
void t_perror(const char *s) { perror(s); }
const char *getProperty(const char *n) { (void)n; return NULL; }
void setProperty(const char *n, const char *v) { (void)n; (void)v; }
double myatof(const char *s) { return atof(s); }

static double frand(void) { return 2.0 * ((double)rand() / RAND_MAX) - 1.0; }

#define RATE   48000
#define NFFT   4096
#define DECIM  (RATE / RADE_CORR_FS)
#define FRAMES 30                       /* modem frames in the replay buffer */

/*
 * A RADE-like stream at the DDC rate on two arms, as test_rade.c makes
 * it: pilot symbol then four data symbols, Barker-13 pilot over the
 * carriers, cyclic prefix, upsampled, with the aux arm through a fixed
 * channel h. Whole modem frames, so replaying the buffer end to end keeps
 * the pilot timing valid across the wrap.
 */
static long gen(float **buf, double noise, double hr, double hi) {
  const long nsym = (long)FRAMES * (RADE_CORR_NS + 1);
  const long n8   = (long)FRAMES * RADE_CORR_NMF;
  const long nd   = n8 * DECIM;
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
      s8[2 * idx    ] = re[RADE_CORR_M - RADE_CORR_NCP + n];
      s8[2 * idx + 1] = im[RADE_CORR_M - RADE_CORR_NCP + n];
      idx++;
    }

    for (int n = 0; n < RADE_CORR_M; n++) {
      s8[2 * idx    ] = re[n];
      s8[2 * idx + 1] = im[n];
      idx++;
    }
  }

  for (long i = 0; i < nd; i++) {
    const double sr = s8[2 * (i / DECIM)];
    const double si = s8[2 * (i / DECIM) + 1];
    (*buf)[4 * i + 0] = (float)(sr + noise * frand());
    (*buf)[4 * i + 1] = (float)(si + noise * frand());
    (*buf)[4 * i + 2] = (float)(hr * sr - hi * si + noise * frand());
    (*buf)[4 * i + 3] = (float)(hr * si + hi * sr + noise * frand());
  }

  free(s8);
  return nd;
}

/*
 * The single .divc the run left behind.
 */
static char *find_capture(const char *dir) {
  GDir *d = g_dir_open(dir, 0, NULL);

  if (d == NULL) { return NULL; }

  const char *name;
  char *found = NULL;

  while ((name = g_dir_read_name(d)) != NULL) {
    if (g_str_has_suffix(name, ".divc")) {
      found = g_build_filename(dir, name, NULL);
      break;
    }
  }

  g_dir_close(d);
  return found;
}

int main(int argc, char **argv) {
  verbose = (argc > 1);
  int fails = 0;
  /*
   * With PIHPSDR_DIVCAP_DIR already set, the capture is written there and
   * kept - which is how to make a sample .divc to try replay_rade
   * against without a radio. Otherwise it goes to a temporary directory
   * and is cleaned up.
   */
  const char *want = g_getenv("PIHPSDR_DIVCAP_DIR");
  const int keep = (want != NULL && *want != '\0');
  char *dir = keep ? g_strdup(want) : g_dir_make_tmp("divcap-test-XXXXXX", NULL);

  if (dir == NULL) {
    fprintf(stderr, "test_capture: cannot make a temporary directory\n");
    return 1;
  }

  g_setenv("PIHPSDR_DIVCAP_DIR", dir, TRUE);
  g_setenv("PIHPSDR_DIVCAP_SECONDS", "30", TRUE);
  g_setenv("PIHPSDR_DIVCAP_NOTE", "test_capture: synthetic RADE, h=0.62-0.48j", TRUE);
  /*
   * LSB-side digital passband, no CTUN. Same geometry test_rade.c uses
   * for its plain LSB acquisition case.
   */
  rx0.sample_rate  = RATE;
  rx0.filter_low   = -2800;
  rx0.filter_high  = -200;
  vfo[0].mode      = modeDIGL;
  vfo[0].frequency = 7100000;
  vfo[0].offset    = 0;
  vfo[0].ctun_frequency = vfo[0].frequency;
  div_auto_ref  = DIV_REF_RADE_V1;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_tau  = 2.0;
  div_auto_hang = 10.0;
  div_auto_coherence_min = 0.1;
  rade_tuning_defaults();
  srand(7);
  float *buf = NULL;
  const long nd = gen(&buf, 0.01, 0.62, -0.48);
  long pos = 0;
  diversity_auto_start();

  if (!diversity_auto_capture_start()) {
    fprintf(stderr, "test_capture: capture would not arm\n");
    return 1;
  }

  /*
   * Long enough to acquire (1 to 5 s on a clean signal) and then track
   * for a while, so the file carries a lock transition and a run of
   * tracking frames rather than just a search.
   */
  const int nblocks = 160;

  for (int b = 0; b < nblocks; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                            buf[4 * pos + 2], buf[4 * pos + 3]);
      pos = (pos + 1) % nd;
    }

    /*
     * Room for the worker to drain. Feeding flat out overruns the queue,
     * and a dropped block would make this a test of the drop path.
     */
    g_usleep(12000);
  }

  g_usleep(200000);
  const int locked_live = rade_corr_locked;
  diversity_auto_stop();          /* closes the file */
  free(buf);
  printf("capture: %d blocks fed, live lock=%d\n", nblocks, locked_live);

  if (!locked_live) {
    printf("  FAIL: the synthetic signal did not lock, so there is "
           "nothing worth round-tripping\n");
    fails++;
  }

  char *path = find_capture(dir);

  if (path == NULL) {
    printf("  FAIL: no .divc written to %s\n", dir);
    return 1;
  }

  /*
   * Size check. The file is a header, then one fixed record plus two
   * arms of nfft float pairs per block, then a trailer - so its length
   * says how many blocks it holds, and disagreement means the layout the
   * writer used is not the one the reader expects.
   */
  struct divcap_header h;
  long data_start = 0;
  FILE *f = divcap_open(path, &h, &data_start);

  if (f == NULL) { return 1; }

  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  const long per = (long)sizeof(struct divcap_block) + (long)h.block_bytes;
  const long body = len - data_start - (long)sizeof(struct divcap_trailer);
  printf("file:    %s\n", path);
  printf("         rate=%u nfft=%u payload=%u note=\"%s\"\n",
         h.sample_rate, h.nfft, h.block_bytes, h.note);
  printf("         %ld bytes = header + %ld x %ld + trailer",
         len, body / per, per);

  if (body % per != 0) {
    printf("  FAIL: %ld bytes left over\n", body % per);
    fails++;
  } else {
    printf("  ok\n");
  }

  if ((int)h.sample_rate != RATE || (int)h.nfft != NFFT) {
    printf("  FAIL: header says %u/%u, fed %d/%d\n",
           h.sample_rate, h.nfft, RATE, NFFT);
    fails++;
  }

  /*
   * The round trip. rade_corr_* is left where the live run put it, so
   * the replay has to start from a stopped correlator - divcap_replay()
   * calls rade_corr_start() itself.
   */
  struct divcap_result r;
  struct divcap_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.verify = 1;

  if (!divcap_replay(f, &h, data_start, &opts, &r)) {
    printf("  FAIL: replay would not run\n");
    return 1;
  }

  fclose(f);
  printf("replay:  %d blocks, %.1f s, %d acquisition(s), locked %.0f%%, "
         "first lock %.2f s\n",
         r.blocks, r.seconds, r.acquisitions,
         100.0 * (r.blocks ? (double)r.locked_blocks / r.blocks : 0.0),
         r.first_lock);

  if (r.blocks < nblocks - 2) {
    printf("  FAIL: %d blocks in the file, %d were fed\n", r.blocks, nblocks);
    fails++;
  }

  if (r.acquisitions < 1) {
    printf("  FAIL: the replay never locked\n");
    fails++;
  }

  printf("verify:  %d block(s) checked, %d differ", r.verify_checked, r.verify_bad);

  if (r.verify_bad != 0) {
    printf("  FAIL\n");
    fails++;
  } else {
    printf("  ok\n");
  }

  if (keep) {
    printf("kept:    %s\n", path);
  } else {
    g_unlink(path);
    g_rmdir(dir);
  }

  g_free(path);
  g_free(dir);
  printf("%s\n", fails ? "FAILED" : "PASS");
  return fails ? 1 : 0;
}
