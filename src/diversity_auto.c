/* Copyright (C)
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#include <gtk/gtk.h>
#include <math.h>
#include <fftw3.h>

#include <wdsp.h>

#include "diversity_auto.h"
#include "message.h"
#include "mode.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"

//
// ----------------------------------------------------------------------
// Theory of operation
// ----------------------------------------------------------------------
//
// rx_add_div_iq_samples() forms  z = z0 + w*z1  with a single complex
// weight w that is flat across the whole DDC passband. This module works
// out a value for w from the cross spectrum of the two raw streams.
//
// Every block of nfft sample pairs is windowed and transformed. Writing
// X0 and X1 for the two spectra, we accumulate, over the bins k that fall
// inside the analysis window,
//
//     Sxy = sum X0(k) * conj(X1(k))     Sxx = sum |X0(k)|^2
//                                       Syy = sum |X1(k)|^2
//
// with an exponential forgetting factor across blocks, and then take
//
//   DIV_AUTO_NULL:  w = -Sxy/Syy    minimises E|z0 + w*z1|^2, i.e. it
//                                   subtracts whatever is common to both
//                                   antennas. This is the noise-cancelling
//                                   case and the default.
//
//   DIV_AUTO_SUM:   w = +Sxy/Sxx    equals conj(h) for z1 = h*z0, which is
//                                   maximum ratio combining when the two
//                                   channels carry equal noise power: the
//                                   antennas are co-phased and each is
//                                   weighted by its own signal strength.
//
// Note the two cases use *different* denominators. -Sxy/Syy and +Sxy/Sxx
// are not simply sign-flipped versions of one another.
//
// The quality of the fit is the magnitude squared coherence
//
//     gamma^2 = |Sxy|^2 / (Sxx*Syy)
//
// which is 1 when a single complex weight describes the relationship
// perfectly and 0 when the two antennas are unrelated. The loop holds
// (stops updating) below div_auto_coherence_min, which keeps it from
// wandering off when there is nothing worth combining.
//
// ----------------------------------------------------------------------
// Frequency bookkeeping
// ----------------------------------------------------------------------
//
// We tap the *raw* DDC streams, ahead of WDSP. WDSP's first stage is
// xshift(), which multiplies by exp(+j*2*pi*offset*t) with
// offset = vfo[0].offset, and everything after it - the operator's
// passband (RXASetPassband, filter_low/filter_high) and the SAM PLL's
// carrier frequency - is expressed in that shifted frame, where the tuned
// signal sits at zero.
//
// So a frequency f in the shifted frame is at f - offset in the raw frame,
// and that single relation covers both the filter edges and the PLL. We
// never have to know whether the raw baseband runs the same way as RF or
// is mirrored, because both quantities we care about arrive already
// expressed in the same frame.
//
// ----------------------------------------------------------------------
//

//
// Tunables. Target bin width, in Hz. The FFT length is chosen per sample
// rate to land near this, so the frequency resolution and the block
// duration are the same whatever the radio is running at.
//
#define DIV_TARGET_BIN_HZ   12.0
#define DIV_MIN_NFFT        4096
#define DIV_MAX_NFFT        65536

//
// Never let the automatic loop ask for more than this. The manual sliders
// go to +/-27 dB, but a large |w| means the aux antenna's own noise
// dominates the sum, and it costs headroom in everything downstream.
//
#define DIV_MAX_WEIGHT      10.0    // +20 dB

//
// Fraction of the remaining distance to the target that w moves in one
// block. With ~85 ms blocks this settles in a little over a second from
// any starting point, which is fast enough to be useful and slow enough
// that the change in the mix is not heard as a step. A fixed absolute
// step was tried first and is wrong: the time to converge then depends on
// how far away the answer is, and a large |w| took the best part of a
// minute to reach.
//
#define DIV_SLEW_FRAC       0.15

//
// Number of bins either side of the carrier bin used in DIV_REF_CARRIER.
// The window spreads a pure tone over a few bins.
//
#define DIV_CARRIER_BINS    2

int    div_auto_mode           = DIV_AUTO_OFF;
int    div_auto_ref            = DIV_REF_BAND;
int    div_auto_follow_filter  = 1;
double div_auto_centre         = 0.0;
double div_auto_width          = 1000.0;
double div_auto_tau            = 2.0;
double div_auto_coherence_min  = 0.30;

double div_auto_coherence      = 0.0;
int    div_auto_holding        = 1;
double div_auto_carrier        = 0.0;
int    div_auto_carrier_valid  = 0;

int    div_auto_running        = 0;

//
// FFT state, owned by the analysis thread once it is started
//
static int             nfft = 0;
static double          binhz = 0.0;
static double          blocktime = 0.0;
static float          *window = NULL;
static fftwf_complex  *fftin0 = NULL, *fftin1 = NULL;
static fftwf_complex  *fftout0 = NULL, *fftout1 = NULL;
static fftwf_plan      plan0, plan1;
static int             have_plans = 0;

//
// Sample collection. fill[] is written by the RX sample path, work[] is
// read by the analysis thread; the two are swapped when a block is ready.
//
static float          *fill0 = NULL, *fill1 = NULL;
static float          *work0 = NULL, *work1 = NULL;
static int             fillptr = 0;

static GMutex          mbox_mutex;
static GCond           mbox_cond;
static int             mbox_full = 0;
static int             mbox_quit = 0;
static GThread        *worker = NULL;

//
// Accumulated statistics
//
static double          acc_xy_re, acc_xy_im, acc_xx, acc_yy;
static int             acc_valid = 0;

//
// Everything the bin mask depends on. When any of it changes the
// accumulated statistics describe a different measurement and have to be
// thrown away, so we watch it here rather than hooking every call site
// that could move the radio.
//
struct div_context {
  long long frequency;
  long long ctun_frequency;
  long long offset;
  int       sample_rate;
  int       mode;
  int       filter_low;
  int       filter_high;
  int       ref;
  int       follow;
  double    centre;
  double    width;
};

static struct div_context lastctx;

static void div_reset_stats(void) {
  acc_xy_re = acc_xy_im = acc_xx = acc_yy = 0.0;
  acc_valid = 0;
  div_auto_coherence = 0.0;
  div_auto_holding = 1;
}

void diversity_auto_reset(void) {
  //
  // Safe to call from any thread: the analysis thread only ever adds to
  // the accumulators, so zeroing them can at worst cost one block.
  //
  div_reset_stats();
}

//
// Pick an FFT length for this sample rate. Powers of two only, so that
// fftw takes its fast path.
//
static int div_choose_nfft(int sample_rate) {
  int n = DIV_MIN_NFFT;

  while (n < DIV_MAX_NFFT && (double)sample_rate / (double)n > DIV_TARGET_BIN_HZ) {
    n <<= 1;
  }

  return n;
}

//
// 4-term Blackman-Harris. The whole point of the analysis window is to
// look at one narrow slice of spectrum and ignore everything else, so the
// -92 dB sidelobes are worth having over the -31 dB of a Hann.
//
static void div_make_window(void) {
  const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;

  for (int i = 0; i < nfft; i++) {
    double x = 2.0 * M_PI * (double)i / (double)nfft;
    window[i] = (float)(a0 - a1 * cos(x) + a2 * cos(2.0 * x) - a3 * cos(3.0 * x));
  }
}

//
// Snapshot everything the bin mask depends on.
//
static void div_get_context(struct div_context *ctx) {
  const RECEIVER *rx = receiver[0];
  ctx->frequency      = vfo[0].frequency;
  ctx->ctun_frequency = vfo[0].ctun_frequency;
  ctx->offset         = vfo[0].offset;
  ctx->sample_rate    = rx->sample_rate;
  ctx->mode           = vfo[0].mode;
  ctx->filter_low     = rx->filter_low;
  ctx->filter_high    = rx->filter_high;
  ctx->ref            = div_auto_ref;
  ctx->follow         = div_auto_follow_filter;
  ctx->centre         = div_auto_centre;
  ctx->width          = div_auto_width;
}

static int div_context_changed(const struct div_context *a, const struct div_context *b) {
  return a->frequency      != b->frequency      ||
         a->ctun_frequency != b->ctun_frequency ||
         a->offset         != b->offset         ||
         a->sample_rate    != b->sample_rate    ||
         a->mode           != b->mode           ||
         a->filter_low     != b->filter_low     ||
         a->filter_high    != b->filter_high    ||
         a->ref            != b->ref            ||
         a->follow         != b->follow         ||
         a->centre         != b->centre         ||
         a->width          != b->width;
}

//
// Work out which bins to accumulate over, as an inclusive range of
// unwrapped indices (they may be negative; the caller wraps them).
// Returns 0 if there is nothing usable to measure.
//
static int div_bin_range(const struct div_context *ctx, int *klo, int *khi) {
  double flo, fhi;

  if (ctx->ref == DIV_REF_CARRIER) {
    //
    // Method B: the carrier bin only, located by the SAM PLL. The PLL is
    // only run in SAM (in plain AM the demodulator is an envelope
    // detector and the PLL state is stale), so there is nothing to
    // measure in any other mode.
    //
    if (ctx->mode != modeSAM || !GetRXAAMDPLLRunning(0)) {
      div_auto_carrier = 0.0;
      div_auto_carrier_valid = 0;
      return 0;
    }

    div_auto_carrier = GetRXAAMDCarrierFreq(0);
    div_auto_carrier_valid = 1;
    flo = div_auto_carrier - DIV_CARRIER_BINS * binhz;
    fhi = div_auto_carrier + DIV_CARRIER_BINS * binhz;
  } else if (ctx->follow) {
    //
    // Method A following the operator's filter.
    //
    flo = ctx->filter_low;
    fhi = ctx->filter_high;
  } else {
    //
    // Method A with a hand-placed window: park it on a known noise, or
    // size it to take in just the mark and space tones of an FSK signal.
    //
    flo = ctx->centre - 0.5 * ctx->width;
    fhi = ctx->centre + 0.5 * ctx->width;
  }

  if (fhi <= flo) { return 0; }

  //
  // Shifted frame -> raw frame. See the note at the top of this file.
  //
  flo -= (double)ctx->offset;
  fhi -= (double)ctx->offset;
  *klo = (int)floor(flo / binhz);
  *khi = (int)ceil (fhi / binhz);

  //
  // A window wider than the DDC passband is meaningless, and one that has
  // collapsed to nothing gives us no statistics at all.
  //
  if (*khi - *klo + 1 > nfft) { return 0; }

  if (*khi < *klo) { return 0; }

  return 1;
}

//
// Write a new weight, rate limited. Called from the analysis thread.
//
static void div_apply_weight(double wr, double wi) {
  double mag = sqrt(wr * wr + wi * wi);

  if (!isfinite(wr) || !isfinite(wi)) { return; }

  if (mag > DIV_MAX_WEIGHT) {
    wr *= DIV_MAX_WEIGHT / mag;
    wi *= DIV_MAX_WEIGHT / mag;
  }

  //
  // The sample path reads div_cos and div_sin one after the other without
  // a lock, so a read can catch the old value of one and the new value of
  // the other. That costs a single sample computed with a mismatched pair
  // - inaudible - and the alternative, locking per sample at up to 384 kHz,
  // is not worth it.
  //
  div_cos += DIV_SLEW_FRAC * (wr - div_cos);
  div_sin += DIV_SLEW_FRAC * (wi - div_sin);
  //
  // Back-compute the values the menu, the props file and remote clients
  // work in, so everything stays consistent with what is actually being
  // applied to the samples.
  //
  mag = sqrt(div_cos * div_cos + div_sin * div_sin);

  if (mag > 1.0e-9) {
    div_gain = 20.0 * log10(mag);
  } else {
    div_gain = -27.0;
  }

  if (div_gain >  27.0) { div_gain =  27.0; }

  if (div_gain < -27.0) { div_gain = -27.0; }

  div_phase = atan2(div_sin, div_cos) * (180.0 / M_PI);
}

//
// Process one block. Runs on the analysis thread.
//
static void div_process_block(void) {
  struct div_context ctx;
  int klo, khi;

  if (receivers < 1 || receiver[0] == NULL) {
    div_auto_holding = 1;
    return;
  }

  div_get_context(&ctx);

  if (div_context_changed(&ctx, &lastctx)) {
    //
    // The radio moved under us: anything we accumulated describes a
    // different measurement.
    //
    div_reset_stats();
    lastctx = ctx;
  }

  if (!div_bin_range(&ctx, &klo, &khi)) {
    //
    // Nothing to measure - in DIV_REF_CARRIER this is the normal state
    // whenever the mode is not SAM - so do not spend two transforms on it.
    //
    div_auto_holding = 1;
    return;
  }

  for (int i = 0; i < nfft; i++) {
    fftin0[i][0] = work0[2 * i    ] * window[i];
    fftin0[i][1] = work0[2 * i + 1] * window[i];
    fftin1[i][0] = work1[2 * i    ] * window[i];
    fftin1[i][1] = work1[2 * i + 1] * window[i];
  }

  fftwf_execute(plan0);
  fftwf_execute(plan1);
  double bxy_re = 0.0, bxy_im = 0.0, bxx = 0.0, byy = 0.0;

  for (int k = klo; k <= khi; k++) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    double i0 = fftout0[idx][0], q0 = fftout0[idx][1];
    double i1 = fftout1[idx][0], q1 = fftout1[idx][1];
    //
    // X0 * conj(X1)
    //
    bxy_re += i0 * i1 + q0 * q1;
    bxy_im += q0 * i1 - i0 * q1;
    bxx    += i0 * i0 + q0 * q0;
    byy    += i1 * i1 + q1 * q1;
  }

  //
  // Exponential forgetting across blocks
  //
  double alpha = 1.0 - exp(-blocktime / div_auto_tau);

  if (!acc_valid) {
    alpha = 1.0;
    acc_valid = 1;
  }

  acc_xy_re += alpha * (bxy_re - acc_xy_re);
  acc_xy_im += alpha * (bxy_im - acc_xy_im);
  acc_xx    += alpha * (bxx    - acc_xx);
  acc_yy    += alpha * (byy    - acc_yy);

  if (acc_xx <= 0.0 || acc_yy <= 0.0) {
    div_auto_coherence = 0.0;
    div_auto_holding = 1;
    return;
  }

  double xy2 = acc_xy_re * acc_xy_re + acc_xy_im * acc_xy_im;
  div_auto_coherence = xy2 / (acc_xx * acc_yy);

  if (div_auto_coherence > 1.0) { div_auto_coherence = 1.0; }

  if (div_auto_coherence < div_auto_coherence_min) {
    //
    // Nothing the two antennas agree on. Hold what we have rather than
    // chase noise.
    //
    div_auto_holding = 1;
    return;
  }

  div_auto_holding = 0;
  double den = (div_auto_mode == DIV_AUTO_SUM) ? acc_xx : acc_yy;
  double sign = (div_auto_mode == DIV_AUTO_SUM) ? 1.0 : -1.0;
  div_apply_weight(sign * acc_xy_re / den, sign * acc_xy_im / den);
}

static gpointer div_worker_thread(gpointer data) {
  (void) data;
  t_print("%s: diversity auto-phasing analysis thread running\n", __func__);

  for (;;) {
    g_mutex_lock(&mbox_mutex);

    while (!mbox_full && !mbox_quit) {
      g_cond_wait(&mbox_cond, &mbox_mutex);
    }

    if (mbox_quit) {
      g_mutex_unlock(&mbox_mutex);
      break;
    }

    g_mutex_unlock(&mbox_mutex);
    //
    // mbox_full stays set while we work, so the sample path drops blocks
    // instead of waiting for us. Dropping is harmless here: we are
    // estimating something that changes far more slowly than one block.
    //
    div_process_block();
    g_mutex_lock(&mbox_mutex);
    mbox_full = 0;
    g_mutex_unlock(&mbox_mutex);
  }

  t_print("%s: diversity auto-phasing analysis thread stopped\n", __func__);
  return NULL;
}

void diversity_auto_sample(double i0, double q0, double i1, double q1) {
  //
  // Called once per sample pair from rx_add_div_iq_samples(), on the
  // protocol receive thread. Nothing but stores happens here.
  //
  fill0[2 * fillptr    ] = (float)i0;
  fill0[2 * fillptr + 1] = (float)q0;
  fill1[2 * fillptr    ] = (float)i1;
  fill1[2 * fillptr + 1] = (float)q1;
  fillptr++;

  if (fillptr < nfft) { return; }

  fillptr = 0;
  g_mutex_lock(&mbox_mutex);

  if (!mbox_full) {
    float *t0 = work0, *t1 = work1;
    work0 = fill0;
    work1 = fill1;
    fill0 = t0;
    fill1 = t1;
    mbox_full = 1;
    g_cond_signal(&mbox_cond);
  }

  g_mutex_unlock(&mbox_mutex);
}

void diversity_auto_start(void) {
  if (div_auto_running) { return; }

  if (div_auto_mode == DIV_AUTO_OFF) { return; }

  if (!diversity_enabled || receivers < 1 || receiver[0] == NULL) { return; }

  //
  // On a remote client the samples are combined on the server side and
  // rx_add_div_iq_samples() never runs here, so there would be nothing to
  // analyse.
  //
  if (radio_is_remote) { return; }

  nfft = div_choose_nfft(receiver[0]->sample_rate);
  binhz = (double)receiver[0]->sample_rate / (double)nfft;
  blocktime = (double)nfft / (double)receiver[0]->sample_rate;
  //
  // The buffers are allocated once, at the largest size we will ever use,
  // and then kept for the lifetime of the program. The RX sample path
  // checks div_auto_running without any lock and can already be inside
  // diversity_auto_sample() when diversity_auto_stop() runs, so freeing
  // these on stop would be a use-after-free. Holding on to them costs a
  // few MB and makes start/stop trivially safe: a write that arrives late
  // lands in a buffer that is still valid, and the worst that happens is
  // one block of stale data.
  //
  if (window == NULL) {
    window  = g_new(float, DIV_MAX_NFFT);
    fill0   = g_new0(float, 2 * DIV_MAX_NFFT);
    fill1   = g_new0(float, 2 * DIV_MAX_NFFT);
    work0   = g_new0(float, 2 * DIV_MAX_NFFT);
    work1   = g_new0(float, 2 * DIV_MAX_NFFT);
    fftin0  = fftwf_malloc(sizeof(fftwf_complex) * DIV_MAX_NFFT);
    fftin1  = fftwf_malloc(sizeof(fftwf_complex) * DIV_MAX_NFFT);
    fftout0 = fftwf_malloc(sizeof(fftwf_complex) * DIV_MAX_NFFT);
    fftout1 = fftwf_malloc(sizeof(fftwf_complex) * DIV_MAX_NFFT);
  }

  div_make_window();
  //
  // Plan creation is not thread safe, so it happens here, before the
  // analysis thread exists. ESTIMATE rather than MEASURE: planning a
  // 65536-point transform with MEASURE can stall for seconds, which is
  // not acceptable when the operator has just flipped a switch.
  //
  plan0 = fftwf_plan_dft_1d(nfft, fftin0, fftout0, FFTW_FORWARD, FFTW_ESTIMATE);
  plan1 = fftwf_plan_dft_1d(nfft, fftin1, fftout1, FFTW_FORWARD, FFTW_ESTIMATE);
  have_plans = 1;
  fillptr = 0;
  mbox_full = 0;
  mbox_quit = 0;
  div_reset_stats();
  div_get_context(&lastctx);
  t_print("%s: nfft=%d bin=%0.2f Hz block=%0.1f ms rate=%d\n", __func__,
          nfft, binhz, 1000.0 * blocktime, receiver[0]->sample_rate);
  worker = g_thread_new("div_auto", div_worker_thread, NULL);
  //
  // Set last: the sample path tests this without any lock.
  //
  div_auto_running = 1;
}

void diversity_auto_stop(void) {
  if (!div_auto_running) { return; }

  //
  // Stop the sample path feeding us first, then wake the thread so it can
  // see the quit flag.
  //
  div_auto_running = 0;
  g_mutex_lock(&mbox_mutex);
  mbox_quit = 1;
  g_cond_signal(&mbox_cond);
  g_mutex_unlock(&mbox_mutex);

  if (worker != NULL) {
    g_thread_join(worker);
    worker = NULL;
  }

  if (have_plans) {
    fftwf_destroy_plan(plan0);
    fftwf_destroy_plan(plan1);
    have_plans = 0;
  }

  //
  // The sample and FFT buffers are deliberately not freed here; see the
  // note in diversity_auto_start().
  //
  div_auto_coherence = 0.0;
  div_auto_holding = 1;
}

void diversity_auto_restart(void) {
  diversity_auto_stop();

  if (diversity_enabled && div_auto_mode != DIV_AUTO_OFF) {
    diversity_auto_start();
  }
}

void diversity_auto_save_state(void) {
  SetPropI0("diversity_auto_mode",           div_auto_mode);
  SetPropI0("diversity_auto_ref",            div_auto_ref);
  SetPropI0("diversity_auto_follow_filter",  div_auto_follow_filter);
  SetPropF0("diversity_auto_centre",         div_auto_centre);
  SetPropF0("diversity_auto_width",          div_auto_width);
  SetPropF0("diversity_auto_tau",            div_auto_tau);
  SetPropF0("diversity_auto_coherence_min",  div_auto_coherence_min);
}

void diversity_auto_restore_state(void) {
  GetPropI0("diversity_auto_mode",           div_auto_mode);
  GetPropI0("diversity_auto_ref",            div_auto_ref);
  GetPropI0("diversity_auto_follow_filter",  div_auto_follow_filter);
  GetPropF0("diversity_auto_centre",         div_auto_centre);
  GetPropF0("diversity_auto_width",          div_auto_width);
  GetPropF0("diversity_auto_tau",            div_auto_tau);
  GetPropF0("diversity_auto_coherence_min",  div_auto_coherence_min);

  if (div_auto_tau < 0.1) { div_auto_tau = 0.1; }

  if (div_auto_width < 10.0) { div_auto_width = 10.0; }
}
