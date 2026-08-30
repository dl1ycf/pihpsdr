/*
 * DEVELOPMENT TOOL. Not part of piHPSDR - see README.md.
 *
 * The replay itself: read a .divc block by block and hand each one to
 * rade_corr_process() with the bank, frame offset, averaging time and
 * hang the live run used.
 *
 * Reproducing the live run exactly is the whole requirement here. Two
 * things reset the correlator on air and both have to be reproduced in
 * the right order: a changed context, which diversity_auto.c checks at
 * the top of div_process_block(), and a gap in the sample stream, which
 * the worker acts on just before calling it. Both happen ahead of the
 * capture tap, so the state recorded in a block is the state *after*
 * them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "rade_correlator.h"
#include "divcap_replay.h"

/*
 * Box-Muller, with its own state so a run is reproducible from the seed
 * alone and does not depend on anything else in the process having used
 * rand().
 */
static unsigned nz_state = 1u;

static double nz_uniform(void) {
  nz_state = nz_state * 1103515245u + 12345u;
  return ((double)((nz_state >> 8) & 0xFFFFFFu) + 1.0) / 16777217.0;
}

void divcap_noise_seed(unsigned seed) { nz_state = seed + 1u; }

static double nz_gauss(void) {
  static int have = 0;
  static double spare = 0.0;

  if (have) { have = 0; return spare; }

  const double u = nz_uniform(), v = nz_uniform();
  const double r = sqrt(-2.0 * log(u));
  spare = r * sin(2.0 * M_PI * v);
  have = 1;
  return r * cos(2.0 * M_PI * v);
}

/*
 * Independent AWGN on both arms, per component. Shared with score_rade so
 * that a decode run and a detector run over the same capture, seed and
 * level see exactly the same noise.
 */
void divcap_add_noise(float *arm0, float *arm1, int nfft, double rms) {
  if (rms <= 0.0) { return; }

  for (int i = 0; i < 2 * nfft; i++) {
    arm0[i] += (float)(rms * nz_gauss());
    arm1[i] += (float)(rms * nz_gauss());
  }
}

/*
 * Everything about a block except the samples, kept so the replay can
 * decide about resets exactly the way diversity_auto.c did.
 */
#define DIVCAP_RETUNE_HZ 20    /* mirrors DIV_RETUNE_HZ in diversity_auto.c */

static int ctx_differs(const struct divcap_block *a, const struct divcap_block *b) {
  return llabs(a->frequency      - b->frequency)      > DIVCAP_RETUNE_HZ ||
         llabs(a->ctun_frequency - b->ctun_frequency) > DIVCAP_RETUNE_HZ ||
         llabs(a->offset         - b->offset)         > DIVCAP_RETUNE_HZ ||
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

static int near(double a, double b) {
  const double d = fabs(a - b);
  return d <= 1.0e-9 + 1.0e-6 * fabs(b);
}

/*
 * One pass over the file with whatever is currently in rade_tuning.
 */
int divcap_replay(FILE *f, const struct divcap_header *h, long data_start,
                  const struct divcap_opts *opts, struct divcap_result *r) {
  static const struct divcap_opts none = { 0 };
  const struct divcap_opts *o = (opts != NULL) ? opts : &none;
  const int    verify = o->verify;
  const int    nfft  = (int)h->nfft;
  const size_t half  = (size_t)nfft * 2u * sizeof(float);
  float       *arm0  = malloc(half);
  float       *arm1  = malloc(half);
  struct divcap_block m, prev;
  int   have_prev = 0;
  int   was_locked = 0;
  double sw_re = 0.0, sw_im = 0.0, sw_n = 0.0;
  double sq_re = 0.0, sq_im = 0.0;
  double snr_sum = 0.0, q_sum = 0.0;

  if (arm0 == NULL || arm1 == NULL) { free(arm0); free(arm1); return 0; }

  memset(r, 0, sizeof(*r));
  r->first_lock = -1.0;
  divcap_noise_seed(o->seed);

  if (o->weights != NULL) {
    fprintf(o->weights, "block,t,locked,confirming,quality,snr,freq_off,ok,wr,wi\n");
  }

  if (!rade_corr_start((int)h->sample_rate)) {
    fprintf(stderr, "replay: correlator will not run at %u Hz\n", h->sample_rate);
    free(arm0);
    free(arm1);
    return 0;
  }

  /*
   * rade_corr_reset(), which rade_corr_start() calls, clears every
   * exported status word except these two - they are only ever written
   * when a lock is taken, so they survive a reset and a stop/start with
   * the previous lock's values still in them.
   *
   * Harmless enough on air (the menu shows a stale sideband and frequency
   * for as long as it takes to re-acquire) but not here: one sweep point
   * would start with the last one's answer in place, and a run that never
   * locked would report the frequency of one that did. Cleared from the
   * harness rather than by touching src/rade_correlator.c, which this
   * instrument does not modify.
   */
  rade_corr_freq_off = 0.0;
  rade_corr_mirrored = 0;

  if (fseek(f, data_start, SEEK_SET) != 0) { goto done; }

  for (;;) {
    if (fread(&m, sizeof(m), 1, f) != 1) { break; }

    if (m.rec_magic != DIVCAP_REC_MAGIC) {
      /* The trailer, or a truncated file. Either way the blocks are done. */
      break;
    }

    if (fread(arm0, 1, half, f) != half) { break; }

    if (fread(arm1, 1, half, f) != half) { break; }

    /*
     * Additive white Gaussian noise, independent per arm and per
     * component, which is the right model for two receivers' own thermal
     * noise: it is the part of the noise the array cannot null, so
     * raising it is what walks a capture down towards the threshold.
     * Anything common to both antennas is already in the recording.
     */
    divcap_add_noise(arm0, arm1, nfft, o->noise);

    /*
     * The two things diversity_auto.c resets for, in the order it does
     * them: a changed context inside div_process_block(), and a gap in
     * the sample stream in the worker just before it. Both happen ahead
     * of the capture tap, so the state recorded in this block is the
     * state after them - which is why the comparison below comes after.
     */
    if (have_prev && ctx_differs(&m, &prev)) {
      rade_corr_reset();
      prev = m;
    }

    if (m.dropped > 0) { rade_corr_reset(); }

    if (verify) {
      /*
       * Only the correlator's own state is checked. live_cos/live_sin and
       * the track_* pair describe what the auto-phasing loop did with the
       * answer - slewing, Hold, the objective sign - and this harness
       * does not run that loop.
       */
      int bad = 0;
      bad |= (rade_corr_locked     != m.live_locked);
      bad |= (rade_corr_confirming != m.live_confirming);
      bad |= (rade_corr_mirrored   != m.live_mirrored);
      bad |= !near(rade_corr_quality,  m.live_quality);
      bad |= !near(rade_corr_freq_off, m.live_freq_off);
      bad |= !near(rade_corr_snr,      m.live_snr);

      if ((int)m.seq < o->from_block) { bad = 0; }
      else { r->verify_checked++; }

      if (bad) {
        if (r->verify_bad < 8) {
          fprintf(stderr,
                  "verify: block %u differs: lock %d/%d confirm %d/%d mirror %d/%d "
                  "q %.9g/%.9g f %.9g/%.9g snr %.9g/%.9g  (replay/recorded)\n",
                  m.seq,
                  rade_corr_locked, m.live_locked,
                  rade_corr_confirming, m.live_confirming,
                  rade_corr_mirrored, m.live_mirrored,
                  rade_corr_quality, m.live_quality,
                  rade_corr_freq_off, m.live_freq_off,
                  rade_corr_snr, m.live_snr);
        }

        r->verify_bad++;
      }
    }

    double wr = 0.0, wi = 0.0;
    const double tau  = (o->tau  > 0.0) ? o->tau  : m.tau;
    const double hang = (o->hang > 0.0) ? o->hang : m.hang;
    const int ok = rade_corr_process(arm0, arm1, nfft, m.expect_bank,
                                     m.frame_off, tau, hang, &wr, &wi);
    const double t = (double)r->blocks * (double)nfft / (double)h->sample_rate;

    if (o->weights != NULL) {
      /*
       * The weight the correlator produced, not the weight the radio
       * applied: div_apply_weight()'s slew, the Null sign and Hold all
       * belong to the auto-phasing loop, which is not what is being
       * measured here.
       */
      fprintf(o->weights, "%u,%.4f,%d,%d,%.6g,%.4f,%.4f,%d,%.9g,%.9g\n",
              m.seq, t, rade_corr_locked, rade_corr_confirming,
              rade_corr_quality, rade_corr_snr, rade_corr_freq_off, ok, wr, wi);
    }

    r->blocks++;

    if (rade_corr_locked) {
      r->locked_blocks++;
      snr_sum += rade_corr_snr;
      q_sum   += rade_corr_quality;

      if (!was_locked) {
        r->acquisitions++;

        if (r->first_lock < 0.0) { r->first_lock = t; }
      }
    }

    was_locked = rade_corr_locked;

    if (ok) {
      sw_re += wr;
      sw_im += wi;
      sq_re += wr * wr;
      sq_im += wi * wi;
      sw_n  += 1.0;
    }

    if (!have_prev) { prev = m; }

    have_prev = 1;
  }

  r->seconds = (double)r->blocks * (double)nfft / (double)h->sample_rate;

  if (r->locked_blocks > 0) {
    r->mean_snr     = snr_sum / r->locked_blocks;
    r->mean_quality = q_sum / r->locked_blocks;
  }

  if (sw_n > 1.0) {
    /*
     * Movement of the weight about its own mean. On a static path this is
     * the jitter the averaging time is trading against; on a fading one it
     * is jitter plus the real motion of the channel, so compare it only
     * between runs over the same capture.
     */
    const double vr = sq_re / sw_n - (sw_re / sw_n) * (sw_re / sw_n);
    const double vi = sq_im / sw_n - (sw_im / sw_n) * (sw_im / sw_n);
    r->weight_jitter = sqrt(((vr > 0.0) ? vr : 0.0) + ((vi > 0.0) ? vi : 0.0));
  }

done:
  rade_corr_stop();
  free(arm0);
  free(arm1);
  return 1;
}


FILE *divcap_open(const char *path, struct divcap_header *h, long *data_start) {
  FILE *f = fopen(path, "rb");

  if (f == NULL) { perror(path); return NULL; }

  if (fread(h, sizeof(*h), 1, f) != 1 || memcmp(h->magic, DIVCAP_MAGIC, 8) != 0) {
    fprintf(stderr, "%s: not a diversity capture\n", path);
    fclose(f);
    return NULL;
  }

  if (h->version != DIVCAP_VERSION) {
    fprintf(stderr, "%s: format version %u, this tool speaks %u\n",
            path, h->version, DIVCAP_VERSION);
    fclose(f);
    return NULL;
  }

  *data_start = ftell(f);
  return f;
}
