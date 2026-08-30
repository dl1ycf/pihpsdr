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

#ifdef DIVERSITY_CAPTURE
  //
  // DEVELOPMENT TOOL - remove with the rest of the capture instrument.
  // See src/diversity_capture.h and test/diversity/devtools/README.md.
  //
  #include "diversity_capture.h"
  //
  // Blocks the sample path lost immediately ahead of the one about to be
  // processed. The worker knows it; the capture hook, further down, needs
  // it to mark the discontinuity in the file.
  //
  static int divcap_dropped = 0;
#endif

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
// How far div_mvdr2()'s denominator may cancel before the solve is called
// degenerate. A relative test, not an absolute one - see the note there.
// 1e-9 is seven orders of magnitude clear of double precision and still
// rejects a genuinely singular pair.
//
#define DIV_MVDR_EPS        1e-9

//
// DIV_AUTO_BEST: how much better one antenna must measure before the
// selection moves to it, and how fast the per-arm noise floor is allowed
// to creep up when the reference has no noise bins of its own and has to
// track a minimum over time instead.
//
// The hysteresis matters most where it matters least: two antennas within
// a decibel of each other are the case where the choice does not matter
// and the case where an ungated comparison would chatter between them.
//
// The floor rise is slow deliberately. It only has to outrun a change of
// band conditions, and anything faster starts following the signal it is
// supposed to be measuring underneath.
//
#define DIV_BEST_HYST_DB    1.0
#define DIV_FLOOR_RISE_DB   0.2     // dB per second

//
// The floor is tracked on power smoothed over this, not over the
// operator's averaging time.
//
// That distinction is the whole mechanism. Averaging is seconds to tens
// of seconds, longer than the gap between two overs and far longer than
// the gap between two syllables, so a minimum taken over the averaged
// power never sees a gap at all - it lands on a moment that still holds
// signal, on both arms, in the same ratio as the signal itself, and the
// estimate cancels to exactly 0.0 dB. Measured doing precisely that on
// the voice captures before this was separated out.
//
#define DIV_FLOOR_TAU       0.5     // seconds

//
// How far the window power must stand above the tracked floor, on both
// arms, before that floor is taken to be noise.
//
// Without this the tracker answers confidently and wrongly. Its minimum
// is only a noise floor if the capture contained a moment with no signal
// in it; where the signal never stops, the minimum is signal too, and
// since both arms carry the same signal scaled by the same path the two
// minima are in the same ratio as the two powers. Everything then
// cancels and the estimate comes out at exactly 0.0 dB - not "the arms
// are equal" but "this method has told you nothing". Simulated and then
// confirmed on the 60 m RADE captures, which have no gaps in them at all
// and where it read +0.0 dB against a truth of +2.5.
//
// Six decibels is enough to say the floor was set under conditions
// genuinely different from now.
//
#define DIV_ARM_MIN_DB      6.0

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
// Digital I/Q occupancy detection.
//
// How far a bin has to stand above the noise floor of the search region
// to count as occupied. 6 dB is deliberately low: the split only has to
// be good enough to keep the signal out of the noise covariance, and a
// bin wrongly called noise costs far more than one wrongly called
// signal - it puts the wanted signal into R and steers the null onto it.
//
#define DIV_OCC_DB          6.0

//
// The floor is the median of the bin powers over the region, which is
// robust to a signal filling a good part of it. Sorting is O(n log n) per
// block, so the number of bins that go into the estimate is capped and a
// wider region is sampled by striding rather than by sorting more. 4096
// bins is far more than the estimate needs and costs well under a
// millisecond.
//
#define DIV_OCC_MAX_SAMPLES 4096

//
// Fewer occupied bins than this and there is nothing worth calling a
// signal, whatever the coherence says.
//
#define DIV_OCC_MIN_BINS    3

//
// How far this block's power in the bins being measured may fall below
// the smoothed power accumulated over them before the statistics are
// declared stale and the loop holds.
//
// This is what notices a transmission ending, and without it nothing
// does. The accumulators forget exponentially, so Sxy, Sxx and Syy decay
// together and the coherence gate sees gamma^2 stay near 1 the whole way
// down; the Digital I/Q occupancy test is a ratio against the median
// floor and is scale invariant, so it does not see the level collapse at
// all. Left to the forgetting factor alone a 30 dB signal at tau = 2 s
// keeps the loop "tracking" for 5.8 tau - about twelve seconds of walking
// the weight around on noise, once per gap.
//
// Comparing the two answers "is the thing these statistics describe still
// there", which is the question that actually matters, and it scales
// itself: it fires on a signal well out of the noise, which is exactly
// the case where stale statistics do the most harm, and stays quiet on a
// weak one, where they contain little signal to be stale about.
//
// It matters most on a keyed mode. CW is the extreme case - the signal is
// absent for most of a transmission, not just between them - and the loop
// previously spent every key-up period adjusting the weight on noise. It
// now measures only while there is something to measure.
//
// 10 dB is comfortably past ordinary fading and far short of a signal
// stopping. Holding through a deep fade is wanted anyway.
//
#define DIV_STALE_DB        10.0

//
// Bins either side of an occupied one that are excluded from the noise
// covariance as well as from the signal.
//
// Without this the mode cancels the signal it is trying to receive. A
// strong signal spreads past its own bins - the analysis window's skirts
// are 92 dB down but a signal 40 dB above the noise still puts more into
// its neighbours than the noise floor holds - and those bins are
// correlated between the arms with the signal's own channel. Feeding them
// to R tells MVDR that the direction the signal arrives from is
// interference, and it dutifully steers the null onto it.
//
// This is the standard failure of MVDR trained on data containing the
// desired signal, and a guard band is the standard answer to it. Four
// bins is where the Blackman-Harris skirts have gone; the cost is four
// bins of noise estimate either side of the signal, which a mostly empty
// passband has plenty of.
//
#define DIV_OCC_GUARD       4

//
// How far the receiver may be retuned before the accumulated statistics
// are thrown away.
//
// This was an exact comparison, so every click of the tuning knob - one
// hertz on a fine step - discarded the channel estimate, the covariance
// and the correlator's lock. Measured on a recorded capture of an
// operator tuning around: 23 resets over 30 analysis blocks, a median of
// one block between them, against the 31 blocks the operator's averaging
// time asks for. The mode spends the whole of a tune permanently in the
// first block or two of an estimate that needs thirty-one, and stays
// there for an averaging time after the knob stops.
//
// The antenna-to-antenna transfer h1/h0 is a property of the two antennas
// and the path. It does not change because the dial moved a hertz. What a
// retune changes is *which* signal is in the window, and 20 Hz is far too
// little to change that: it is under a tenth of the narrowest CW filter,
// and well inside the +/-60 Hz the RADE correlator tracks, so a lock
// survives it. Tuning across a band to a different station moves
// kilohertz and still resets.
//
// Cumulative, not per block: the comparison below is against the context
// as it was at the last reset, so twenty single-hertz steps count as
// twenty hertz. Comparing against the previous block would never fire at
// all, which would be worse than resetting too often.
//
#define DIV_RETUNE_HZ       20


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
double div_auto_hang           = 10.0;
double div_auto_coherence_min  = 0.30;
int    div_auto_weighting      = DIV_WEIGHT_COHERENCE;
double div_auto_resolution     = DIV_TARGET_BIN_HZ;

//
// The window controls are modal: DIV_REF_BAND, DIV_REF_CARRIER and
// DIV_REF_DIGITAL_IQ each keep their own centre and width, so moving
// between them does not destroy the others' settings. div_auto_centre and
// div_auto_width always hold the pair for whichever reference is
// selected; these hold the pairs for the rest.
//
double div_band_centre         = 0.0;
double div_band_width          = 1000.0;
double div_carrier_centre      = 0.0;
double div_carrier_width       = 1000.0;
//
// The digital default is the whole SSB audio passband rather than a
// narrow slice: occupancy narrows it from there, so the operator does not
// have to know how wide the signal is. It is only reachable at all with
// the follow tick cleared.
//
double div_digital_centre      = 0.0;
double div_digital_width       = 2600.0;

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

double div_auto_arm_db         = 0.0;
int    div_auto_arm_valid      = 0;
int    div_auto_arm_pick       = 0;

//
// Per-arm noise floors for the references that have no noise bins to
// measure one in. See div_arm_floor_update().
//
static double arm_floor0 = 0.0, arm_floor1 = 0.0;
static int    arm_floor_valid = 0;
static double arm_pw0 = 0.0, arm_pw1 = 0.0;
static double arm_fast0 = 0.0, arm_fast1 = 0.0;

double div_auto_occ_lo         = 0.0;
double div_auto_occ_hi         = 0.0;
int    div_auto_occ_valid      = 0;

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
//
// A gap in the sample stream is recorded against the slot that follows
// it, not in one counter read at dequeue time.
//
// A drop can only happen when the queue is full, which means DIV_QUEUE-1
// blocks from *before* the gap are still waiting. Reading a global
// counter at dequeue therefore reset the correlator three blocks early:
// it re-acquired on pre-gap data and then tracked straight through the
// discontinuity with nothing to tell it, which is exactly the failure -
// the pilot slides by a non-multiple of the modem frame and the lock dies
// a few seconds later - that this mechanism exists to prevent.
//
static int             q_pending_drop = 0;   // dropped since the last enqueue
static int             q_gap[DIV_QUEUE];     // gap ahead of each queued slot

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
// Scratch for the Digital I/Q noise-floor median. Sized at
// DIV_OCC_MAX_SAMPLES rather than DIV_MAX_NFFT because the estimate is
// strided down to that many bins however wide the region is.
//
static double         *occ_scratch = NULL;

//
// Which bins were found occupied, by wrapped index, so the noise pass can
// keep its distance from them. See DIV_OCC_GUARD.
//
static unsigned char  *occ_mask = NULL;

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
// when below. Written by DIV_REF_RADE_V1 on every block, from the
// operator's passband, and read by the menu and the panadapter overlay.
// The other references leave it alone.
//
static int div_rade_side = 1;

//
// Set when the next weight update should be applied without slewing.
//
static int div_jump = 0;

//
// Operator hold. The analysis carries on; only the application of its
// answer is suspended. Not persisted - it is an operating state, not a
// setting, and coming back up held would be baffling.
//
int    div_auto_hold = 0;
double div_track_gain = 0.0;
double div_track_phase = 0.0;

//
// Smoothed carrier frequency, shifted frame, for DIV_REF_CARRIER.
//
static double div_carrier_hz = 0.0;


//
// Swap Null for Sum, or the other way about.
//
// The two are the same measurement with the sign of the answer and the
// normalising power exchanged, so they land essentially 180 degrees
// apart. Setting div_jump alone was not enough: it only takes effect when
// the loop next produces a weight, and it may not be producing one - the
// coherence gate can be holding, the RADE correlator can be frozen on a
// fade, and under operator Hold nothing is applied at all. The control
// then changed which answer was being computed while leaving the audio
// exactly as it was, which is not what "invert" means to anyone.
//
// So the weight in force is turned through 180 degrees here and now,
// whatever the loop is doing, and div_jump is set so that when the loop
// does have something it goes straight there rather than slewing.
//
// Under Hold this acts on the operator's own manual weight, which is the
// only thing being applied then, and is exactly what is wanted.
//
void diversity_auto_invert(void) {
  div_cos = -div_cos;
  div_sin = -div_sin;
  div_phase += 180.0;

  while (div_phase >  180.0) { div_phase -= 360.0; }

  while (div_phase < -180.0) { div_phase += 360.0; }

  //
  // The magnitude is unchanged, so div_gain is left alone.
  //
  div_jump = 1;
}

void diversity_auto_set_hold(int on) {
  on = on ? 1 : 0;

  if (on == div_auto_hold) { return; }

  div_auto_hold = on;

  if (!on) {
    //
    // Apply what the loop has tracked to meanwhile, in one step. Done by
    // the analysis thread on its next block rather than here, so the
    // weight is only ever written from one place.
    //
    div_jump = 1;
  }
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
  arm_floor_valid = 0;
  arm_floor0 = arm_floor1 = 0.0;
  arm_pw0 = arm_pw1 = 0.0;
  arm_fast0 = arm_fast1 = 0.0;
  div_auto_arm_valid = 0;
  div_auto_arm_db = 0.0;
  div_auto_coherence = 0.0;
  div_auto_holding = 1;
  div_carrier_hz = 0.0;
  div_auto_carrier_valid = 0;
  div_auto_occ_valid = 0;
  div_auto_occ_lo = 0.0;
  div_auto_occ_hi = 0.0;
  //
  // Start the tracked readout from what is actually applied, so it does
  // not claim 0 dB / 0 degrees before the loop has produced anything.
  //
  div_track_gain = div_gain;
  div_track_phase = div_phase;
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

//
// b is the context as it stood at the last reset, not the previous
// block - div_process_block() only writes lastctx when it resets - so the
// three frequency comparisons below are against where the estimate was
// actually made. See DIV_RETUNE_HZ.
//
static int div_context_changed(const struct div_context *a, const struct div_context *b) {
  return llabs(a->frequency      - b->frequency)      > DIV_RETUNE_HZ ||
         llabs(a->ctun_frequency - b->ctun_frequency) > DIV_RETUNE_HZ ||
         llabs(a->offset         - b->offset)         > DIV_RETUNE_HZ ||
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
  } else if (ctx->ref == DIV_REF_DIGITAL_IQ) {
    //
    // The *search region*, not the bins finally accumulated. Occupancy
    // narrows it after the transform - see div_digital_solve().
    //
    // Nothing computed from the spectrum may appear here: this runs
    // before the transform, and making the bin range depend on something
    // only the transform can supply is exactly what left the carrier
    // reference sitting on "searching" for ever. The region therefore
    // starts from the filter or from the operator's own numbers, both of
    // which are always available.
    //
    // Following the filter is the default and wants no sideband table:
    // the passband is already on the right side of the tuned frequency in
    // every mode, DIGU/DIGL and CW included, and under CTUN too. That is
    // why there is no +/-1500 Hz constant anywhere in this mode.
    //
    if (ctx->follow) {
      flo = ctx->filter_low;
      fhi = ctx->filter_high;
    } else {
      flo = ctx->centre - 0.5 * ctx->width;
      fhi = ctx->centre + 0.5 * ctx->width;
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
// MVDR for a two-element array.
//
// For R = [[r00, r01], [conj(r01), r11]] and h = [h0, h1], the weight
// vector g = R^-1 h is
//
//   g0 = (r11*h0 - r01*h1)       / det
//   g1 = (r00*h1 - conj(r01)*h0) / det
//
// The combiner forms y = z0 + w*z1, which is g^H z with arm 0 normalised
// to unity, so the weight it wants is conj(g1/g0) - and det cancels.
//
// With R diagonal and equal - two arms carrying the same, uncorrelated
// noise - this reduces to conj(h1/h0), which is exactly the maximum ratio
// combining answer the wideband "Sum" mode computes as +Sxy/Sxx. So the
// mode degenerates to the older one whenever there is no correlated
// interference to null, which is both the right behaviour and the easiest
// property to test.
//
// Shared with the RADE V1 correlator, which arrives at the same two
// matrices from pilot correlations rather than from spectral occupancy.
//
void div_mvdr2(double r00, double r11, double r01re, double r01im,
               double h0re, double h0im, double h1re, double h1im,
               double *wr, double *wi) {
  //
  // Diagonal loading. Without it a nearly singular covariance - two arms
  // seeing almost identical noise - produces an enormous weight out of
  // what is mostly estimation error.
  //
  const double load = 0.01 * (r00 + r11) + 1e-20;
  r00 += load;
  r11 += load;
  //
  // num = r00*h1 - conj(r01)*h0,  den = r11*h0 - r01*h1
  //
  const double numre = r00 * h1re - (r01re * h0re + r01im * h0im);
  const double numim = r00 * h1im - (r01re * h0im - r01im * h0re);
  const double denre = r11 * h0re - (r01re * h1re - r01im * h1im);
  const double denim = r11 * h0im - (r01re * h1im + r01im * h1re);
  const double d2 = denre * denre + denim * denim;
  //
  // Reject a degenerate solve, and only that.
  //
  // This used to read "d2 > 1e-30", which is an absolute magnitude test
  // on a quantity that has no fixed magnitude. den is a difference of two
  // products of energies, so on the RADE path it goes as the eighth power
  // of the sample level: measured across the recorded captures it lands
  // anywhere between 1e-28 and 1e-34 with nothing wrong with any of them.
  // The test fired on between half and all of the frames of every capture
  // but the loudest, returned a weight of exactly zero - which mutes the
  // second antenna and shows in the menu as the -27 dB floor with phase 0,
  // indistinguishable from a real answer - and cost up to 2.0 dB against
  // simply using the better antenna. See Finding 11 in
  // docs/diversity-measurements.md.
  //
  // What makes the answer meaningless is not that den is small but that
  // it is small *compared with the two terms it is the difference of*,
  // which is the catastrophic-cancellation condition and is scale-free.
  // DIV_MVDR_EPS is far above the point where double precision runs out,
  // so this now fires only on a covariance that really is singular
  // against the channel - and the diagonal loading above has already made
  // that very difficult to arrange.
  //
  const double h0m  = sqrt(h0re * h0re + h0im * h0im);
  const double h1m  = sqrt(h1re * h1re + h1im * h1im);
  const double r01m = sqrt(r01re * r01re + r01im * r01im);
  const double scale = r11 * h0m + r01m * h1m;

  if (!(scale > 0.0) || !(d2 > DIV_MVDR_EPS * DIV_MVDR_EPS * scale * scale)) {
    *wr = 0.0;
    *wi = 0.0;
    return;
  }

  //
  // num/den, then conjugated for the g^H combining sense.
  //
  const double qre = (numre * denre + numim * denim) / d2;
  const double qim = (numim * denre - numre * denim) / d2;
  *wr =  qre;
  *wi = -qim;
}

//
// ------------------------------------------------------------------
// Which antenna is better
// ------------------------------------------------------------------
//
// Every reference can say something about the two arms separately, and
// what it needs to say it is the same in each case: the signal power on
// each arm, and the noise power on each arm. The advantage of arm 1 is
// then (S1/N1)/(S0/N0), and where the reference measures the channel
// ratio rather than the two signal powers - which all of them do - that
// is |h1/h0|^2 * (N0/N1).
//
// The RADE V1 and Digital I/Q references already have both halves: their
// MVDR covariance is a measurement of N0 and N1 taken off the signal. The
// wideband Window and Carrier references have no such thing, so they get
// a noise floor tracked over time instead - see div_arm_floor_update().
//
// This is worth publishing whatever objective is running. Nothing an
// operator can otherwise see separates an antenna that reads 12 dB down
// because it is deaf from one that reads 12 dB down because it is quiet,
// and the two want opposite weights - which is exactly the case the 60 m
// captures turned up. See Finding 13 in docs/diversity-measurements.md.
//
static void div_arm_publish(int valid, double db) {
  div_auto_arm_valid = valid;

  if (valid) { div_auto_arm_db = db; }
}

//
// Minimum statistics: the noise floor of a channel is the quietest it has
// recently been. Track the smoothed in-window power down instantly and
// let it creep back up slowly, so a gap between overs sets it and a long
// transmission does not drag it along.
//
// Crude next to a covariance measured off the carriers, and the only
// thing available to a reference whose window is the whole passband: the
// bins outside it are the rejected sideband, and Finding 1 is the record
// of what happens when that is used as a noise reference.
//
static void div_arm_floor_update(double p0, double p1) {
  if (!(p0 > 0.0) || !(p1 > 0.0)) { return; }

  if (!arm_floor_valid) {
    arm_floor0 = p0;
    arm_floor1 = p1;
    arm_floor_valid = 1;
    return;
  }

  const double rise = pow(10.0, 0.1 * DIV_FLOOR_RISE_DB * blocktime);
  arm_floor0 = (p0 < arm_floor0) ? p0 : arm_floor0 * rise;
  arm_floor1 = (p1 < arm_floor1) ? p1 : arm_floor1 * rise;
}

//
// The advantage of arm 1, in dB, from the tracked floors. Fails while
// the floor has not been established, and while either arm is sitting on
// its own floor - there is no signal to compare then, and the ratio of
// two noises is not an answer to the question.
//
static int div_arm_from_floor(double p0, double p1, double *db) {
  if (!arm_floor_valid || arm_floor0 <= 0.0 || arm_floor1 <= 0.0) { return 0; }

  const double s0 = p0 - arm_floor0;
  const double s1 = p1 - arm_floor1;

  if (!(s0 > 0.0) || !(s1 > 0.0)) { return 0; }

  //
  // Both arms have to stand clear of their own floor, or the floor is not
  // yet known to be noise. See DIV_ARM_MIN_DB.
  //
  const double need = pow(10.0, 0.1 * DIV_ARM_MIN_DB) - 1.0;

  if (s0 < need * arm_floor0 || s1 < need * arm_floor1) { return 0; }

  *db = 10.0 * log10((s1 / arm_floor1) / (s0 / arm_floor0));
  return 1;
}

//
// Write a new weight, rate limited. Called from the analysis thread.
//
static void div_apply_weight(double wr, double wi);

//
// DIV_AUTO_BEST: give the output to whichever antenna is measuring
// better.
//
// Not a switch, because the combiner cannot express one. It forms
// z0 + w*z1 with arm 0 pinned at unity gain (see receiver.c), so "use
// arm 1 only" exists only as the limit w -> infinity, and the nearest
// reachable point is w at the clamp with the co-phasing angle - arm 1
// dominant with arm 0 co-phased in underneath it, 20 dB down. That is not
// a compromise forced on us: measured against a decoder it beat the full
// MVDR solve by 0.6 dB on the one capture where the two antennas
// disagreed about which was better, because the residue of arm 0 is still
// doing useful combining. Selecting arm 0 needs no such trick - w = 0 is
// exact.
//
// cophase_re/im only has to point the right way; its magnitude is thrown
// away. Every reference already computes it, as the Sum weight.
//
static void div_apply_best(double cophase_re, double cophase_im) {
  if (!div_auto_arm_valid) {
    //
    // Nothing to choose on. Hold rather than guess - and in particular do
    // not fall back to arm 0, which would silently turn the mode into
    // "diversity off" whenever the estimate was unavailable.
    //
    div_auto_holding = 1;
    return;
  }

  if (div_auto_arm_pick == 0) {
    if (div_auto_arm_db >  DIV_BEST_HYST_DB) { div_auto_arm_pick = 1; }
  } else {
    if (div_auto_arm_db < -DIV_BEST_HYST_DB) { div_auto_arm_pick = 0; }
  }

  if (div_auto_arm_pick == 0) {
    div_auto_holding = 0;
    div_apply_weight(0.0, 0.0);
    return;
  }

  const double m = sqrt(cophase_re * cophase_re + cophase_im * cophase_im);

  if (!(m > 0.0)) {
    div_auto_holding = 1;
    return;
  }

  const double k = DIV_MAX_WEIGHT / m;
  div_auto_holding = 0;
  div_apply_weight(cophase_re * k, cophase_im * k);
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
  // Where the loop has got to, in the units the operator reads. Kept
  // separately from div_gain/div_phase, which describe what is actually
  // being applied to the samples: under Hold the two diverge, and being
  // able to see the tracked answer while the manual controls hold a
  // different one is the whole point of the control.
  //
  div_track_gain = (mag > 1.0e-9) ? 20.0 * log10(mag) : -27.0;

  if (div_track_gain >  27.0) { div_track_gain =  27.0; }

  if (div_track_gain < -27.0) { div_track_gain = -27.0; }

  div_track_phase = atan2(wi, wr) * (180.0 / M_PI);

  if (div_auto_hold) {
    //
    // Hold: keep measuring, stop applying. The operator has the gain and
    // phase controls meanwhile, and releasing sets div_jump so the next
    // block puts the tracked answer in place in one step rather than
    // slewing to it from wherever they left it.
    //
    return;
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

static int div_occ_cmp(const void *a, const void *b) {
  const double x = *(const double *)a;
  const double y = *(const double *)b;
  return (x > y) - (x < y);
}

//
// Digital I/Q: split the search region into signal and noise by spectral
// occupancy, then solve.
//
// The wideband references treat every bin in the window the same way, or
// weight it by its own coherence. Neither of them ever forms a picture of
// the *noise* on its own, so "Sum" has to assume the two branches carry
// equal, uncorrelated noise - which is what makes w = +Sxy/Sxx maximum
// ratio combining. On a real station that assumption is usually false:
// ADC1 is often a small loop or an active whip on a bare rear-panel
// input, and much of what both antennas hear is common-mode noise picked
// up on the feedlines, which is correlated between them.
//
// A digital signal is narrow and sits in a passband that is mostly empty,
// so here the noise can simply be looked at directly: the bins that carry
// no signal are the noise, and the covariance measured over them is what
// MVDR needs. w = R^-1 h then whitens against both an unequal branch
// noise level and a correlated one, and degenerates exactly to +Sxy/Sxx
// when the noise really is equal and uncorrelated.
//
// What this does *not* do is separate a wanted signal from co-channel QRM
// inside the same region: both are occupied and both are correlated
// between the arms, so occupancy cannot tell them apart. That is what the
// RADE V1 pilot is for. Here the operator separates them by placing the
// region, and nulls with the Null objective, exactly as in Window mode.
//
static void div_digital_solve(const struct div_context *ctx, int klo, int khi) {
  const int n = khi - klo + 1;

  //
  // The noise floor, as the median of the bin powers over the region.
  //
  // A median rather than a mean because a signal filling a good part of
  // the region would drag a mean up with it and hide itself. Sorting is
  // the only per-block cost this mode adds over the wideband ones, so the
  // sample count is capped and a wider region is strided down to it
  // rather than sorted in full.
  //
  const int stride = (n > DIV_OCC_MAX_SAMPLES) ? (n / DIV_OCC_MAX_SAMPLES + 1) : 1;
  int ns = 0;

  for (int k = klo; k <= khi && ns < DIV_OCC_MAX_SAMPLES; k += stride) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    occ_scratch[ns++] = bin_xx[idx] + bin_yy[idx];
  }

  if (ns < DIV_OCC_MIN_BINS) {
    div_auto_occ_valid = 0;
    div_auto_coherence = 0.0;
    div_auto_holding = 1;
    return;
  }

  qsort(occ_scratch, ns, sizeof(double), div_occ_cmp);
  const double floorp = occ_scratch[ns / 2];
  const double thresh = floorp * pow(10.0, DIV_OCC_DB / 10.0);
  //
  // Signal: above the floor *and* coherent between the arms.
  // Noise:  below the floor, whether or not it is coherent - correlated
  //         noise is precisely what R exists to describe, so it must not
  //         be excluded for being correlated.
  //
  // A bin that is loud but incoherent - a burst on one antenna only -
  // belongs to neither. Putting it in R would describe noise the arms do
  // not actually share; calling it signal would aim the array at it.
  //
  double sig_xy_re = 0.0, sig_xy_im = 0.0, sig_xx = 0.0, sig_yy = 0.0;
  double r01re = 0.0, r01im = 0.0, r00 = 0.0, r11 = 0.0;
  int nsig = 0, nnoise = 0;
  int kmin = 0, kmax = 0;
  //
  // This block's power in the signal bins against the smoothed power
  // that selected them. See DIV_STALE_DB.
  //
  double cur_sig = 0.0, acc_sig = 0.0;
  memset(occ_mask, 0, (size_t)nfft);

  //
  // First pass: which bins carry signal, and the channel over them.
  //
  for (int k = klo; k <= khi; k++) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    const double xx = bin_xx[idx], yy = bin_yy[idx];
    const double xyre = bin_xy_re[idx], xyim = bin_xy_im[idx];

    if (xx + yy > thresh) {
      const double den = xx * yy;

      if (den <= 0.0) { continue; }

      double g2 = (xyre * xyre + xyim * xyim) / den;

      if (g2 > 1.0) { g2 = 1.0; }

      if (g2 < div_auto_coherence_min) { continue; }

      if (nsig == 0) { kmin = kmax = k; }

      if (k < kmin) { kmin = k; }

      if (k > kmax) { kmax = k; }

      occ_mask[idx] = 1;

      //
      // Weighted by the bin's own coherence, for the reason the wideband
      // window offers the same choice: summing flat makes h a
      // power-weighted average of h(f), and the marginal bins that only
      // just cleared the occupancy threshold then add their noise to the
      // denominator while adding little signal to the numerator.
      //
      // Occupancy has already thrown out the bins that are pure noise,
      // so this is a smaller correction here than it is over a whole
      // passband - on a strong signal every occupied bin has g2 near 1
      // and it does nothing at all. It earns its place on a weak one,
      // where the threshold sits just above the floor and most of the
      // occupied bins are marginal.
      //
      // Not an operator control: the threshold has already decided which
      // bins count as signal, and this only stops the weakest of those
      // dominating by weight of numbers.
      //
      sig_xy_re += g2 * xyre;
      sig_xy_im += g2 * xyim;
      sig_xx    += g2 * xx;
      sig_yy    += g2 * yy;
      cur_sig   += (double)fftout0[idx][0] * fftout0[idx][0]
                   + (double)fftout0[idx][1] * fftout0[idx][1]
                   + (double)fftout1[idx][0] * fftout1[idx][0]
                   + (double)fftout1[idx][1] * fftout1[idx][1];
      acc_sig   += xx + yy;
      nsig++;
    }
  }

  //
  // Second pass: the noise covariance, from the bins that are neither
  // occupied nor next to an occupied one.
  //
  // Correlation is deliberately not a disqualification here. Common-mode
  // noise picked up on both feedlines is correlated, and describing it is
  // the whole reason R is measured separately - excluding coherent bins
  // would throw away the one thing this mode can do that Sum cannot.
  // Distance from the signal is what keeps the signal out of R instead.
  //
  for (int k = klo; k <= khi; k++) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    int near = 0;

    for (int d = -DIV_OCC_GUARD; d <= DIV_OCC_GUARD && !near; d++) {
      int j = (k + d) % nfft;

      if (j < 0) { j += nfft; }

      if (occ_mask[j]) { near = 1; }
    }

    if (near) { continue; }

    r01re += bin_xy_re[idx];
    r01im += bin_xy_im[idx];
    r00   += bin_xx[idx];
    r11   += bin_yy[idx];
    nnoise++;
  }

  if (nsig < DIV_OCC_MIN_BINS) {
    //
    // Nothing stands out. Two very different situations look like this
    // and the difference matters, because one of them wants a weight and
    // the other must not get one.
    //
    // The region is *empty*: it is all noise, the median is the noise and
    // nothing clears it. Hold. A weight invented from noise would be
    // applied across the whole passband.
    //
    // The region is *full*: the signal covers all of it, so the median is
    // the signal and nothing clears that either. This is not a corner
    // case - it is what a filter set snugly around the signal looks like,
    // with the follow tick on, which is what a careful operator does.
    // Holding there would be a trap: the better the filter, the more
    // certainly the mode would do nothing.
    //
    // Coherence tells them apart. Accumulate the region as a whole and
    // let the ordinary gate below decide: a full region is coherent and
    // gets a weight, an empty one is not and holds. With every bin called
    // signal there are no noise bins left, so the solve falls through to
    // plain maximum ratio combining further down - which is the right
    // answer when nothing is known about the noise, and is exactly what
    // the wideband Window reference would have produced.
    //
    sig_xy_re = sig_xy_im = sig_xx = sig_yy = 0.0;
    r01re = r01im = r00 = r11 = 0.0;
    cur_sig = acc_sig = 0.0;
    nsig = nnoise = 0;

    for (int k = klo; k <= khi; k++) {
      int idx = k % nfft;

      if (idx < 0) { idx += nfft; }

      const double xx = bin_xx[idx], yy = bin_yy[idx];
      const double den = xx * yy;

      if (den <= 0.0) { continue; }

      double g2 = (bin_xy_re[idx] * bin_xy_re[idx]
                   + bin_xy_im[idx] * bin_xy_im[idx]) / den;

      if (g2 > 1.0) { g2 = 1.0; }

      sig_xy_re += g2 * bin_xy_re[idx];
      sig_xy_im += g2 * bin_xy_im[idx];
      sig_xx    += g2 * xx;
      sig_yy    += g2 * yy;
      cur_sig   += (double)fftout0[idx][0] * fftout0[idx][0]
                   + (double)fftout0[idx][1] * fftout0[idx][1]
                   + (double)fftout1[idx][0] * fftout1[idx][0]
                   + (double)fftout1[idx][1] * fftout1[idx][1];
      acc_sig   += xx + yy;
      nsig++;
    }

    kmin = klo;
    kmax = khi;
  }

  if (nsig < DIV_OCC_MIN_BINS || sig_xx <= 0.0 || sig_yy <= 0.0) {
    div_auto_occ_valid = 0;
    div_auto_coherence = 0.0;
    div_auto_holding = 1;
    return;
  }

  //
  // Have the bins we chose actually still got a signal in them?
  //
  // Deliberately before the span is published and before any solve, so
  // that a transmission ending clears the overlay and the status line
  // rather than leaving both asserting a signal that has gone.
  //
  // Holding rather than flushing. The accumulators keep decaying at the
  // operator's averaging time either way, but the weight in force is the
  // last one measured on a real signal, which is what is wanted across a
  // gap. Flushing would put the loop one block from the start, where the
  // single-block cross spectrum is perfectly coherent by construction and
  // any bin at all looks like a signal.
  //
  if (acc_sig > 0.0 && cur_sig * pow(10.0, DIV_STALE_DB / 10.0) < acc_sig) {
    div_auto_occ_valid = 0;
    div_auto_holding = 1;
    return;
  }

  //
  // Publish the occupied span for the overlay and the status line, back
  // in the shifted frame the operator's controls use. Half a bin either
  // side so a single occupied bin still has a width to draw. The mapping
  // inverts, so the edges swap - see div_shift_to_bin().
  //
  {
    const double fo = div_frame_off(ctx);
    const double sa = -((double)kmin - 0.5) * binhz - fo;
    const double sb = -((double)kmax + 0.5) * binhz - fo;
    div_auto_occ_lo = (sa < sb) ? sa : sb;
    div_auto_occ_hi = (sa < sb) ? sb : sa;
    div_auto_occ_valid = 1;
  }
  const double xy2 = sig_xy_re * sig_xy_re + sig_xy_im * sig_xy_im;
  div_auto_coherence = xy2 / (sig_xx * sig_yy);

  if (div_auto_coherence > 1.0) { div_auto_coherence = 1.0; }

  if (div_auto_coherence < div_auto_coherence_min) {
    div_auto_holding = 1;
    return;
  }

  div_auto_holding = 0;
  //
  // Per-arm SNR, from the same two sets of bins the solve uses: the
  // channel ratio over the occupied ones, the two noise powers over the
  // rest. Where occupancy found no noise bins there is nothing to divide
  // by and the estimate is simply unavailable.
  //
  {
    double db = 0.0;
    int ok = 0;

    if (nnoise >= DIV_OCC_MIN_BINS && r00 > 0.0 && r11 > 0.0 && sig_xx > 0.0) {
      const double hr = (sig_xy_re * sig_xy_re + sig_xy_im * sig_xy_im)
                        / (sig_xx * sig_xx);

      if (hr > 0.0) {
        db = 10.0 * log10(hr * r00 / r11);
        ok = 1;
      }
    }

    div_arm_publish(ok, db);
  }

  if (div_auto_mode == DIV_AUTO_BEST) {
    div_apply_best(sig_xy_re / sig_xx, sig_xy_im / sig_xx);
    return;
  }

  if (div_auto_mode == DIV_AUTO_NULL) {
    //
    // Null cancels what the region is sitting on, which is the occupied
    // part of it - the same objective as everywhere else, restricted to
    // the bins that carry something. MVDR has no part in it: it maximises
    // the signal-to-interference ratio of the thing it is pointed at, and
    // Null exists to do the opposite.
    //
    div_apply_weight(-sig_xy_re / sig_yy, -sig_xy_im / sig_yy);
    return;
  }

  if (nnoise < DIV_OCC_MIN_BINS || r00 <= 0.0 || r11 <= 0.0) {
    //
    // The signal fills the region, so there are no noise bins to build R
    // from. Diagonal loading would not rescue an empty covariance - it
    // would just return the unweighted answer through a longer route - so
    // take the maximum ratio combining weight directly and say nothing:
    // it is the correct answer when nothing is known about the noise.
    //
    div_apply_weight(sig_xy_re / sig_xx, sig_xy_im / sig_xx);
    return;
  }

  //
  // h with arm 0 as the reference. Writing z_m = a_m s + n_m and
  // accumulating over the signal bins,
  //
  //   Sxx = |a0|^2 S,   Sxy = a0 conj(a1) S
  //
  // so h0 = Sxx = a0 * (conj(a0) S) and h1 = conj(Sxy) = a1 * (conj(a0) S)
  // are the two channels scaled by one common complex factor, which is
  // all MVDR needs since the solve normalises arm 0 to unity.
  //
  {
    double wr, wi;
    div_mvdr2(r00, r11, r01re, r01im,
              sig_xx, 0.0, sig_xy_re, -sig_xy_im,
              &wr, &wi);
    div_apply_weight(wr, wi);
  }
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

#ifdef DIVERSITY_CAPTURE

  //
  // DEVELOPMENT TOOL - remove with the rest of the capture instrument.
  //
  // The tap. This block is what the correlator is about to be given, and
  // the correlator globals still hold what the previous block left, so a
  // record written here is an (input, state) pair the replay can be
  // checked against. See src/diversity_capture.h.
  //
  if (div_capture_active) {
    struct divcap_block m;
    memset(&m, 0, sizeof(m));
    m.dropped         = (guint32)divcap_dropped;
    m.rec_flags       = 0;
    m.frequency       = (gint64)ctx.frequency;
    m.ctun_frequency  = (gint64)ctx.ctun_frequency;
    m.offset          = (gint64)ctx.offset;
    m.sidetone        = ctx.sidetone;
    m.ctx_sample_rate = ctx.sample_rate;
    m.mode            = ctx.mode;
    m.filter_low      = ctx.filter_low;
    m.filter_high     = ctx.filter_high;
    m.ref             = ctx.ref;
    m.follow          = ctx.follow;
    m.weighting       = ctx.weighting;
    m.centre          = ctx.centre;
    m.width           = ctx.width;
    //
    // Recorded whatever the reference is, so a capture taken while
    // watching one mode can still be replayed into another. The RADE
    // branch below derives these two the same way.
    //
    {
      const int expect = div_rade_side_expected(&ctx);
      m.expect_bank = (expect == 0) ? -1 : (expect < 0 ? 0 : 1);
    }
    m.auto_mode        = div_auto_mode;
    m.frame_off        = div_frame_off(&ctx);
    m.tau              = div_auto_tau;
    m.hang             = div_auto_hang;
    m.live_locked      = rade_corr_locked;
    m.live_confirming  = rade_corr_confirming;
    m.live_mirrored    = rade_corr_mirrored;
    m.live_holding     = div_auto_holding;
    m.live_quality     = rade_corr_quality;
    m.live_freq_off    = rade_corr_freq_off;
    m.live_snr         = rade_corr_snr;
    m.live_coherence   = div_auto_coherence;
    m.live_track_gain  = div_track_gain;
    m.live_track_phase = div_track_phase;
    m.live_cos         = div_cos;
    m.live_sin         = div_sin;
    diversity_capture_block(work0, work1, &m);
  }

  divcap_dropped = 0;
#endif

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
                               div_frame_off(&ctx), div_auto_tau, div_auto_hang,
                               &wr, &wi);
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

    //
    // The correlator measures both arms whenever it is locked, whether or
    // not it produced a weight this block.
    //
    div_arm_publish(rade_corr_arm_valid, rade_corr_arm_db);

    if (ok) {
      div_auto_coherence = rade_corr_quality;
      div_auto_holding = 0;

      if (div_auto_mode == DIV_AUTO_BEST) {
        //
        // rade_corr_arm_cos/sin is the unit weight that brings arm 1 onto
        // arm 0 in phase, which is all div_apply_best() wants. The MVDR
        // weight in wr/wi would do at a pinch but its phase is not the
        // co-phasing one - measured 18 degrees off on the capture where
        // arm 1 won - and the whole point of this mode is not to depend
        // on that solve.
        //
        div_apply_best(rade_corr_arm_cos, rade_corr_arm_sin);
        return;
      }

      //
      // Respect the objective, as every other reference does.
      //
      // The correlator always solves for the weight that maximises the
      // pilot's SINR - that is what MVDR against the interference
      // covariance means, and there is no second answer to compute.
      // Turning it through 180 degrees is what Null asks for here:
      // cancel the signal the pilot is pointing at rather than combine
      // for it, which is how an operator checks that the array really is
      // pointed at the RADE station and not at something else.
      //
      // Without this the objective and the Invert button were inert in
      // this mode. diversity_auto_invert() turns div_cos/div_sin over
      // immediately, so the audio changed - and then the next block
      // applied the un-inverted answer again and slewed straight back,
      // which looks like a control that does not work rather than one
      // that is not implemented.
      //
      const double sign = (div_auto_mode == DIV_AUTO_SUM) ? 1.0 : -1.0;
      div_apply_weight(sign * wr, sign * wi);
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
    // The second call can fail where the first succeeded - the carrier can
    // be near enough the Nyquist limit that its few bins clamp away to
    // nothing - and klo/khi would then still hold the whole search window.
    // Accumulating that as if it were the carrier bin is worse than not
    // measuring at all.
    //
    if (!div_bin_range(&ctx, &klo, &khi)) {
      div_auto_holding = 1;
      return;
    }
  }

  //
  // See below: weighting applies to the wideband window only.
  //
  const int coherence_weighted = (ctx.weighting == DIV_WEIGHT_COHERENCE)
                                 && (ctx.ref == DIV_REF_BAND);
  //
  // Exponential forgetting across blocks, applied per bin.
  //
  double alpha = 1.0 - exp(-blocktime / div_auto_tau);

  if (!acc_valid) {
    alpha = 1.0;
    acc_valid = 1;
  }

  double cur_xx = 0.0, cur_yy = 0.0;

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
    //
    // Unweighted window power per arm, for the noise floor tracker. It
    // has to be unweighted and it has to be per arm: a coherence-weighted
    // sum follows the signal, which is the one thing a noise floor must
    // not do.
    //
    cur_xx += i0 * i0 + q0 * q0;
    cur_yy += i1 * i1 + q1 * q1;
  }

  //
  // Digital I/Q takes it from here. The region has been accumulated;
  // which of its bins are signal is decided from the spectrum, which is
  // why this cannot happen in div_bin_range() with the rest.
  //
  if (ctx.ref == DIV_REF_DIGITAL_IQ) {
    div_digital_solve(&ctx, klo, khi);
    return;
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
  //
  // This block's power against the smoothed power, over the same bins and
  // with the same weights, so the staleness test below asks about exactly
  // what the estimate is being made from. See DIV_STALE_DB.
  //
  double cur_p = 0.0, acc_p = 0.0;

  for (int k = klo; k <= khi; k++) {
    int idx = k % nfft;

    if (idx < 0) { idx += nfft; }

    double xx = bin_xx[idx], yy = bin_yy[idx];
    double w = 1.0;

    //
    // Only where there is a window of bins to choose between. The carrier
    // reference accumulates a handful either side of one peak, all of them
    // the same signal, so weighting them against each other does nothing -
    // and the menu hides the control there, which would otherwise leave
    // whichever setting was last chosen in Window mode silently in force.
    //
    if (coherence_weighted) {
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
    cur_p     += w * ((double)fftout0[idx][0] * fftout0[idx][0]
                      + (double)fftout0[idx][1] * fftout0[idx][1]
                      + (double)fftout1[idx][0] * fftout1[idx][0]
                      + (double)fftout1[idx][1] * fftout1[idx][1]);
    acc_p     += w * (xx + yy);
    wsum      += w;
  }

  //
  // Per-arm signal and noise, before any of the gates below: the floor
  // has to go on learning while the loop is holding, because holding is
  // mostly what it does between overs and between overs is when the floor
  // is measurable.
  //
  arm_pw0 += alpha * (cur_xx - arm_pw0);
  arm_pw1 += alpha * (cur_yy - arm_pw1);
  {
    //
    // A second, much shorter smoothing, for the floor only. See
    // DIV_FLOOR_TAU.
    //
    const double fa = 1.0 - exp(-blocktime / DIV_FLOOR_TAU);
    arm_fast0 += fa * (cur_xx - arm_fast0);
    arm_fast1 += fa * (cur_yy - arm_fast1);
  }
  div_arm_floor_update(arm_fast0, arm_fast1);
  {
    //
    // Evaluated into a local first: the order in which a call's arguments
    // are evaluated is unspecified, so passing the estimate and the
    // function that produces it in one expression reads the estimate
    // before it has been written.
    //
    double db = 0.0;
    const int ok = div_arm_from_floor(arm_pw0, arm_pw1, &db);
    div_arm_publish(ok, db);
  }

  if (acc_xx <= 0.0 || acc_yy <= 0.0 || wsum <= 0.0) {
    div_auto_coherence = 0.0;
    div_auto_holding = 1;
    return;
  }

  //
  // Is what these statistics describe still on the air?
  //
  // Under Coherence weighting the comparison is weighted too, so it
  // follows the bins the estimate actually rests on rather than the whole
  // window - which is what makes it sensitive to a narrow signal, a CW
  // carrier included, stopping inside a wide filter.
  //
  if (acc_p > 0.0 && cur_p * pow(10.0, DIV_STALE_DB / 10.0) < acc_p) {
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

  if (div_auto_mode == DIV_AUTO_BEST) {
    div_apply_best(acc_xy_re / acc_xx, acc_xy_im / acc_xx);
    return;
  }

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
    int dropped = q_gap[q_tail];
    q_gap[q_tail] = 0;
    g_mutex_unlock(&mbox_mutex);

    if (reset_requested) {
      reset_requested = 0;
      rade_corr_reset();
    }

#ifdef DIVERSITY_CAPTURE
    //
    // DEVELOPMENT TOOL - the capture hook in div_process_block() marks
    // the discontinuity in the file. Remove with the rest.
    //
    divcap_dropped = dropped;
#endif

    if (dropped > 0) {
      //
      // The block about to be processed is the first after a hole in the
      // sample stream. Everything the correlator knows about where the
      // pilot is refers to a clock that has just skipped, so start again
      // rather than track something that has moved.
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
    //
    // This block is the first one after any gap, so it carries the count.
    //
    q_gap[q_head] = q_pending_drop;
    q_pending_drop = 0;
    q_count++;
    q_head = (q_head + 1) % DIV_QUEUE;
    g_cond_signal(&mbox_cond);
  } else {
    q_pending_drop++;
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
    occ_scratch = g_new0(double, DIV_OCC_MAX_SAMPLES);
    occ_mask    = g_new0(unsigned char, DIV_MAX_NFFT);
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
  q_pending_drop = 0;
  memset(q_gap, 0, sizeof(q_gap));
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
      // fall back to Digital I/Q rather than silently doing nothing if
      // that ever stops being true - it places itself on the operator's
      // passband and finds the modem's occupied bins there, which is the
      // job the retired RADE passband reference used to do.
      //
      t_print("%s: falling back to DIV_REF_DIGITAL_IQ\n", __func__);
      div_auto_ref = DIV_REF_DIGITAL_IQ;
    }
  }

  worker = g_thread_new("div_auto", div_worker_thread, NULL);
  //
  // Set last: the sample path tests this without any lock.
  //
  div_auto_running = 1;
}

#ifdef DIVERSITY_CAPTURE
//
// DEVELOPMENT TOOL - remove with the rest of the capture instrument.
//
// The menu arms the capture through here because the block geometry it
// has to be sized for - nfft - is private to this file.
//
int diversity_auto_capture_start(void) {
  if (!div_auto_running || receivers < 1 || receiver[0] == NULL) { return 0; }

  return diversity_capture_start(receiver[0]->sample_rate, nfft);
}

#endif

void diversity_auto_stop(void) {
  if (!div_auto_running) { return; }

#ifdef DIVERSITY_CAPTURE
  //
  // The blocks stop here, so the file has to be closed here: a capture
  // left armed across a sample-rate change would otherwise be waiting for
  // a thread that is never going to feed it again.
  //
  diversity_capture_stop();
#endif

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

//
// Numbering scheme for diversity_auto_ref.
//
// Scheme 1 was BAND, CARRIER, RADE_BAND, RADE_V1, DIGITAL_IQ. The RADE
// passband reference has since been retired - Digital I/Q does the same
// job from the operator's passband and does it better - so scheme 2 is
// BAND, CARRIER, RADE_V1, DIGITAL_IQ, and every value from 2 upwards
// means something different from what it used to.
//
// A file written before this key existed carries no scheme, and the two
// numberings cannot be told apart by inspection: a stored 2 is either the
// old RADE passband or the new RADE V1. Writing the scheme is what makes
// the migration below unambiguous rather than a guess.
//
#define DIV_REF_SCHEME 2

void diversity_auto_save_state(void) {
  SetPropI0("diversity_auto_mode",           div_auto_mode);
  SetPropI0("diversity_auto_ref",            div_auto_ref);
  SetPropI0("diversity_auto_ref_scheme",     DIV_REF_SCHEME);
  SetPropI0("diversity_auto_follow_filter",  div_auto_follow_filter);
  SetPropF0("diversity_auto_centre",         div_auto_centre);
  SetPropF0("diversity_auto_width",          div_auto_width);
  SetPropF0("diversity_auto_tau",            div_auto_tau);
  SetPropF0("diversity_auto_hang",           div_auto_hang);
  SetPropF0("diversity_auto_coherence_min",  div_auto_coherence_min);
  SetPropI0("diversity_auto_weighting",      div_auto_weighting);
  SetPropF0("diversity_auto_resolution",     div_auto_resolution);
  SetPropF0("diversity_band_centre",         div_band_centre);
  SetPropF0("diversity_band_width",          div_band_width);
  SetPropF0("diversity_carrier_centre",      div_carrier_centre);
  SetPropF0("diversity_carrier_width",       div_carrier_width);
  SetPropF0("diversity_digital_centre",      div_digital_centre);
  SetPropF0("diversity_digital_width",       div_digital_width);
}

void diversity_auto_restore_state(void) {
  GetPropI0("diversity_auto_mode",           div_auto_mode);
  GetPropI0("diversity_auto_ref",            div_auto_ref);
  GetPropI0("diversity_auto_follow_filter",  div_auto_follow_filter);
  GetPropF0("diversity_auto_centre",         div_auto_centre);
  GetPropF0("diversity_auto_width",          div_auto_width);
  GetPropF0("diversity_auto_tau",            div_auto_tau);
  GetPropF0("diversity_auto_hang",           div_auto_hang);
  GetPropF0("diversity_auto_coherence_min",  div_auto_coherence_min);
  GetPropI0("diversity_auto_weighting",      div_auto_weighting);
  GetPropF0("diversity_auto_resolution",     div_auto_resolution);
  GetPropF0("diversity_band_centre",         div_band_centre);
  GetPropF0("diversity_band_width",          div_band_width);
  GetPropF0("diversity_carrier_centre",      div_carrier_centre);
  GetPropF0("diversity_carrier_width",       div_carrier_width);
  GetPropF0("diversity_digital_centre",      div_digital_centre);
  GetPropF0("diversity_digital_width",       div_digital_width);

  //
  // Validate everything that came out of the file, not just the two that
  // happened to get clamped first. A props file can be hand-edited or
  // written by a future version, and an out-of-range value here is hard
  // to diagnose from the UI: a bad reference shows a blank combo, and a
  // coherence threshold above 1.0 wedges the loop in permanent HOLD with
  // nothing on screen to say why.
  //
  if (div_auto_mode < DIV_AUTO_OFF || div_auto_mode > DIV_AUTO_BEST) {
    div_auto_mode = DIV_AUTO_OFF;
  }

  //
  // Migrate a reference written under the old numbering. Absent key means
  // scheme 1; see DIV_REF_SCHEME.
  //
  {
    //
    // GetPropI0 leaves the variable alone when the key is absent, so the
    // default here has to be the *old* scheme - a file that predates the
    // key is exactly the one that needs migrating.
    //
    int scheme = 1;
    GetPropI0("diversity_auto_ref_scheme", scheme);

    if (scheme < 2) {
      switch (div_auto_ref) {
      case 2:
        //
        // The RADE passband reference. Digital I/Q replaces it: it places
        // itself on the operator's passband in the same way and finds the
        // modem's occupied bins inside it, so an operator who was using
        // that lands on its successor rather than on something unrelated.
        //
        div_auto_ref = DIV_REF_DIGITAL_IQ;
        break;

      case 3: div_auto_ref = DIV_REF_RADE_V1;    break;   // was RADE V1
      case 4: div_auto_ref = DIV_REF_DIGITAL_IQ; break;   // was Digital I/Q

      default: break;                                     // 0 and 1 unmoved
      }
    }
  }

  if (div_auto_ref < DIV_REF_BAND || div_auto_ref > DIV_REF_DIGITAL_IQ) {
    div_auto_ref = DIV_REF_BAND;
  }

  div_auto_follow_filter = div_auto_follow_filter ? 1 : 0;

  //
  // 0.2, not 0.1, to match the slider's minimum.
  //
  if (div_auto_tau < 0.2) { div_auto_tau = 0.2; }

  if (div_auto_tau > 30.0) { div_auto_tau = 30.0; }

  //
  // Both ends match the slider. Zero is deliberately not allowed: the
  // hang has to outlast the gate that feeds it, which averages over
  // about a second, or a single noisy frame would end a lock.
  //
  if (div_auto_hang < 1.0)  { div_auto_hang = 1.0; }

  if (div_auto_hang > 30.0) { div_auto_hang = 30.0; }

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

  if (div_digital_width < 20.0)    { div_digital_width = 20.0; }

  if (div_digital_width > 40000.0) { div_digital_width = 40000.0; }

  if (div_digital_centre < -400000.0) { div_digital_centre = -400000.0; }

  if (div_digital_centre >  400000.0) { div_digital_centre =  400000.0; }

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
