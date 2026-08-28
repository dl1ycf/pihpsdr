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
#include <string.h>
#include <fftw3.h>

#include "diversity_auto.h"
#include "message.h"
#include "mode.h"
#include "property.h"
#include "radio.h"
#include "rade_correlator.h"
#include "receiver.h"
#include "vfo.h"

//
// ----------------------------------------------------------------------
// Theory of operation
// ----------------------------------------------------------------------
//
// rx_add_div_iq_samples() forms  z = z0 + w*z1  with a single complex
// weight w that is flat across the whole DDC passband. This module works
// out a value for w.
//
// Three of the four reference modes do that from the cross spectrum of
// the two raw streams, as described below. The fourth, DIV_REF_RADE_V1,
// uses no transform at all: it hands the block to rade_correlator.c,
// which correlates against the known FreeDV RADE pilot and solves for an
// MVDR weight. See that file.
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
// We tap the *raw* DDC streams, ahead of WDSP. The operator's passband
// (filter_low/filter_high) and the window controls are expressed in
// WDSP's shifted frame, where the tuned signal sits at zero. Converting
// between the two takes an offset and a sign, and both have been wrong
// here at different times.
//
// The offset. WDSP's frame is displaced from the dial by
//
//     frame_off = vfo[0].offset, less the CW sidetone frequency in CWU
//                 and plus it in CWL
//
// because rx_set_filter() folds the sidetone into filter_low/filter_high
// and rx_set_offset() takes it back out before handing the shift to WDSP.
// The panadapter draws the filter edges at cAp*filter_low + cAp*offset
// with the same sidetone terms, and WDSP's notch database compares
// absolute RF notch frequencies against flow + tunefreq + shift
// (wdsp/nbp.c). It is also the only arrangement that puts the CW passband
// on the dial frequency. So in *RF* terms, a shifted-frame frequency s is
// at dial + frame_off + s.
//
// The sign. The tapped buffer is spectrally inverted with respect to RF:
// a signal above the dial appears at a *negative* complex frequency in
// it, and one below the dial at a positive one. So
//
//     bin frequency = -(s + frame_off)
//
// which is what div_shift_to_bin() computes.
//
// That inversion is not derived, it is measured, and it has now been
// measured three times on air:
//
//   - the wideband RADE mode compares the energy in the modem band on
//     each side of the carrier. On an LSB RADE signal it found the energy
//     at positive bin frequencies;
//   - the V1 pilot correlator searches a normal and a mirrored pilot
//     bank. On an LSB signal it locks the *normal* bank - carriers at
//     +750..+2200 - twice, on separate occasions, by a wide margin
//     (7.97 against 4.75 on a weak signal);
//   - which is also what an operator expects: LSB inverts the audio on
//     transmit, and if the path to this tap inverts it again the two
//     cancel and the modem arrives the right way up.
//
// Reading the code does not give this answer, and three attempts to
// derive it produced two different wrong ones. The chain that ought to
// settle it - wdsp/shift.c, wdsp/analyzer.c and the panadapter's pixel
// mapping - cannot all three be read consistently with each other, and
// the measurement does not care. If this is ever revisited, revisit it
// with a signal, not with a text editor: put a known carrier a few kHz
// off the dial, run the Carrier reference, and see which way
// div_auto_carrier moves.
//
// ----------------------------------------------------------------------
//

//
// Tunables. Target bin width, in Hz. The FFT length is chosen per sample
// rate to land near this, so the frequency resolution and the block
// duration are the same whatever the radio is running at.
//
// The default target; the operator can ask for finer bins - see
// div_auto_resolution.
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


//
// Bin-weighting for the wideband window.
//
// Flat sums the cross and auto spectra over the window and divides, which
// makes the answer a power-weighted average of h(f): dominated by the
// loudest bins whether or not the two antennas actually agree there, and
// diluted by noise-only bins that add to the denominator but not the
// numerator.
//
// Coherence weights each bin by how well the antennas agree in it, so
// bins carrying signal dominate and noise-only bins fall out. That is
// what makes a wide window usable on SSB voice, where the energy moves
// around constantly and there is no carrier to sit on.
//
// (the enum itself is in diversity_auto.h)

int    div_auto_mode           = DIV_AUTO_OFF;
int    div_auto_ref            = DIV_REF_BAND;
int    div_auto_follow_filter  = 1;
double div_auto_centre         = 0.0;
double div_auto_width          = 1000.0;
double div_auto_tau            = 2.0;
double div_auto_coherence_min  = 0.30;
int    div_auto_weighting      = DIV_WEIGHT_COHERENCE;
double div_auto_resolution     = DIV_TARGET_BIN_HZ;

//
// The window controls are modal: DIV_REF_BAND and DIV_REF_CARRIER each
// keep their own centre and width, so moving between them does not
// destroy the other's setting. div_auto_centre/width always hold the pair
// for whichever reference is selected; these hold the pair for the other.
//
double div_band_centre         = 0.0;
double div_band_width          = 1000.0;
double div_carrier_centre      = 0.0;
double div_carrier_width       = 1000.0;

//
// Set when the requested window had to be pulled inside the Nyquist
// limit, so the UI can say so rather than quietly measuring elsewhere.
//
int    div_auto_clamped        = 0;

//
// The bin width actually achieved, which is not always the one asked for:
// nfft is capped at DIV_MAX_NFFT.
//
double div_auto_binhz          = 0.0;

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
//
// A short queue rather than a single slot.
//
// The original design handed over one block at a time and dropped any
// block that arrived while the worker was busy, on the grounds that the
// estimate moves far more slowly than one block. That is true for the
// three transform-based reference modes and quite wrong for RADE V1,
// which tracks the pilot by *absolute* decimated sample index and carries
// the NCO phase and the decimator delay line across blocks. A dropped
// block slides the real pilot by a non-multiple of the modem frame -
// 682 samples at 192 kHz against a 960-sample frame - which the one
// sample of timing nudge in the tracker cannot recover, so the lock is
// lost a few seconds later.
//
// It was also self-inflicted: acquisition is by far the most expensive
// thing the worker does, so drops were most likely precisely while
// searching, and were then repeated for up to RADE_ACQ_PASSES passes.
//
// The queue holds DIV_QUEUE buffers, one of which is always the one being
// filled, so at most DIV_QUEUE-1 are ever waiting.
//
#define DIV_QUEUE 4

static float          *qbuf0[DIV_QUEUE], *qbuf1[DIV_QUEUE];
static int             q_head = 0;      // slot being filled
static int             q_tail = 0;      // slot being processed
static int             q_count = 0;     // slots waiting
static float          *fill0 = NULL, *fill1 = NULL;
static float          *work0 = NULL, *work1 = NULL;
static int             fillptr = 0;

//
// Blocks the sample path had to throw away because the queue was full.
// Read and cleared by the worker: a gap in the sample stream invalidates
// RADE V1's pilot timing, so it has to re-acquire rather than carry on
// against a pilot that has silently moved.
//
static int             q_dropped = 0;

//
// Set by diversity_auto_reset() on the GTK thread, consumed by the worker
// between blocks. See the note there.
//
static int             reset_requested = 0;

static GMutex          mbox_mutex;
static GCond           mbox_cond;
static int             mbox_quit = 0;
static GThread        *worker = NULL;

//
// Accumulated statistics
//
static double          acc_xy_re, acc_xy_im, acc_xx, acc_yy;

//
// Per-bin running cross and auto spectra, allocated at DIV_MAX_NFFT with
// the rest of the buffers. Indexed by wrapped bin, so only the bins
// inside the current window are ever touched.
//
static double         *bin_xy_re = NULL, *bin_xy_im = NULL;
static double         *bin_xx = NULL, *bin_yy = NULL;
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
  int       sidetone;
  int       sample_rate;
  int       mode;
  int       filter_low;
  int       filter_high;
  int       ref;
  int       follow;
  double    centre;
  double    width;
  int       weighting;
};

static struct div_context lastctx;

//
// +1 when the RADE modem is above the tuned carrier in this frame, -1
// when below. Chosen from the measured spectrum, on every block in which
// DIV_REF_RADE_BAND is the active reference; the other modes leave it
// alone (DIV_REF_RADE_V1 determines the sense from its pilot bank, and
// never reaches the transform).
//
static int div_rade_side = 1;

//
// Set when the next weight update should be applied without slewing.
//
static int div_jump = 0;

//
// Smoothed carrier frequency, shifted frame, for DIV_REF_CARRIER.
//
static double div_carrier_hz = 0.0;


void diversity_auto_jump(void) {
  div_jump = 1;
}

int div_rade_side_get(void) {
  return div_rade_side;
}

static void div_reset_stats(void) {
  acc_xy_re = acc_xy_im = acc_xx = acc_yy = 0.0;

  if (bin_xy_re != NULL) {
    memset(bin_xy_re, 0, DIV_MAX_NFFT * sizeof(double));
    memset(bin_xy_im, 0, DIV_MAX_NFFT * sizeof(double));
    memset(bin_xx,    0, DIV_MAX_NFFT * sizeof(double));
    memset(bin_yy,    0, DIV_MAX_NFFT * sizeof(double));
  }

  acc_valid = 0;
  div_auto_coherence = 0.0;
  div_auto_holding = 1;
  div_carrier_hz = 0.0;
  div_auto_carrier_valid = 0;
}

void diversity_auto_reset(void) {
  //
  // Called from the GTK thread. Zeroing the transform accumulators from
  // here is harmless - the worker only ever adds to them, so the worst
  // case is one block's contribution lost.
  //
  // rade_corr_reset() is a different matter: it clears the correlator's
  // lock state and memsets an 80 KB accumulation grid that the worker may
  // be part way through reading. So it is requested here and performed by
  // the worker between blocks instead.
  //
  div_reset_stats();
  reset_requested = 1;
}

//
// Pick an FFT length for this sample rate. Powers of two only, so that
// fftw takes its fast path.
//
//
// Pick the transform length for a requested bin width.
//
// Finer bins raise a weak carrier further out of the per-bin noise floor,
// which is the real sensitivity control - averaging only reduces the
// variance of an estimate, it does not lift the signal. The cost is
// responsiveness: the block period is nfft/rate, so every halving of the
// bin width doubles it.
//
// nfft is capped at DIV_MAX_NFFT rather than growing to meet the request,
// because the buffers are allocated at the cap whatever rate is running.
// The achieved bin width is published in div_auto_binhz so the UI can
// show what was actually obtained.
//
static int div_choose_nfft(int sample_rate, double target_hz) {
  int n = DIV_MIN_NFFT;

  if (target_hz < 0.5) { target_hz = 0.5; }

  while (n < DIV_MAX_NFFT && (double)sample_rate / (double)n > target_hz) {
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
// What the mode says about which sideband is in use.
//
// This is *not* how the RADE window is placed. Both RADE modes measure
// the side rather than deriving it - stage 1 from the energy either side
// of the carrier, stage 2 from which pilot bank correlates - because on
// air the mode-derived rule turned out to be the wrong way round.
//
// It survives only to be logged next to the measured answer, which is how
// that was established and is worth keeping visible.
//
//
// Offset of WDSP's shifted frame from our raw one: raw = shifted +
// div_frame_off(). See the frequency bookkeeping note at the top.
//
static double div_frame_off(const struct div_context *ctx) {
  double off = (double)ctx->offset;

  if (ctx->mode == modeCWU) {
    off -= (double)ctx->sidetone;
  } else if (ctx->mode == modeCWL) {
    off += (double)ctx->sidetone;
  }

  return off;
}

//
// Shifted frame -> the frequency our FFT bins are indexed by. Both the
// displacement and the inversion; see the note at the top.
//
static double div_shift_to_bin(const struct div_context *ctx, double s) {
  return -(s + div_frame_off(ctx));
}

//
// Which side of the tuned frequency the RADE modem is on, from the
// operator's own passband: the midpoint of filter_low..filter_high in the
// shifted frame. Returns 0 when the passband straddles zero (AM, SAM, FM,
// DSB), where it says nothing.
//
// The passband is used rather than vfo[].mode because it is what the
// operator actually set and it covers the digital modes without a table:
// an LSB-side passband is negative in this frame whatever the mode is
// called.
//
static int div_rade_side_expected(const struct div_context *ctx) {
  const double mid = 0.5 * ((double)ctx->filter_low + (double)ctx->filter_high);

  if (mid >  0.5 * RADE_CORR_FLO) { return  1; }

  if (mid < -0.5 * RADE_CORR_FLO) { return -1; }

  return 0;
}

//
// Snapshot everything the bin mask depends on.
//
static void div_get_context(struct div_context *ctx) {
  const RECEIVER *rx = receiver[0];
  ctx->frequency      = vfo[0].frequency;
  ctx->ctun_frequency = vfo[0].ctun_frequency;
  ctx->offset         = vfo[0].offset;
  ctx->sidetone       = cw_keyer_sidetone_frequency;
  ctx->sample_rate    = rx->sample_rate;
  ctx->mode           = vfo[0].mode;
  ctx->filter_low     = rx->filter_low;
  ctx->filter_high    = rx->filter_high;
  ctx->ref            = div_auto_ref;
  ctx->follow         = div_auto_follow_filter;
  ctx->centre         = div_auto_centre;
  ctx->width          = div_auto_width;
  ctx->weighting      = div_auto_weighting;
}

static int div_context_changed(const struct div_context *a, const struct div_context *b) {
  return a->frequency      != b->frequency      ||
         a->ctun_frequency != b->ctun_frequency ||
         a->offset         != b->offset         ||
         a->sidetone       != b->sidetone       ||
         a->sample_rate    != b->sample_rate    ||
         a->mode           != b->mode           ||
         a->filter_low     != b->filter_low     ||
         a->filter_high    != b->filter_high    ||
         a->ref            != b->ref            ||
         a->follow         != b->follow         ||
         a->centre         != b->centre         ||
         a->width          != b->width          ||
         a->weighting      != b->weighting;
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
    // The carrier bin only. The frequency comes from our own tracker,
    // which runs on the spectrum further down, so this works in any mode
    // with a carrier and its smoothing is under the operator's control.
    //
    // div_carrier_hz always holds a usable value - it starts at zero,
    // the tuned frequency, which is where an AM carrier sits to within
    // the tuning error. It deliberately has no "not valid yet" state:
    // an earlier version returned failure here until the tracker had run
    // once, and since the bin range is computed before the transform and
    // the tracker runs after it, that could never happen. The mode sat on
    // "searching" for ever on a strong, perfectly tuned signal.
    //
    flo = div_carrier_hz - DIV_CARRIER_BINS * binhz;
    fhi = div_carrier_hz + DIV_CARRIER_BINS * binhz;
  } else if (DIV_REF_IS_RADE(ctx->ref)) {
    //
    // The RADE modem occupies 750..2200 Hz of audio, on one side of the
    // tuned carrier or the other. div_rade_side says which, in the
    // shifted frame: -1 below the carrier, +1 above. It comes from the
    // operator's passband, which is the only thing that should decide it
    // - see the note above div_rade_side_expected().
    //
    if (div_rade_side < 0) {
      flo = -RADE_CORR_FHI;
      fhi = -RADE_CORR_FLO;
    } else {
      flo = RADE_CORR_FLO;
      fhi = RADE_CORR_FHI;
    }

    //
    // Never measure outside what the operator is listening to. A 1.8 kHz
    // filter on a 1.45 kHz modem band leaves most of it, and measuring the
    // part they have filtered out would be measuring something they cannot
    // hear the effect of.
    //
    // If the two do not overlap at all the filter is not for this signal;
    // the modem band is then used as it stands rather than returning
    // nothing, so the mode still works while the operator sorts the filter
    // out.
    //
    if (ctx->filter_high > ctx->filter_low) {
      double plo = flo > (double)ctx->filter_low  ? flo : (double)ctx->filter_low;
      double phi = fhi < (double)ctx->filter_high ? fhi : (double)ctx->filter_high;

      if (phi > plo) {
        flo = plo;
        fhi = phi;
      }
    }
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
  // Shifted frame -> bin frequency. This inverts as well as displaces, so
  // the two edges swap. See the note at the top of this file.
  //
  {
    const double a = div_shift_to_bin(ctx, flo);
    const double b = div_shift_to_bin(ctx, fhi);
    flo = (a < b) ? a : b;
    fhi = (a < b) ? b : a;
  }

  //
  // Hold the window inside the first Nyquist zone.
  //
  // The accumulation loops index bins as k % nfft, so a bin outside
  // [-nfft/2, nfft/2) is not an error - it silently becomes a *different*
  // frequency. Before this guard a window edge at +30 kHz on a 48 kHz
  // stream was measured at -18 kHz instead, with nothing to say so, and
  // the spin ranges allowed exactly that.
  //
  // Clamping rather than rejecting keeps a partly-reachable window
  // usable; div_auto_clamped tells the UI it happened.
  //
  const double nyq = 0.5 * (double)ctx->sample_rate - binhz;
  div_auto_clamped = 0;

  if (flo < -nyq) {
    flo = -nyq;
    div_auto_clamped = 1;
  }

  if (fhi > nyq) {
    fhi = nyq;
    div_auto_clamped = 1;
  }

  if (fhi <= flo) {
    //
    // Entirely outside the usable spectrum.
    //
    return 0;
  }

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
  if (div_jump) {
    //
    // The operator asked for a different objective; go straight there so
    // the two can be compared without waiting out the slew.
    //
    div_jump = 0;
    div_cos = wr;
    div_sin = wi;
  } else {
    div_cos += DIV_SLEW_FRAC * (wr - div_cos);
    div_sin += DIV_SLEW_FRAC * (wi - div_sin);
  }
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
    rade_corr_reset();
    lastctx = ctx;
  }

  if (ctx.ref == DIV_REF_RADE_V1) {
    //
    // Pilot-correlating path. This one does not use the FFT at all: it
    // downconverts to the 8 kHz modem rate and correlates against the
    // known RADE V1 pilot, which separates the wanted signal from noise
    // and QRM well enough to estimate the two separately.
    //
    double wr, wi;
    //
    // The operator's sideband, as the pilot bank to search.
    //
    // Bank 0 is the pilot as transmitted, carriers at +750..+2200 Hz in
    // the tapped buffer. The buffer is inverted with respect to RF, so
    // those positive bin frequencies are *below* the dial: bank 0 is the
    // LSB bank and bank 1 the USB one. See the frequency bookkeeping note
    // at the top - this is the mapping the on-air logs give, and it is
    // the opposite of the one reading the code suggests.
    //
    // -1 means the passband straddles the carrier and does not say.
    //
    const int expect = div_rade_side_expected(&ctx);
    const int bank = (expect == 0) ? -1 : (expect < 0 ? 0 : 1);
    int ok = rade_corr_process(work0, work1, nfft, bank,
                               div_frame_off(&ctx), div_auto_tau, &wr, &wi);
    //
    // The overlay follows the passband, locked or not. It used to switch
    // to the bank the correlator reported once it locked, which is how a
    // lock on the wrong side of an LSB passband announced itself: the
    // green box jumped across the carrier at the moment of locking. There
    // is nothing to report any more - the correlator only searches the
    // bank the passband names - and the only case where it still chooses
    // is AM/SAM/FM, where the passband says nothing and the correlator's
    // answer is the only one there is.
    //
    div_rade_side = (expect != 0) ? expect
                    : (rade_corr_locked ? (rade_corr_mirrored ? 1 : -1) : div_rade_side);

    if (ok) {
      div_auto_coherence = rade_corr_quality;
      div_auto_holding = 0;
      div_apply_weight(wr, wi);
    } else {
      div_auto_coherence = rade_corr_quality;
      div_auto_holding = 1;
    }

    return;
  }

  if (!div_bin_range(&ctx, &klo, &khi)) {
    //
    // Nothing worth transforming: an empty or nonsensical window.
    //
    // Note this runs *before* the transform, so nothing computed from the
    // spectrum may be required to make it succeed - see the note in
    // div_bin_range() about the carrier tracker.
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

  if (ctx.ref == DIV_REF_CARRIER) {
    //
    // Find the carrier ourselves rather than asking the SAM PLL.
    //
    // WDSP's SAM PLL is set up for fast acquisition and drift following:
    // omegaN 250 rad/s with unity damping is a 39.8 Hz natural frequency
    // and about 25 Hz of loop noise bandwidth, which on a weak carrier
    // gives several Hz of frequency jitter. That is the right choice for
    // demodulating SAM and the wrong one for measuring a stable carrier,
    // and it cannot be narrowed without spoiling the audio it exists to
    // produce.
    //
    // The spectrum is already in front of us, so the peak bin plus a
    // parabolic interpolation over its neighbours gives a sub-bin
    // estimate, and it can then be smoothed as slowly as the operator
    // likes. It also works in plain AM, where the SAM PLL does not run at
    // all.
    //
    //
    // Search where the operator pointed us, not blindly around the tuned
    // frequency. Parking a 1 kHz window on +5 kHz is what lets a carrier
    // other than the primary be tracked - and nulled - since the primary
    // is then outside the search entirely. The selection has no memory
    // between blocks, so restricting the region is the whole mechanism.
    //
    const double a = div_shift_to_bin(&ctx, ctx.centre - 0.5 * ctx.width);
    const double b = div_shift_to_bin(&ctx, ctx.centre + 0.5 * ctx.width);
    double slo = (a < b) ? a : b;
    double shi = (a < b) ? b : a;
    const double snyq = 0.5 * (double)ctx.sample_rate - binhz;

    if (slo < -snyq) { slo = -snyq; }

    if (shi >  snyq) { shi =  snyq; }

    int klo_s = (int)floor(slo / binhz);
    int khi_s = (int)ceil (shi / binhz);
    int peak = klo_s;
    double peakval = -1.0;

    for (int k = klo_s; k <= khi_s; k++) {
      int idx = k % nfft;

      if (idx < 0) { idx += nfft; }

      double p = (double)fftout0[idx][0] * fftout0[idx][0]
                 + (double)fftout0[idx][1] * fftout0[idx][1];

      if (p > peakval) {
        peakval = p;
        peak = k;
      }
    }

    double delta = 0.0;

    if (peakval > 0.0) {
      //
      // Parabolic interpolation on log power over the three bins about
      // the peak. Good to a small fraction of a bin for a windowed tone.
      //
      double m[3];

      for (int j = 0; j < 3; j++) {
        int idx = (peak - 1 + j) % nfft;

        if (idx < 0) { idx += nfft; }

        double p = (double)fftout0[idx][0] * fftout0[idx][0]
                   + (double)fftout0[idx][1] * fftout0[idx][1];
        m[j] = log(p > 1e-30 ? p : 1e-30);
      }

      double den2 = m[0] - 2.0 * m[1] + m[2];

      if (fabs(den2) > 1e-12) {
        delta = 0.5 * (m[0] - m[2]) / den2;
      }

      if (delta > 0.5) { delta = 0.5; }

      if (delta < -0.5) { delta = -0.5; }
    }

    //
    // Bin frequency back to the shifted frame, which is what the menu,
    // the overlay and div_bin_range() all work in: the inverse of
    // div_shift_to_bin(), which is its own inverse up to the sign.
    //
    double hz = -((double)peak + delta) * binhz - div_frame_off(&ctx);

    if (!div_auto_carrier_valid) {
      //
      // First look after a reset: take it, rather than crawling towards
      // it from the tuned frequency over one averaging time.
      //
      div_carrier_hz = hz;
      div_auto_carrier_valid = 1;
    } else {
      div_carrier_hz += (1.0 - exp(-blocktime / div_auto_tau)) * (hz - div_carrier_hz);
    }

    div_auto_carrier = div_carrier_hz;
    //
    // Re-aim the window now the carrier is known.
    //
    div_bin_range(&ctx, &klo, &khi);
  }

  if (ctx.ref == DIV_REF_RADE_BAND) {
    //
    // Which side of the tuned frequency to measure.
    //
    // The operator's passband decides it, full stop. An earlier version
    // took the stronger of the two sides by energy, which is a coin toss
    // on a RADE signal near the noise floor - and RADE is usually near the
    // noise floor, that being the point of it - so it could sit on the
    // wrong side indefinitely, and the panadapter overlay with it.
    //
    // Measuring the side the operator is not listening to is not a
    // fallback worth having here. Whatever is over there is a different
    // signal, and combining for it would optimise the array for something
    // that is filtered out downstream.
    //
    int want = div_rade_side_expected(&ctx);

    if (want == 0) {
      //
      // AM, SAM, FM: the passband straddles the carrier and says nothing,
      // so the energy is all there is to go on.
      //
      double up = 0.0, dn = 0.0;

      for (int side = 0; side < 2; side++) {
        double lo = side ? -RADE_CORR_FHI : RADE_CORR_FLO;
        double hi = side ? -RADE_CORR_FLO : RADE_CORR_FHI;
        double p = div_shift_to_bin(&ctx, lo);
        double q = div_shift_to_bin(&ctx, hi);
        int a = (int)floor(((p < q) ? p : q) / binhz);
        int b = (int)ceil (((p < q) ? q : p) / binhz);
        double acc = 0.0;

        for (int k = a; k <= b; k++) {
          int idx = k % nfft;

          if (idx < 0) { idx += nfft; }

          acc += (double)fftout0[idx][0] * fftout0[idx][0]
                 + (double)fftout0[idx][1] * fftout0[idx][1];
        }

        if (side) { dn = acc; } else { up = acc; }
      }

      want = (dn > up) ? -1 : 1;
    }

    if (want != div_rade_side) {
      div_rade_side = want;
      div_reset_stats();
      div_bin_range(&ctx, &klo, &khi);
    }
  }

  //
  // Exponential forgetting across blocks, applied per bin.
  //
  double alpha = 1.0 - exp(-blocktime / div_auto_tau);

  if (!acc_valid) {
    alpha = 1.0;
    acc_valid = 1;
  }

  //
  // Per-bin running spectra. Keeping these per bin rather than as four
  // scalars is what allows the bins to be weighted by how well the two
  // antennas agree in each - see below.
  //
  for (int k = klo; k <= khi; k++) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    double i0 = fftout0[idx][0], q0 = fftout0[idx][1];
    double i1 = fftout1[idx][0], q1 = fftout1[idx][1];
    //
    // X0 * conj(X1)
    //
    bin_xy_re[idx] += alpha * ((i0 * i1 + q0 * q1) - bin_xy_re[idx]);
    bin_xy_im[idx] += alpha * ((q0 * i1 - i0 * q1) - bin_xy_im[idx]);
    bin_xx[idx]    += alpha * ((i0 * i0 + q0 * q0) - bin_xx[idx]);
    bin_yy[idx]    += alpha * ((i1 * i1 + q1 * q1) - bin_yy[idx]);
  }

  //
  // Combine the bins.
  //
  // Flat reproduces the original behaviour: sum everything and divide,
  // which is a power-weighted average of h(f). It is dominated by the
  // loudest bins whether or not the antennas agree there, and noise-only
  // bins dilute it by adding to the denominator but not the numerator.
  //
  // Coherence weights each bin by its own magnitude-squared coherence, so
  // bins carrying a signal both antennas hear dominate and noise-only
  // bins fall out. That is what makes a wide window work on SSB voice,
  // where the energy moves about constantly and there is no carrier to
  // sit on: the window can span the whole passband and the estimator
  // picks the bins worth using, following the voice as it moves.
  //
  acc_xy_re = acc_xy_im = acc_xx = acc_yy = 0.0;
  double wsum = 0.0;

  for (int k = klo; k <= khi; k++) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    double xx = bin_xx[idx], yy = bin_yy[idx];
    double w = 1.0;

    if (ctx.weighting == DIV_WEIGHT_COHERENCE) {
      double den = xx * yy;

      if (den <= 0.0) { continue; }

      double g2 = (bin_xy_re[idx] * bin_xy_re[idx]
                   + bin_xy_im[idx] * bin_xy_im[idx]) / den;

      if (g2 > 1.0) { g2 = 1.0; }

      w = g2;

      if (w <= 0.0) { continue; }
    }

    acc_xy_re += w * bin_xy_re[idx];
    acc_xy_im += w * bin_xy_im[idx];
    acc_xx    += w * xx;
    acc_yy    += w * yy;
    wsum      += w;
  }

  if (acc_xx <= 0.0 || acc_yy <= 0.0 || wsum <= 0.0) {
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

    while (q_count == 0 && !mbox_quit) {
      g_cond_wait(&mbox_cond, &mbox_mutex);
    }

    if (mbox_quit) {
      g_mutex_unlock(&mbox_mutex);
      break;
    }

    work0 = qbuf0[q_tail];
    work1 = qbuf1[q_tail];
    int dropped = q_dropped;
    q_dropped = 0;
    g_mutex_unlock(&mbox_mutex);

    if (reset_requested) {
      reset_requested = 0;
      rade_corr_reset();
    }

    if (dropped > 0) {
      //
      // The sample stream has a hole in it. Everything the correlator
      // knows about where the pilot is refers to a clock that has just
      // skipped, so start again rather than track something that has
      // moved.
      //
      t_print("%s: dropped %d analysis block(s), re-acquiring\n", __func__, dropped);
      rade_corr_reset();
    }

    div_process_block();
    g_mutex_lock(&mbox_mutex);
    q_count--;
    q_tail = (q_tail + 1) % DIV_QUEUE;
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

  //
  // One slot is always reserved for filling, so the most that can be
  // waiting is DIV_QUEUE-1 and the head never collides with the tail.
  //
  if (q_count < DIV_QUEUE - 1) {
    q_count++;
    q_head = (q_head + 1) % DIV_QUEUE;
    g_cond_signal(&mbox_cond);
  } else {
    q_dropped++;
  }

  fill0 = qbuf0[q_head];
  fill1 = qbuf1[q_head];
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

  nfft = div_choose_nfft(receiver[0]->sample_rate, div_auto_resolution);
  binhz = (double)receiver[0]->sample_rate / (double)nfft;
  div_auto_binhz = binhz;
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
    bin_xy_re = g_new0(double, DIV_MAX_NFFT);
    bin_xy_im = g_new0(double, DIV_MAX_NFFT);
    bin_xx    = g_new0(double, DIV_MAX_NFFT);
    bin_yy    = g_new0(double, DIV_MAX_NFFT);
    for (int i = 0; i < DIV_QUEUE; i++) {
      qbuf0[i] = g_new0(float, 2 * DIV_MAX_NFFT);
      qbuf1[i] = g_new0(float, 2 * DIV_MAX_NFFT);
    }
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
  //
  // Under the mutex: a sample-path call that was still in flight when the
  // previous stop cleared div_auto_running could otherwise enqueue a
  // stale block after these were reset.
  //
  g_mutex_lock(&mbox_mutex);
  fillptr = 0;
  q_head = q_tail = q_count = 0;
  q_dropped = 0;
  reset_requested = 0;
  mbox_quit = 0;
  fill0 = qbuf0[0];
  fill1 = qbuf1[0];
  g_mutex_unlock(&mbox_mutex);
  div_reset_stats();
  div_get_context(&lastctx);
  t_print("%s: nfft=%d bin=%0.2f Hz block=%0.1f ms rate=%d\n", __func__,
          nfft, binhz, 1000.0 * blocktime, receiver[0]->sample_rate);
  if (div_auto_ref == DIV_REF_RADE_V1) {
    if (!rade_corr_start(receiver[0]->sample_rate)) {
      //
      // The correlator needs a DDC rate that is a whole multiple of the
      // 8 kHz modem rate. Every rate piHPSDR offers satisfies that, but
      // fall back to the wideband RADE window rather than silently doing
      // nothing if that ever stops being true.
      //
      t_print("%s: falling back to DIV_REF_RADE_BAND\n", __func__);
      div_auto_ref = DIV_REF_RADE_BAND;
    }
  }

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

  rade_corr_stop();
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
  SetPropI0("diversity_auto_weighting",      div_auto_weighting);
  SetPropF0("diversity_auto_resolution",     div_auto_resolution);
  SetPropF0("diversity_band_centre",         div_band_centre);
  SetPropF0("diversity_band_width",          div_band_width);
  SetPropF0("diversity_carrier_centre",      div_carrier_centre);
  SetPropF0("diversity_carrier_width",       div_carrier_width);
}

void diversity_auto_restore_state(void) {
  GetPropI0("diversity_auto_mode",           div_auto_mode);
  GetPropI0("diversity_auto_ref",            div_auto_ref);
  GetPropI0("diversity_auto_follow_filter",  div_auto_follow_filter);
  GetPropF0("diversity_auto_centre",         div_auto_centre);
  GetPropF0("diversity_auto_width",          div_auto_width);
  GetPropF0("diversity_auto_tau",            div_auto_tau);
  GetPropF0("diversity_auto_coherence_min",  div_auto_coherence_min);
  GetPropI0("diversity_auto_weighting",      div_auto_weighting);
  GetPropF0("diversity_auto_resolution",     div_auto_resolution);
  GetPropF0("diversity_band_centre",         div_band_centre);
  GetPropF0("diversity_band_width",          div_band_width);
  GetPropF0("diversity_carrier_centre",      div_carrier_centre);
  GetPropF0("diversity_carrier_width",       div_carrier_width);

  //
  // Validate everything that came out of the file, not just the two that
  // happened to get clamped first. A props file can be hand-edited or
  // written by a future version, and an out-of-range value here is hard
  // to diagnose from the UI: a bad reference shows a blank combo, and a
  // coherence threshold above 1.0 wedges the loop in permanent HOLD with
  // nothing on screen to say why.
  //
  if (div_auto_mode < DIV_AUTO_OFF || div_auto_mode > DIV_AUTO_SUM) {
    div_auto_mode = DIV_AUTO_OFF;
  }

  if (div_auto_ref < DIV_REF_BAND || div_auto_ref > DIV_REF_RADE_V1) {
    div_auto_ref = DIV_REF_BAND;
  }

  div_auto_follow_filter = div_auto_follow_filter ? 1 : 0;

  //
  // 0.2, not 0.1, to match the slider's minimum.
  //
  if (div_auto_tau < 0.2) { div_auto_tau = 0.2; }

  if (div_auto_tau > 30.0) { div_auto_tau = 30.0; }

  if (div_auto_weighting < DIV_WEIGHT_FLAT || div_auto_weighting > DIV_WEIGHT_COHERENCE) {
    div_auto_weighting = DIV_WEIGHT_COHERENCE;
  }

  if (div_auto_resolution < 3.0)  { div_auto_resolution = 3.0; }

  if (div_auto_resolution > 12.0) { div_auto_resolution = 12.0; }

  if (div_band_width < 20.0)       { div_band_width = 20.0; }

  if (div_band_width > 40000.0)    { div_band_width = 40000.0; }

  if (div_band_centre < -400000.0) { div_band_centre = -400000.0; }

  if (div_band_centre >  400000.0) { div_band_centre =  400000.0; }

  if (div_carrier_width < 20.0)    { div_carrier_width = 20.0; }

  if (div_carrier_width > 40000.0) { div_carrier_width = 40000.0; }

  if (div_carrier_centre < -400000.0) { div_carrier_centre = -400000.0; }

  if (div_carrier_centre >  400000.0) { div_carrier_centre =  400000.0; }

  //
  // 20.0, not 10.0: the spin button's minimum is 20, so a restored value
  // below it was silently snapped up the first time the menu was opened.
  //
  if (div_auto_width < 20.0) { div_auto_width = 20.0; }

  if (div_auto_width > 40000.0) { div_auto_width = 40000.0; }

  //
  // Deliberately generous: the window is allowed outside the passband, and
  // how far is a function of the sample rate, so div_bin_range() does the
  // real limiting against the Nyquist frequency at the rate in use.
  //
  if (div_auto_centre < -400000.0) { div_auto_centre = -400000.0; }

  if (div_auto_centre >  400000.0) { div_auto_centre =  400000.0; }

  if (div_auto_coherence_min < 0.0) { div_auto_coherence_min = 0.0; }

  if (div_auto_coherence_min > 0.95) { div_auto_coherence_min = 0.95; }
}
