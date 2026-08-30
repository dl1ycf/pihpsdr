/*
 * DEVELOPMENT TOOL. Not part of piHPSDR - see README.md.
 *
 * Scores a .divc capture on what actually matters: whether RADE decodes.
 *
 * replay_rade measures the detector - how fast it locks, how long it
 * holds, how much the weight moves. Those are proxies. The question the
 * diversity combiner exists to answer is whether the modem recovers more
 * frames with the two antennas combined than with the better one alone,
 * and only a decoder can say that.
 *
 * So this runs three librade receivers side by side over one capture:
 * arm 0 alone, arm 1 alone, and the two combined with the weight the
 * correlator produces as it goes. The number to look at is the last one
 * minus the best of the first two.
 *
 *   make score RADE_DIR=$HOME/sdr/AetherSDR/third_party/radae
 *   ./score_rade cap.divc
 *   ./score_rade cap.divc --set use_ratio=2.0
 *   ./score_rade cap.divc --noise 2e-5 --weights oracle=w.csv
 *
 * --weights adds a fourth, fifth ... stream driven by a weight sequence
 * from a file rather than by the correlator, which is how a proposed
 * estimator is scored against the shipping one over the same capture and
 * the same decoder. The file is replay_rade's --weights output, or
 * anything with the same wr/wi columns.
 *
 * --noise walks the capture down towards the decoder's threshold by
 * adding independent noise to each arm. Independent, because that is the
 * part a two-branch array cannot null: whatever both antennas hear in
 * common is already in the recording, and adding more of it would make
 * the array look better than it is.
 *
 * Sweeping means a shell loop around it - each point runs three decoders
 * over the whole capture, so the decoder dominates and there is nothing
 * to be gained by nesting the sweep inside.
 *
 * The correlator is #included rather than linked because the combining
 * has to happen on the 8 kHz stream its own NCO and polyphase decimator
 * produce, and those, with the ring they write into, are static. Reusing
 * them is the point: the three streams then differ only in the weight
 * applied to them, which is what is being measured. Combining after the
 * decimator rather than before is exact, not an approximation - the
 * weight is one complex scalar over the block and the decimator is
 * linear.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <gtk/gtk.h>

#include "mode.h"
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

/*
 * librade first, then the correlator.
 *
 * radae's own rade_dsp.h defines RADE_ACQ_FRANGE, RADE_ACQ_FSTEP and
 * RADE_ACQ_NFREQ for its acquisition, with different values from ours -
 * 2.5 Hz over 40 steps against our 5 Hz over 21. Whichever is included
 * second silently wins, and if that were librade the correlator here
 * would be searching a different grid from the one it searches on air,
 * which is exactly the sort of difference that makes a scoring run a lie.
 *
 * So: include librade, then take those three names back.
 */
#include "rade_api.h"

#undef RADE_ACQ_FRANGE
#undef RADE_ACQ_FSTEP
#undef RADE_ACQ_NFREQ

/*
 * The correlator itself, with the sweepable constants lifted out.
 * #included rather than linked - see the note at the top.
 */
#include "rade_correlator_tunable.c"

/* ------------------------------------------------------------------ */

#define MAX_STREAM 8

/*
 * A weight sequence read from a file, one row per analysis block. Rows
 * where the producer had no answer (ok = 0) hold the previous weight,
 * which is what the radio does.
 */
struct wseq {
  double *wr, *wi;
  int     n;
};

static int wseq_load(struct wseq *w, const char *path) {
  FILE *f = fopen(path, "r");

  if (f == NULL) { perror(path); return 0; }

  char line[1024];
  int cwr = -1, cwi = -1, cok = -1, cap = 1024;
  w->n = 0;
  w->wr = malloc(sizeof(double) * cap);
  w->wi = malloc(sizeof(double) * cap);

  if (fgets(line, sizeof(line), f) == NULL) { fclose(f); return 0; }

  {
    /* Locate the columns by name, so the row layout can change. */
    int col = 0;
    char *save = NULL;

    for (char *t = strtok_r(line, ",\r\n", &save); t != NULL;
         t = strtok_r(NULL, ",\r\n", &save), col++) {
      if (!strcmp(t, "wr")) { cwr = col; }
      else if (!strcmp(t, "wi")) { cwi = col; }
      else if (!strcmp(t, "ok")) { cok = col; }
    }
  }

  if (cwr < 0 || cwi < 0) {
    fprintf(stderr, "%s: no wr/wi columns in the header row\n", path);
    fclose(f);
    return 0;
  }

  double lr = 0.0, li = 0.0;

  while (fgets(line, sizeof(line), f) != NULL) {
    double vr = lr, vi = li;
    int ok = 1, col = 0;
    char *save = NULL;

    for (char *t = strtok_r(line, ",\r\n", &save); t != NULL;
         t = strtok_r(NULL, ",\r\n", &save), col++) {
      if (col == cwr) { vr = atof(t); }
      else if (col == cwi) { vi = atof(t); }
      else if (col == cok) { ok = atoi(t); }
    }

    if (!ok) { vr = lr; vi = li; }

    if (w->n >= cap) {
      cap *= 2;
      w->wr = realloc(w->wr, sizeof(double) * cap);
      w->wi = realloc(w->wi, sizeof(double) * cap);
    }

    w->wr[w->n] = vr;
    w->wi[w->n] = vi;
    w->n++;
    lr = vr;
    li = vi;
  }

  fclose(f);
  return 1;
}

struct stream {
  const char *name;
  struct rade *r;
  RADE_COMP  *buf;          /* pending 8 kHz complex samples            */
  int         n;            /* how many are pending                     */
  int         cap;
  float      *features;
  float      *eoo;
  long        frames;       /* rade_rx() calls that produced features   */
  long        synced;       /* of those, in sync                        */
  double      snr_sum;
  long        snr_n;
  /*
   * How this stream is combined. -1 arm 0 alone, -2 arm 1 alone,
   * -3 the correlator's own answer, >= 0 an index into wseq[].
   */
  int         src;
};

static int stream_open(struct stream *s, const char *name, char *model, int flags,
                       int src) {
  memset(s, 0, sizeof(*s));
  s->name = name;
  s->src = src;
  s->r = rade_open(model, flags);

  if (s->r == NULL) {
    fprintf(stderr, "score: rade_open(\"%s\") failed for %s\n", model, name);
    return 0;
  }

  s->cap = rade_nin_max(s->r) * 4;
  s->buf = malloc(sizeof(RADE_COMP) * s->cap);
  s->features = malloc(sizeof(float) * rade_n_features_in_out(s->r));
  s->eoo = malloc(sizeof(float) * (rade_n_eoo_bits(s->r) + 1));
  return (s->buf != NULL && s->features != NULL && s->eoo != NULL);
}

static void stream_push(struct stream *s, float re, float im) {
  if (s->n >= s->cap) { return; }        /* cannot happen: drained below */

  s->buf[s->n].real = re;
  s->buf[s->n].imag = im;
  s->n++;
}

/*
 * Hand whole rade_nin() blocks to the decoder and keep the remainder.
 */
static void stream_drain(struct stream *s) {
  for (;;) {
    const int nin = rade_nin(s->r);

    if (nin <= 0 || s->n < nin) { break; }

    int has_eoo = 0;
    const int got = rade_rx(s->r, s->features, &has_eoo, s->eoo, s->buf);

    if (got > 0) {
      s->frames++;

      if (rade_sync(s->r)) {
        s->synced++;
        s->snr_sum += rade_snrdB_3k_est(s->r);
        s->snr_n++;
      }
    }

    memmove(s->buf, s->buf + nin, sizeof(RADE_COMP) * (size_t)(s->n - nin));
    s->n -= nin;
  }
}

static void stream_close(struct stream *s) {
  if (s->r != NULL) { rade_close(s->r); }

  free(s->buf);
  free(s->features);
  free(s->eoo);
}

/* mirrors DIV_RETUNE_HZ in diversity_auto.c */
#define SCORE_RETUNE_HZ 20

static int ctx_changed(const struct divcap_block *a, const struct divcap_block *b) {
  return llabs(a->frequency      - b->frequency)      > SCORE_RETUNE_HZ ||
         llabs(a->ctun_frequency - b->ctun_frequency) > SCORE_RETUNE_HZ ||
         llabs(a->offset         - b->offset)         > SCORE_RETUNE_HZ ||
         a->sidetone       != b->sidetone       ||
         a->ctx_sample_rate != b->ctx_sample_rate ||
         a->mode           != b->mode           ||
         a->filter_low     != b->filter_low     ||
         a->filter_high    != b->filter_high    ||
         a->ref            != b->ref            ||
         a->follow         != b->follow         ||
         a->centre         != b->centre         ||
         a->width          != b->width          ||
         a->weighting      != b->weighting;
}

int main(int argc, char **argv) {
  const char *path = NULL;
  char model[256] = "dummy";
  double noise = 0.0;
  unsigned seed = 0;
  struct wseq wseq[MAX_STREAM];
  const char *wname[MAX_STREAM];
  int nw = 0;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) {
      verbose = 1;
    } else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
      snprintf(model, sizeof(model), "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--noise") && i + 1 < argc) {
      noise = atof(argv[++i]);
    } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
      seed = (unsigned)atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--weights") && i + 1 < argc) {
      char *a = argv[++i];
      char *eq = strchr(a, '=');

      if (eq == NULL || nw >= MAX_STREAM - 3) {
        fprintf(stderr, "score: --weights wants NAME=FILE\n");
        return 2;
      }

      *eq = '\0';
      wname[nw] = a;

      if (!wseq_load(&wseq[nw], eq + 1)) { return 1; }

      nw++;
    } else if (!strcmp(argv[i], "--set") && i + 1 < argc) {
      char *s = argv[++i];
      char *eq = strchr(s, '=');

      if (eq == NULL) { fprintf(stderr, "score: --set wants name=value\n"); return 2; }

      *eq = '\0';

      if (!rade_tuning_set(s, atof(eq + 1))) {
        fprintf(stderr, "score: unknown setting \"%s\"\n", s);
        return 2;
      }
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "usage: %s FILE.divc [--set name=v]... [--model NAME] [-v]\n", argv[0]);
      return 2;
    } else {
      path = argv[i];
    }
  }

  if (path == NULL) {
    fprintf(stderr, "usage: %s FILE.divc [--set name=v]... [--model NAME] [-v]\n", argv[0]);
    return 2;
  }

  struct divcap_header h;
  long data_start = 0;
  FILE *f = divcap_open(path, &h, &data_start);

  if (f == NULL) { return 1; }

  printf("# %s\n# note %s\n# rate %u Hz, nfft %u\n",
         path, h.note, h.sample_rate, h.nfft);
  rade_initialize();
  const int flags = RADE_USE_C_ENCODER | RADE_USE_C_DECODER |
                    (verbose ? 0 : RADE_VERBOSE_0);
  struct stream st[MAX_STREAM];
  const int nst = 3 + nw;

  if (!stream_open(&st[0], "arm0",     model, flags, -1) ||
      !stream_open(&st[1], "arm1",     model, flags, -2) ||
      !stream_open(&st[2], "correlator", model, flags, -3)) {
    return 1;
  }

  for (int i = 0; i < nw; i++) {
    if (!stream_open(&st[3 + i], wname[i], model, flags, i)) { return 1; }
  }

  if (!rade_corr_start((int)h.sample_rate)) {
    fprintf(stderr, "score: correlator will not run at %u Hz\n", h.sample_rate);
    return 1;
  }

  rade_corr_freq_off = 0.0;
  rade_corr_mirrored = 0;
  const int nfft = (int)h.nfft;
  const size_t half = (size_t)nfft * 2u * sizeof(float);
  float *arm0 = malloc(half), *arm1 = malloc(half);
  struct divcap_block m, prev;
  int have_prev = 0;
  /*
   * The weight in force. Starts at zero - arm 0 alone - which is what the
   * combiner is doing before it has an answer, and is held through the
   * blocks where the correlator produces nothing, which is what the radio
   * does too.
   */
  double wr = 0.0, wi = 0.0;
  long blocks = 0;
  divcap_noise_seed(seed);
  fseek(f, data_start, SEEK_SET);

  for (;;) {
    if (fread(&m, sizeof(m), 1, f) != 1 || m.rec_magic != DIVCAP_REC_MAGIC) { break; }

    if (fread(arm0, 1, half, f) != half) { break; }

    if (fread(arm1, 1, half, f) != half) { break; }

    divcap_add_noise(arm0, arm1, nfft, noise);

    if (have_prev && ctx_changed(&m, &prev)) {
      rade_corr_reset();
      prev = m;
    }

    if (m.dropped > 0) { rade_corr_reset(); }

    const int64_t before = ringtotal;
    double nwr, nwi;

    if (rade_corr_process(arm0, arm1, nfft, m.expect_bank,
                          m.frame_off, m.tau, m.hang, &nwr, &nwi)) {
      wr = nwr;
      wi = nwi;
    }

    /*
     * The modem arrives mirrored when it sits above the dial. RADE wants
     * it the right way up, so conjugate that case - the same fact the
     * correlator expresses by choosing a pilot bank.
     */
    const int mirror = (m.expect_bank >= 0) ? (m.expect_bank == 1) : rade_corr_mirrored;

    const double sgn = mirror ? -1.0 : 1.0;

    for (int64_t a = before; a < ringtotal; a++) {
      const cplx z0 = ring_get(ring0, a);
      const cplx z1 = ring_get(ring1, a);

      for (int i = 0; i < nst; i++) {
        double ar, ai;

        if (st[i].src == -1) {
          ar = z0.re;
          ai = z0.im;
        } else if (st[i].src == -2) {
          ar = z1.re;
          ai = z1.im;
        } else {
          double ur = wr, ui = wi;

          if (st[i].src >= 0) {
            const struct wseq *q = &wseq[st[i].src];
            const int k = (blocks < q->n) ? (int)blocks : (q->n - 1);
            ur = (k >= 0) ? q->wr[k] : 0.0;
            ui = (k >= 0) ? q->wi[k] : 0.0;
          }

          ar = z0.re + (ur * z1.re - ui * z1.im);
          ai = z0.im + (ur * z1.im + ui * z1.re);
        }

        stream_push(&st[i], (float)ar, (float)(sgn * ai));
      }
    }

    for (int i = 0; i < nst; i++) { stream_drain(&st[i]); }

    if (!have_prev) { prev = m; }

    have_prev = 1;
    blocks++;
  }

  rade_corr_stop();
  fclose(f);
  free(arm0);
  free(arm1);
  printf("\n%-12s %10s %10s %8s %9s\n",
         "stream", "rx frames", "in sync", "sync %", "mean SNR");

  for (int i = 0; i < nst; i++) {
    printf("%-12s %10ld %10ld %7.1f%% %8.1f\n", st[i].name,
           st[i].frames, st[i].synced,
           st[i].frames ? 100.0 * (double)st[i].synced / st[i].frames : 0.0,
           st[i].snr_n ? st[i].snr_sum / st[i].snr_n : 0.0);
  }

  {
    const long best_arm = (st[0].synced > st[1].synced) ? st[0].synced : st[1].synced;
    const double best_snr = (st[0].snr_n && st[1].snr_n)
                            ? ((st[0].snr_sum / st[0].snr_n > st[1].snr_sum / st[1].snr_n)
                               ? st[0].snr_sum / st[0].snr_n : st[1].snr_sum / st[1].snr_n)
                            : 0.0;
    printf("\nagainst the better arm, over %ld block(s):\n", blocks);

    for (int i = 2; i < nst; i++) {
      printf("  %-12s %+ld synced frame(s)  %+.1f dB\n", st[i].name,
             st[i].synced - best_arm,
             (st[i].snr_n ? st[i].snr_sum / st[i].snr_n : 0.0) - best_snr);
    }
  }

  for (int i = 0; i < nst; i++) { stream_close(&st[i]); }

  rade_finalize();
  return 0;
}
