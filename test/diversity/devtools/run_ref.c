/*
 * DEVELOPMENT TOOL. Not part of piHPSDR - see README.md.
 *
 * Replays a .divc through the *whole* auto-phasing engine, with a
 * reference of your choosing, and writes out the weight it applies.
 *
 * replay_rade calls rade_corr_process() directly, which is right for
 * sweeping the correlator but reaches only one of the four references.
 * The Digital I/Q solve lives in div_digital_solve(), which is static and
 * driven from div_process_block() off the analysis thread, so the only
 * honest way to run it over a recording is to feed the samples back in
 * through diversity_auto_sample() exactly as the radio does. That is what
 * this does - the shipping code, unmodified, on recorded input.
 *
 *   ./run_ref cap.divc --ref rade    --out w_rade.csv
 *   ./run_ref cap.divc --ref digital --out w_digital.csv --noise 8.5e-5
 *
 * The output has the same columns replay_rade --weights writes, so
 * score_rade will take either.
 *
 * The pacing is the price. The worker thread has to be given room to
 * drain between blocks or the queue overruns and the drop path resets the
 * correlator - the same trap test_rade.c works around - so a 60 s capture
 * takes about ten seconds of wall clock. The weight is read one block
 * after the samples that produced it, which is the same one-block lag the
 * capture instrument records.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <gtk/gtk.h>

#include "mode.h"
#include "discovered.h"
#include "receiver.h"
#include "vfo.h"
#include "adc.h"
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
//
// The engine reads the two step attenuators as part of its analysis
// context, so a change of either restarts the statistics.
//
ADC adc[3];
int div_indep_att = 0;
//
// The engine tells the menu when a mode change swapped one block of
// modal settings for another. There is no menu here.
//
gboolean diversity_menu_settings_changed(gpointer data) { (void)data; return G_SOURCE_REMOVE; }
struct _vfo vfo[MAX_VFOS];
DISCOVERED *radio = NULL;

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

/*
 * Everything div_get_context() reads, from a recorded block. The sample
 * rate is deliberately left alone: it sizes the transform, it never
 * changes inside a capture, and writing it here would only invite a
 * mid-run reallocation that the radio never performs.
 */
static void set_context(const struct divcap_block *m) {
  rx0.filter_low  = m->filter_low;
  rx0.filter_high = m->filter_high;
  vfo[0].mode           = m->mode;
  vfo[0].frequency      = m->frequency;
  vfo[0].ctun_frequency = m->ctun_frequency;
  vfo[0].offset         = m->offset;
  cw_keyer_sidetone_frequency = m->sidetone;
}

int main(int argc, char **argv) {
  const char *path = NULL, *outp = NULL, *refname = "rade";
  double noise = 0.0, tau = 0.0, hang = 0.0;
  unsigned seed = 0;
  int usleep_us = 12000;
  int weighting = -1;
  int mode = -1;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) { verbose = 1; }
    else if (!strcmp(argv[i], "--ref")   && i + 1 < argc) { refname = argv[++i]; }
    else if (!strcmp(argv[i], "--out")   && i + 1 < argc) { outp    = argv[++i]; }
    else if (!strcmp(argv[i], "--noise") && i + 1 < argc) { noise   = atof(argv[++i]); }
    else if (!strcmp(argv[i], "--seed")  && i + 1 < argc) { seed    = (unsigned)atoi(argv[++i]); }
    else if (!strcmp(argv[i], "--tau")   && i + 1 < argc) { tau     = atof(argv[++i]); }
    else if (!strcmp(argv[i], "--hang")  && i + 1 < argc) { hang    = atof(argv[++i]); }
    else if (!strcmp(argv[i], "--pace")  && i + 1 < argc) { usleep_us = atoi(argv[++i]); }
    else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
      /* null|sum|best, overriding the objective the capture recorded */
      const char *a = argv[++i];
      mode = !strcmp(a, "null") ? DIV_AUTO_NULL
             : !strcmp(a, "sum") ? DIV_AUTO_SUM
             : !strcmp(a, "best") ? DIV_AUTO_BEST : -2;

      if (mode == -2) {
        fprintf(stderr, "%s: --mode wants null, sum or best\n", argv[0]);
        return 2;
      }
    }
    else if (!strcmp(argv[i], "--weighting") && i + 1 < argc) {
      /* flat|coherence, for the wideband window reference */
      const char *a = argv[++i];
      weighting = !strcmp(a, "coherence") ? DIV_WEIGHT_COHERENCE
                  : !strcmp(a, "flat") ? DIV_WEIGHT_FLAT : -2;

      if (weighting == -2) {
        fprintf(stderr, "%s: --weighting wants flat or coherence\n", argv[0]);
        return 2;
      }
    }
    else if (argv[i][0] == '-') {
      fprintf(stderr, "usage: %s FILE.divc --ref band|carrier|rade|digital --out W.csv\n"
              "       [--mode null|sum|best] [--weighting flat|coherence]\n"
              "       [--noise RMS] [--seed N]\n"
              "       [--tau S] [--hang S] [--pace US] [-v]\n", argv[0]);
      return 2;
    } else { path = argv[i]; }
  }

  if (path == NULL || outp == NULL) {
    fprintf(stderr, "%s: need a capture and --out\n", argv[0]);
    return 2;
  }

  int ref;

  if (!strcmp(refname, "band"))         { ref = DIV_REF_BAND; }
  else if (!strcmp(refname, "carrier")) { ref = DIV_REF_CARRIER; }
  else if (!strcmp(refname, "rade"))    { ref = DIV_REF_RADE_V1; }
  else if (!strcmp(refname, "digital")) { ref = DIV_REF_DIGITAL_IQ; }
  else { fprintf(stderr, "%s: unknown reference \"%s\"\n", argv[0], refname); return 2; }

  struct divcap_header h;
  long data_start = 0;
  FILE *f = divcap_open(path, &h, &data_start);

  if (f == NULL) { return 1; }

  const int nfft = (int)h.nfft;
  const size_t half = (size_t)nfft * 2u * sizeof(float);
  float *arm0 = malloc(half), *arm1 = malloc(half);
  struct divcap_block m;

  /*
   * The operator's settings, taken from the first block so that the run
   * reproduces the radio's context - it is what div_get_context() reads,
   * and getting it wrong moves the analysis window.
   */
  if (fread(&m, sizeof(m), 1, f) != 1 || m.rec_magic != DIVCAP_REC_MAGIC) {
    fprintf(stderr, "%s: no blocks\n", path);
    return 1;
  }

  rx0.sample_rate = m.ctx_sample_rate;
  set_context(&m);
  div_auto_ref  = ref;
  div_auto_mode = (mode >= 0) ? mode : m.auto_mode;
  div_auto_follow_filter = m.follow;
  div_auto_weighting     = (weighting >= 0) ? weighting : m.weighting;
  div_auto_centre = m.centre;
  div_auto_width  = m.width;
  div_auto_tau  = (tau  > 0.0) ? tau  : m.tau;
  div_auto_hang = (hang > 0.0) ? hang : m.hang;
  rade_tuning_defaults();
  FILE *out = fopen(outp, "w");

  if (out == NULL) { perror(outp); return 1; }

  fprintf(out, "block,t,locked,confirming,quality,snr,freq_off,ok,wr,wi,"
          "arm_valid,arm_db,arm_pick\n");
  divcap_noise_seed(seed);
  diversity_auto_start();
  fseek(f, data_start, SEEK_SET);
  long nb = 0;

  for (;;) {
    if (fread(&m, sizeof(m), 1, f) != 1 || m.rec_magic != DIVCAP_REC_MAGIC) { break; }

    if (fread(arm0, 1, half, f) != half) { break; }

    if (fread(arm1, 1, half, f) != half) { break; }

    divcap_add_noise(arm0, arm1, nfft, noise);
    /*
     * Follow the recorded context block by block, not just at the start.
     * The radio moves under the engine while a capture runs - the
     * operator tunes, changes filter - and div_context_changed() is what
     * decides whether that invalidates the estimate, so a replay that
     * pins the context to block 0 cannot exercise it at all.
     */
    set_context(&m);

    for (int i = 0; i < nfft; i++) {
      diversity_auto_sample(arm0[2 * i], arm0[2 * i + 1],
                            arm1[2 * i], arm1[2 * i + 1]);
    }

    g_usleep(usleep_us);
    /*
     * div_cos/div_sin rather than the correlator's raw answer: this is
     * what the radio applies, slew and Hold and objective included, which
     * is the thing to score when two references are being compared.
     */
    fprintf(out, "%ld,%.4f,%d,%d,%.6g,%.4f,%.4f,%d,%.9g,%.9g,%d,%.3f,%d\n",
            nb, (double)nb * nfft / h.sample_rate,
            rade_corr_locked, rade_corr_confirming, div_auto_coherence,
            rade_corr_snr, rade_corr_freq_off, !div_auto_holding,
            div_cos, div_sin,
            div_auto_arm_valid, div_auto_arm_db, div_auto_arm_pick);
    nb++;
  }

  g_usleep(200000);
  diversity_auto_stop();
  fclose(out);
  fclose(f);
  printf("%s: %ld block(s) through the %s reference -> %s\n", path, nb, refname, outp);
  printf("  final weight %+.4f %+.4f  (%.1f dB %+.0f deg), holding=%d coherence=%.3f\n",
         div_cos, div_sin, div_gain, div_phase, div_auto_holding, div_auto_coherence);
  free(arm0);
  free(arm1);
  return 0;
}
