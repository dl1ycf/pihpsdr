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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "diversity_auto.h"
#include "message.h"
#include "radio.h"
#include "rade_correlator.h"

//
// ----------------------------------------------------------------------
// Chain
// ----------------------------------------------------------------------
//
//   raw DDC IQ (both arms)
//        |
//        |  NCO shift by +frame_off, so the tuned carrier lands at 0 -
//        |  the same frame WDSP works in after xshift(). Nothing else is
//        |  done to the samples; the spectral sense is handled by which
//        |  pilot bank is used, not by conjugating the input.
//        v
//   polyphase FIR decimator, DDC rate -> 8000 Hz
//        |
//        v
//   ring buffer -> pilot correlation over (bank, timing, frequency)
//        |
//        v
//   h0, h1  and the residual covariance Rnn  ->  MVDR weight
//
// ----------------------------------------------------------------------
// Sideband: the operator's passband decides it
// ----------------------------------------------------------------------
//
// RADE is received through an SSB passband, so the modem occupies
// 750..2200 Hz on one side of the tuned carrier and the mirror image of
// that on the other. Which way round depends on the sideband in use.
//
// Correlating a mirrored stream against the pilot is the same as
// correlating the original stream against a *mirrored pilot*, and conj(p)
// is exactly the pilot with its carriers reflected about zero. So the
// sense is a choice of pilot bank: bank 0 is the pilot as transmitted,
// carriers at +750..+2200 Hz in the tapped buffer, and bank 1 is its
// mirror.
//
// Bank 0 is the **LSB** bank. That is measured, not derived. The tapped
// buffer is inverted with respect to RF - see the frequency bookkeeping
// note in diversity_auto.c, which sets out the evidence - so an LSB
// signal, already inverted by the transmitter, arrives here the right way
// up and correlates against the pilot as transmitted. On air, in LSB, on
// a weak signal:
//
//   acq normal=7.97 mirrored=4.75
//
// The operator's passband names the bank, and that is the only one
// searched. A pilot on the other side of the tuned frequency is outside
// the passband, and a lock there is of no use: this mode exists to pull
// coherence out of the signal the operator is listening to, not to find
// whatever modem happens to be nearby. Only when the passband straddles
// zero and so says nothing - AM, SAM, FM - are both banks searched.
//
// It has been every way round. Deriving the sense from vfo[].mode and
// conjugating the input never acquired on air, but the frame conversion
// had the wrong sign at the time, so that evidence proved nothing.
// Searching both banks blind did acquire, and then left the choice to
// noise on a signal near the noise floor - which is where RADE lives - so
// the analysis window, and the green overlay drawn from it, could settle
// on the wrong side of an LSB passband, or flip there at the moment of
// locking. Constraining it to the passband is both the right answer for
// this mode and the cheaper one: it halves the search, which is by far
// the most expensive thing in this file.
//
// Conjugation stays out of the sample path throughout, so there is no
// weight to conjugate back: whichever bank is used, h0 and h1 describe
// the real untouched arms and the MVDR solution applies to them directly.
//

//
// Acquisition search. This matches the +/-50 Hz that RADE's own
// acquisition covers: a first version searched only +/-25 Hz on the
// grounds that the operator has already tuned the signal, which is one
// more way to silently find nothing if they are a little off frequency.
// Timing is searched on a coarse grid and then refined, which is what
// keeps this affordable - a full 960-position search at every frequency
// would cost tens of millions of complex MACs per attempt.
//
#define RADE_ACQ_FRANGE     100.0   // Hz, total span
#define RADE_ACQ_FSTEP      5.0     // Hz
#define RADE_ACQ_NFREQ      21
#define RADE_ACQ_TSTEP      4       // coarse timing step, samples
#define RADE_ACQ_TREFINE    4       // +/- refinement around the coarse peak

//
// Lock management.
//
// The detection statistic is the pilot correlation peak measured against
// the correlation *floor* - the level the same correlation reaches where
// there is no pilot - not against the total received energy.
//
// That distinction matters more than it looks. Normalising by received
// energy gives a number that collapses exactly when a strong interferer
// is present, which is the case this whole module exists to handle: a
// first version of this locked happily on a clean signal and then refused
// to lock at all once QRM was added. Interference raises the correlation
// floor only as its amplitude, while a real pilot puts a peak at
// h*pilot_energy regardless of what else is in the band, so peak-over-
// floor degrades gracefully instead of falling off a cliff.
//
// The floor is characterised by its mean *and* its spread, and the
// statistic is (peak - mean) / sd rather than peak / mean. That second
// refinement is not cosmetic either: a carrier sitting on one of the
// RADE subcarriers correlates with the pilot identically at every timing
// offset, so it lifts the whole grid into a flat coherent pedestal. In a
// test with such a carrier 36 dB above the pilot, the floor mean rose to
// 8.88 against a peak of 10.0 - peak/mean was 1.13 and detected nothing,
// while the pilot bump above the pedestal was still perfectly clear.
// Subtracting the mean removes the pedestal; dividing by the spread
// measures what is left in the right units.
//
// For pure Rayleigh noise this statistic lands near 4 for the largest of
// a few thousand cells, so a threshold well clear of "could plausibly be
// noise" is wanted. Measured against pure noise with no signal at all the
// statistic reaches 3.0 to 4.6.
//
// This statistic is used for acquisition only. Holding an existing lock
// is a different and much more forgiving test - see RADE_USE_RATIO and
// the hang time it is counted against.
//
// 4.8 rather than the 6.0 this started at.
//
// The threshold does not have to carry the whole false-alarm budget on
// its own, because what it produces is a *candidate*, not a lock:
// RADE_PROBATION frames of the ordinary tracking test follow, no weight
// is produced while they run, and that test is a pilot-to-floor ratio
// rather than this statistic, so it fails a noise candidate on different
// evidence. Measured over two minutes of pure noise at 4.8: no false
// candidates at all, and no false locks.
//
// Be clear about what lowering it did and did not buy, because the
// numbers are not what one would guess. It does let a weak signal through
// this gate - at the point where a 32-pass score of 5.90 used to miss 6.0
// by a tenth, a candidate is now raised at the correct frequency. But
// across 7 SNRs x 3 seeds and 5 noise levels x 5 seeds on synthetic
// signals, 4.8 and 6.0 produce *identical* outcomes: every case that
// locks at one locks at the other, in the same number of blocks. The
// candidates the lower threshold raises are then turned down by
// probation, at pilot/floor 1.83 against the 2.5 RADE_USE_RATIO wants.
//
// So RADE_USE_RATIO, not this, is what currently sets the weak-signal
// floor. That is the constant to look at if the threshold needs to come
// down further - and it is the one that is actually holding the
// false-alarm line, so it should not be moved without measuring what it
// lets through.
//
#define RADE_LOCK_SIGMA     4.8

//
// Acquisition time.
//
// The grid is scored at each of these pass counts rather than only at the
// end, with a threshold that starts high and comes down as the
// integration lengthens. A strong signal is therefore declared after
// about a second instead of waiting out the full integration a weak one
// needs, and the weak case is no worse than before.
//
// The thresholds are raised for the early looks because scoring three
// times instead of once gives noise three chances: at 8 passes the
// largest cell of a few thousand sits near 4 with a tail, and 7.5 keeps
// the false-alarm rate of the whole schedule at about that of the single
// 6.0 test it replaces.
//
// A candidate is not the same as a lock. What follows is RADE_PROBATION
// frames of the ordinary tracking test at the candidate's timing and
// frequency, during which no weight is produced: a false alarm therefore
// costs a second of waiting and never moves the combiner. Confirming the
// one cell we care about is enormously cheaper than the old scheme, which
// re-ran the entire blind search RADE_LOCK_FRAMES times - 3 x 32 x 120 ms,
// 11.5 s before a lock could ever be declared, on any signal however
// strong. That was the dominant term in acquisition time and it bought
// nothing that a cheap confirmation does not buy better.
//
#define RADE_ACQ_CHECKS     3
static const int    rade_acq_at[RADE_ACQ_CHECKS]    = { 8, 16, 32 };
static const double rade_acq_sigma[RADE_ACQ_CHECKS] = { 7.5, 6.75, RADE_LOCK_SIGMA };

#define RADE_PROBATION      8       // frames, ~1 s


//
// Holding a lock is deliberately far more forgiving than getting one.
//
// Acquisition integrates 32 passes over the whole timing-by-frequency
// grid; a tracking frame has one correlation and a handful of probes, so
// its statistic is enormously noisier. Gating each frame on a fresh
// detection threw away locks on signals that had just acquired at three
// times the required margin.
//
// The criterion is the pilot correlation against the correlation floor
// measured off-pilot in the same frame - a ratio, so it does not depend
// on signal level at all.
//
// It deliberately is *not* measured against the level at lock. A version
// that did that ratcheted its reference up to the highest level ever
// seen, so under fading the ratio lived permanently below one and any
// deep enough fade eventually crossed the threshold and dropped the
// lock. On air that gave 50-second locks that ended in a fade.
//
// Which is backwards for a diversity system: a fade is exactly when the
// combining weight is worth the most. Losing the pilot for a few seconds
// means keep going with the last good weight, not start again. Only a
// signal that is really gone - end of over, or the operator retuning -
// should force a re-acquisition.
//
// How long "really gone" is depends on what the operator is listening to,
// so it is the Hang control in the Diversity menu rather than a constant
// here, and it arrives as the hang argument to rade_corr_process().
//
// It replaced a fixed ten seconds counted off the *slow* ratio below,
// which was too slow twice over. That average has a six-second time
// constant, so on a signal that simply stopped it took about seven
// seconds to fall from the six a clean lock reads to the two the test
// wanted, and only then did the ten start. Sixteen seconds of holding one
// station's weight is not what the ten was meant to mean, and on a
// frequency where several stations take turns it is most of an over -
// each one has its own optimal weight, and the combiner spent the
// beginning of every over applying the previous station's.
//
// So the hang is counted off the fast gate instead: RADE_USE_RATIO below
// already decides, about a second at a time, whether this frame is worth
// measuring, and consecutive frames that are not are exactly what "the
// pilot is not there" means. One good frame resets the count, so a fade
// that flickers does not accumulate towards a drop - only a continuous
// absence does.
//
// mag_avg/floor_avg survive as the *reported* health of a lock, which is
// what a level-independent ratio over several seconds is good for: a
// clean lock reads about 6 on it, and 1 would mean the pilot correlates
// no better than anything else in the frame does.
//
#define RADE_MAG_ALPHA      0.02    // ~6 s at 8.33 modem frames/s
#define RADE_FLOOR_PROBES   4

//
// Keeping the lock and trusting the current frame are two different
// decisions, and conflating them was a bug worth spelling out.
//
// When the pilot went away the code kept the lock - correctly, so a fade
// does not cost the weight - but then carried on feeding the channel
// estimate and the covariance from correlations that were pure noise. For
// up to the full hold time the combining weight was being steered by
// nothing at all.
//
// So the frame gate is separate and faster: below it the accumulators and
// the weight are frozen at their last good values, while the slow ratio
// above decides whether the signal has been gone long enough to give up
// and re-acquire.
//
#define RADE_USE_ALPHA      0.12    // ~1 s, for the freeze decision
#define RADE_USE_RATIO      2.50

//
// The reference the pilot correlation is compared against is taken at the
// *same* timing but at frequencies far outside the lock range.
//
// Probing off-pilot in time, which is the obvious thing, turns out to be
// a poor discriminator: those positions land on data symbols carried on
// the same subcarriers, and a random OFDM symbol correlates against the
// pilot nearly as well as the pilot does. The ratio then sits close to
// one even on a clean signal, and any threshold placed there chatters.
//
// A 20 ms correlation window has its first ambiguity null at 50 Hz, so
// 300 Hz away the true pilot contributes essentially nothing while noise
// and interference contribute exactly as much as they do on frequency.
//
#define RADE_FLOOR_DF       300.0

//
// Timing cells within this many samples of the peak are excluded from the
// floor estimate, so the pilot does not contaminate its own reference.
//
#define RADE_FLOOR_GUARD    12

//
// Which bins the interference covariance is measured in.
//
// The pilot span is 160 samples at 8 kHz, so its DFT bins are exactly the
// modem's own 50 Hz carrier grid: carrier c sits in bin 15+c and the 30
// carriers fill bins 15..44. The bins immediately either side carry
// noise, QRM and the skirts of whatever else is in the passband, but no
// modem - which is what a covariance for MVDR has to be built from.
//
// 300 Hz to 2850 Hz, off the carriers: a bin either side of everything a
// 2.7 kHz filter passes, and inside the decimator's 3 kHz corner at both
// ends. Twenty-two bins per frame, EWMA'd over the operator's averaging
// time, so the estimate behind the solve is thousands of samples deep
// even though one frame contributes twenty-two.
//
// They are placed on the modem's *own* side of the tuned frequency,
// which is the part that matters. What made the old pilot-span residual
// useless was that it swept in the rejected sideband - a whole station
// WDSP filters away and the operator never hears - and MVDR nulled that
// instead of the interference. Choosing the bins by the pilot bank keeps
// the other sideband out by construction.
//
// Clipping the set to the operator's passband as well was tried, and
// measured worse: on a 2 kHz filter it drops back to the eleven bins
// immediately beside the carriers and gives up 0.9 dB, because halving
// the bin count doubles the variance of R. The 350 Hz beyond a tight
// filter that this keeps is the same band noise the modem is sitting in,
// not a second station. See docs/diversity-measurements.md.
//
#define RADE_GUARD_LO0      6       //  300 Hz
#define RADE_GUARD_HI1      57      // 2850 Hz
#define RADE_CARRIER_K0     15      //  750 Hz, first modem carrier
#define RADE_CARRIER_K1     44      // 2200 Hz, last
#define RADE_GUARD_BINS     ((RADE_CARRIER_K0 - RADE_GUARD_LO0) + \
                             (RADE_GUARD_HI1 - RADE_CARRIER_K1))

//
// Acquisition passes accumulated before the decision. The pilot lands in
// the same grid cell every pass - the search grid is anchored to absolute
// sample index modulo one modem frame, so it does not slide - while the
// floor averages down as sqrt(passes). Interference that correlates with
// the pilot contributes a constant pedestal which the mean subtraction
// removes; what is left to beat is the spread, and that is what this
// buys down.
//
// Measured on a synthetic signal with a strong CW interferer inside the
// modem band, single pass: +10 dB interferer-to-pilot gives a statistic
// of 6.5, +20 dB gives 2.4, +36 dB gives 1.1. Eight passes (about a
// second) lift the +20 dB case over the threshold, and 32 passes reach
// the +36 dB case.
//
#define RADE_ACQ_PASSES     32

//
// Map the sigma statistic onto 0..1 for display: 0 at the noise-peak
// level, 1 well clear of it.
//
#define RADE_STAT_TO_Q(x)   (((x) - 4.0) / 8.0)

//
// The forgetting factor for the channel and covariance estimates comes
// from the Averaging control in the Diversity menu, not from a constant
// here. It was fixed at roughly 1.5 s to begin with, which turned out to
// be far too short: at the pilot SNR a real signal delivers - swinging
// between about -10 and +3 dB frame to frame - the resulting weight
// swung with it and the movement itself degraded recovery.
//
// The time constant that suits a given path is a judgement about how
// fast it is fading, so it belongs to the operator.
//
#define RADE_FRAME_SECS     ((double)RADE_CORR_NMF / (double)RADE_CORR_FS)

//
// Frequency tracking once locked.
//
// Acquisition leaves the frequency quantised to the RADE_ACQ_FSTEP grid,
// so up to half a step out, and a station drifts a few Hz a minute. The
// phase advance of the pilot correlation from one modem frame to the next
// measures the residual directly: one frame is 120 ms, so the
// discriminator is unambiguous over +/-4.17 Hz, which covers both.
//
// It is deliberately slow - a couple of seconds - because there is
// nothing fast to follow. A drift of a few Hz per minute is 0.01 Hz in
// the time this settles.
//
// The update is skipped on any frame where the timing was nudged: a
// one-sample shift rotates the correlation by 2*pi*1500/8000 radians all
// by itself, which the discriminator cannot tell from a frequency error.
//
#define RADE_FREQ_ALPHA     0.05
#define RADE_FREQ_LIMIT     (0.5 * RADE_ACQ_FRANGE + 10.0)

//
// Decimator: taps per polyphase branch, and the low-pass corner. The
// filter only has to keep 750..2200 Hz and reject everything that would
// alias into it after decimation to 8 kHz, which starts at 8000-2200 =
// 5800 Hz. That is a very wide transition, so a modest filter does.
//
#define RADE_DEC_TAPS_PER_PHASE  16
#define RADE_DEC_CUTOFF          3000.0

#define RADE_RING       (8 * RADE_CORR_NMF)
#define RADE_ACQ_SPAN   (2 * RADE_CORR_NMF + RADE_CORR_M + RADE_CORR_NCP)

int    rade_corr_locked   = 0;
double rade_corr_freq_off = 0.0;
double rade_corr_snr      = 0.0;
double rade_corr_quality  = 0.0;
int    rade_corr_mirrored = 0;
int    rade_corr_confirming = 0;

typedef struct {
  double re, im;
} cplx;

static inline cplx cset(double r, double i)      { cplx c = {r, i}; return c; }
static inline cplx cadd(cplx a, cplx b)          { return cset(a.re + b.re, a.im + b.im); }
static inline cplx cmul(cplx a, cplx b)          { return cset(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
static inline cplx cscale(cplx a, double s)      { return cset(a.re * s, a.im * s); }
static inline cplx cconj(cplx a)                 { return cset(a.re, -a.im); }
static inline double cabs2(cplx a)               { return a.re * a.re + a.im * a.im; }

static int    running = 0;
static int    decim = 0;             // DDC rate / 8000
static int    ntaps = 0;
static float *taps = NULL;           // decimation FIR, ntaps
static float *dline0 = NULL;         // delay lines, one per arm
static float *dline1 = NULL;         // interleaved complex, ntaps entries
static int    dpos = 0;
static int    phase = 0;             // input samples since last output

static double nco_phase = 0.0;       // shift NCO, radians

static float *ring0 = NULL;          // decimated 8 kHz history, interleaved
static float *ring1 = NULL;
static int    ringw = 0;             // write index, samples
//
// The decimated sample clock. int64_t, not long: on a 32-bit build - and
// 32-bit Raspberry Pi OS is a normal piHPSDR target - long is 32 bits,
// which overflows after 2^31/8000 s, about 3.1 days of continuous RADE
// operation. Signed overflow would then wrap these negative and
// ring_get() would index the ring out of bounds.
//
static int64_t ringtotal = 0;        // total samples ever written

static cplx   pilot[RADE_CORR_M];            // time domain pilot, no CP
//
// Two banks: the pilot as transmitted, and its mirror image. conj(p)
// reflects every carrier about zero, so bank 1 finds the signal when the
// baseband turns out to run the other way.
//
static cplx   pilot_w[2][RADE_ACQ_NFREQ][RADE_CORR_M];
static double acq_freq[RADE_ACQ_NFREQ];
static double pilot_energy = 0.0;

//
// Tracking state
//
static int64_t lock_a = 0;           // absolute sample index of the pilot
static double lock_f = 0.0;          // Hz
static int    drop_count = 0;
static int    tracking = 0;          // a candidate is being followed
static int    probation = 0;         // frames of confirmation still owed
static cplx   prev_d0;               // last frame's pilot correlation
static int    prev_valid = 0;        // ... and whether it is usable
static int    nudged = 0;            // timing moved this frame
static int64_t next_process = 0;     // ringtotal at which to look again

#define RADE_ACQ_NCELL  (RADE_CORR_NMF / RADE_ACQ_TSTEP)

static double acq_grid[2][RADE_ACQ_NCELL][RADE_ACQ_NFREQ];
static int    acq_passes = 0;
static int    acq_check = 0;      // next entry of rade_acq_at[] to score at
static int    lock_bank = 0;      // which pilot bank actually correlates

static double mag_avg = 0.0;         // smoothed pilot correlation
static double floor_avg = 0.0;       // smoothed off-pilot correlation
static double use_mag = 0.0;         // faster pair, for the freeze decision
static double use_floor = 0.0;
static int    frozen = 0;
static int    track_report = 0;

//
// The channel, as a cross-spectrum: acc_x01 averages d1*conj(d0) and
// acc_x00 averages |d0|^2. See the note in rade_track() for why the two
// arms' correlations are not averaged coherently on their own.
//
static cplx   acc_x01;
static double acc_x00 = 0.0;
static cplx   acc_r01;
static double acc_r00 = 0.0, acc_r11 = 0.0;
static double acc_sig = 0.0;
static int    acc_valid = 0;

//
// --------------------------------------------------------------------
// Pilot
// --------------------------------------------------------------------
//
// Reproduces rade_barker_pilots() followed by the IDFT in
// rade_ofdm_init(). We only need the pilot waveform, not the decoder, so
// this avoids taking librade as a build dependency - and librade does not
// expose a channel estimate anyway, which is what we actually want from
// it.
//
//   Rs'   = Fs/M = 50 Hz
//   first carrier = 1500 - Rs'*Nc/2 = 750 Hz, index 15
//   w[c]  = 2*pi*(15+c)/M
//   P[c]  = sqrt(2) * barker13[c % 13]        (real)
//   p[n]  = sum_c P[c] * exp(j*w[c]*n) / M
//
static void rade_corr_make_pilot(void) {
  static const double barker13[13] = {
    1.0, 1.0, 1.0, 1.0, 1.0, -1.0, -1.0, 1.0, 1.0, -1.0, 1.0, -1.0, 1.0
  };
  const double rs_dash = (double)RADE_CORR_FS / (double)RADE_CORR_M;
  const int carrier_1 = (int)lround((1500.0 - rs_dash * RADE_CORR_NC / 2.0) / rs_dash);

  for (int n = 0; n < RADE_CORR_M; n++) {
    pilot[n] = cset(0.0, 0.0);
  }

  for (int c = 0; c < RADE_CORR_NC; c++) {
    double w = 2.0 * M_PI * (double)(carrier_1 + c) / (double)RADE_CORR_M;
    double p = sqrt(2.0) * barker13[c % 13];

    for (int n = 0; n < RADE_CORR_M; n++) {
      double th = w * (double)n;
      pilot[n].re += p * cos(th) / (double)RADE_CORR_M;
      pilot[n].im += p * sin(th) / (double)RADE_CORR_M;
    }
  }

  pilot_energy = 0.0;

  for (int n = 0; n < RADE_CORR_M; n++) {
    pilot_energy += cabs2(pilot[n]);
  }

  //
  // Pre-rotate the pilot for each frequency hypothesis, so acquisition is
  // a plain dot product per (timing, frequency) cell.
  //
  for (int f = 0; f < RADE_ACQ_NFREQ; f++) {
    acq_freq[f] = -0.5 * RADE_ACQ_FRANGE + RADE_ACQ_FSTEP * (double)f;
    double w = 2.0 * M_PI * acq_freq[f] / (double)RADE_CORR_FS;

    for (int n = 0; n < RADE_CORR_M; n++) {
      cplx rot = cset(cos(w * n), sin(w * n));
      pilot_w[0][f][n] = cmul(pilot[n], rot);
      pilot_w[1][f][n] = cmul(cconj(pilot[n]), rot);
    }
  }
}

//
// --------------------------------------------------------------------
// Decimator
// --------------------------------------------------------------------
//
static void rade_corr_make_taps(int rate) {
  const double fc = RADE_DEC_CUTOFF / (double)rate;      // normalised
  ntaps = RADE_DEC_TAPS_PER_PHASE * decim;

  if ((ntaps & 1) == 0) { ntaps++; }

  taps = g_new(float, ntaps);
  double sum = 0.0;

  for (int i = 0; i < ntaps; i++) {
    double x = (double)i - 0.5 * (double)(ntaps - 1);
    double s = (fabs(x) < 1e-9) ? (2.0 * fc) : (sin(2.0 * M_PI * fc * x) / (M_PI * x));
    //
    // Blackman-Harris, matching the window used elsewhere in the
    // diversity code.
    //
    double t = 2.0 * M_PI * (double)i / (double)(ntaps - 1);
    double w = 0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2.0 * t) - 0.01168 * cos(3.0 * t);
    taps[i] = (float)(s * w);
    sum += s * w;
  }

  for (int i = 0; i < ntaps; i++) {
    taps[i] = (float)(taps[i] / sum);
  }
}

int rade_corr_start(int ddc_rate) {
  if (running) { return 1; }

  if (ddc_rate <= 0 || (ddc_rate % RADE_CORR_FS) != 0) {
    t_print("%s: DDC rate %d is not a multiple of %d, cannot run\n",
            __func__, ddc_rate, RADE_CORR_FS);
    return 0;
  }

  decim = ddc_rate / RADE_CORR_FS;
  rade_corr_make_taps(ddc_rate);
  rade_corr_make_pilot();
  dline0 = g_new0(float, 2 * ntaps);
  dline1 = g_new0(float, 2 * ntaps);
  ring0  = g_new0(float, 2 * RADE_RING);
  ring1  = g_new0(float, 2 * RADE_RING);
  dpos = 0;
  phase = 0;
  nco_phase = 0.0;
  ringw = 0;
  ringtotal = 0;
  rade_corr_reset();
  running = 1;
  t_print("%s: decim=%d ntaps=%d pilot_energy=%0.4f\n", __func__, decim, ntaps, pilot_energy);
  return 1;
}

void rade_corr_stop(void) {
  if (!running) { return; }

  running = 0;
  g_free(taps);
  g_free(dline0);
  g_free(dline1);
  g_free(ring0);
  g_free(ring1);
  taps = NULL;
  dline0 = dline1 = ring0 = ring1 = NULL;
  rade_corr_locked = 0;
  rade_corr_confirming = 0;
  tracking = 0;
  rade_corr_quality = 0.0;
}

void rade_corr_reset(void) {
  rade_corr_locked = 0;
  rade_corr_confirming = 0;
  tracking = 0;
  drop_count = 0;
  probation = 0;
  prev_valid = 0;
  nudged = 0;
  lock_a = 0;
  lock_bank = 0;
  lock_f = 0.0;
  acc_x01 = cset(0.0, 0.0);
  acc_x00 = 0.0;
  acc_r01 = cset(0.0, 0.0);
  acc_r00 = acc_r11 = 0.0;
  acc_sig = 0.0;
  acc_valid = 0;
  mag_avg = 0.0;
  floor_avg = 0.0;
  use_mag = 0.0;
  use_floor = 0.0;
  frozen = 0;
  track_report = 0;
  rade_corr_quality = 0.0;
  rade_corr_snr = 0.0;
  //
  // These two are only ever written when a lock is taken, so without this
  // they survive a reset - and the menu goes on showing the last lock's
  // sideband and frequency for as long as re-acquisition takes.
  //
  rade_corr_freq_off = 0.0;
  rade_corr_mirrored = 0;
  next_process = 0;
  memset(acq_grid, 0, sizeof(acq_grid));
  acq_passes = 0;
  acq_check = 0;
}

//
// Ring access by absolute sample index. Sample a is valid while it is
// still inside the ring, i.e. ringtotal - a <= RADE_RING.
//
// Indices are absolute rather than "so many back from the newest"
// because blocks do not arrive in whole modem frames: the tap hands us
// nfft samples at a time, which decimates to some arbitrary count. A
// relative offset would therefore slide by a different amount every
// block and walk straight off the pilot.
//
static inline cplx ring_get(const float *r, int64_t a) {
  int idx = (int)(a % RADE_RING);

  //
  // Callers all guard against a < 0, so this is belt and braces - but the
  // cost is one predictable branch and the failure mode without it is an
  // out-of-bounds read. RADE_RING is not a power of two, so masking is
  // not an option.
  //
  if (idx < 0) { idx += RADE_RING; }

  return cset(r[2 * idx], r[2 * idx + 1]);
}

//
// Correlate one arm against the pilot starting at absolute index a.
// Returns sum rx * conj(p_w), which is h * pilot_energy.
//
static cplx rade_correlate(const float *r, int64_t a, const cplx *pw) {
  cplx acc = cset(0.0, 0.0);

  for (int n = 0; n < RADE_CORR_M; n++) {
    acc = cadd(acc, cmul(ring_get(r, a + n), cconj(pw[n])));
  }

  return acc;
}

//
// One DFT bin of the pilot span, at an arbitrary frequency: the sum over
// the span of x[n] * exp(-j*2*pi*hz*n/Fs).
//
// Deliberately unnormalised. By Parseval sum_k |G_k|^2 = M * sum_n
// |x_n|^2, so the *mean* of |G|^2 over a set of bins estimates the energy
// over the span directly - the same unit the residual sums it replaced
// were in, which is what keeps rade_corr_snr and rade_corr_quality
// meaning what they meant.
//
// The rotation is stepped rather than evaluated per sample, as
// rade_corr_process() does with the NCO and for the same reason; over
// 160 steps there is nothing for the error to accumulate into.
//
static cplx rade_dft_bin(const float *r, int64_t a, double hz) {
  const double w = 2.0 * M_PI * hz / (double)RADE_CORR_FS;
  const double cd = cos(w), sd = sin(w);
  double c = 1.0, s = 0.0;
  cplx acc = cset(0.0, 0.0);

  for (int n = 0; n < RADE_CORR_M; n++) {
    cplx x = ring_get(r, a + n);
    //
    // x * conj(exp(j*w*n))
    //
    acc.re += x.re * c + x.im * s;
    acc.im += x.im * c - x.re * s;
    const double ct = c;
    c = ct * cd - s * sd;
    s = ct * sd + s * cd;
  }

  return acc;
}


//
// Coarse then fine search for the pilot on arm 0.
//
static int rade_acquire(int expect_bank) {
  int64_t best_a = 0;
  int best_f = 0;
  //
  // Which pilot banks to search. The operator's sideband names one, and
  // that is the only one looked at: a pilot on the other side of the
  // tuned frequency is outside the passband, and a RADE lock outside the
  // passband is of no use to anybody - this mode exists to pull coherence
  // out of the signal the operator is listening to, not to find whatever
  // modem happens to be nearby. It also halves the cost of the search,
  // which is by far the most expensive thing in this file.
  //
  // Only when the passband straddles zero and so says nothing - AM, SAM,
  // FM - are both searched.
  //
  const int bank_lo = (expect_bank < 0) ? 0 : expect_bank;
  const int bank_hi = (expect_bank < 0) ? 1 : expect_bank;
  //
  // The latest pilot pair we could be looking at ends here.
  //
  const int64_t limit = ringtotal - RADE_CORR_NMF - RADE_CORR_M;

  if (limit < RADE_CORR_NMF) { return 0; }

  //
  // The grid is indexed by absolute sample index modulo one modem frame,
  // not by position within the buffer. That keeps a given pilot in a
  // fixed cell from pass to pass, which is what makes accumulating across
  // passes meaningful.
  //
  for (int cell = 0; cell < RADE_ACQ_NCELL; cell++) {
    int64_t phase_off = (int64_t)cell * RADE_ACQ_TSTEP;
    int64_t a = limit - ((limit - phase_off) % RADE_CORR_NMF);

    if (a - RADE_CORR_NMF < 0 || ringtotal - (a - RADE_CORR_NMF) > RADE_RING) { continue; }

    for (int bank = bank_lo; bank <= bank_hi; bank++) {
      for (int f = 0; f < RADE_ACQ_NFREQ; f++) {
        cplx d1 = rade_correlate(ring0, a, pilot_w[bank][f]);
        cplx d2 = rade_correlate(ring0, a - RADE_CORR_NMF, pilot_w[bank][f]);
        //
        // Two consecutive pilots must both be present. Summing the
        // magnitudes rather than the complex values keeps this insensitive
        // to the phase rotation between frames, and lets passes accumulate
        // without knowing the frequency offset exactly.
        //
        acq_grid[bank][cell][f] += sqrt(cabs2(d1)) + sqrt(cabs2(d2));
      }
    }
  }

  //
  // Score the grid at 8, 16 and 32 passes rather than only at the end, so
  // a signal strong enough to be seen early is not made to wait for the
  // integration a weak one needs.
  //
  acq_passes++;

  if (acq_check >= RADE_ACQ_CHECKS || acq_passes < rade_acq_at[acq_check]) {
    //
    // Keep integrating. Report progress so the UI does not look stalled.
    //
    rade_corr_quality = 0.0;
    return 0;
  }

  const double need = rade_acq_sigma[acq_check];
  const int last_check = (acq_check == RADE_ACQ_CHECKS - 1);
  acq_check++;

  //
  // Score each frequency column separately.
  //
  // A narrowband interferer correlates with the pilot by almost the same
  // amount at every timing offset - shifting the correlation window in
  // time only rotates its phase - but its magnitude varies strongly with
  // the frequency hypothesis. So it appears as a per-column pedestal.
  // Taking the mean and spread down each column removes it, where a
  // single global mean leaves it in the spread and buries the pilot.
  //
  // Integrating more passes does not help against this on its own: the
  // interferer's contribution is deterministic and repeats identically
  // every pass, so it never averages down. Only the per-column reference
  // removes it.
  //
  const int guard = RADE_FLOOR_GUARD / RADE_ACQ_TSTEP;
  double stat = 0.0;
  double stat_bank[2] = { 0.0, 0.0 };
  int bank_f[2] = { 0, 0 };
  int64_t bank_a[2] = { 0, 0 };
  int best_bank = 0;

  for (int bank = bank_lo; bank <= bank_hi; bank++) {
    for (int f = 0; f < RADE_ACQ_NFREQ; f++) {
      int pk = 0;

      for (int cell = 1; cell < RADE_ACQ_NCELL; cell++) {
        if (acq_grid[bank][cell][f] > acq_grid[bank][pk][f]) { pk = cell; }
      }

      double sum = 0.0, sum2 = 0.0;
      long n = 0;

      for (int cell = 0; cell < RADE_ACQ_NCELL; cell++) {
        int d = abs(cell - pk);

        if (d > RADE_ACQ_NCELL / 2) { d = RADE_ACQ_NCELL - d; }

        if (d <= guard) { continue; }

        sum  += acq_grid[bank][cell][f];
        sum2 += acq_grid[bank][cell][f] * acq_grid[bank][cell][f];
        n++;
      }

      if (n < 2) { continue; }

      double mean = sum / (double)n;
      double var = sum2 / (double)n - mean * mean;
      double sd = (var > 0.0) ? sqrt(var) : 0.0;

      if (sd <= 1e-20) { continue; }

      double sf = (acq_grid[bank][pk][f] - mean) / sd;

      if (sf > stat_bank[bank]) {
        stat_bank[bank] = sf;
        bank_f[bank] = f;
        bank_a[bank] = limit - ((limit - (int64_t)pk * RADE_ACQ_TSTEP) % RADE_CORR_NMF);
      }
    }
  }

  //
  // The operator's sideband names the bank; only when it says nothing is
  // there a choice to make here.
  //
  best_bank = (expect_bank < 0) ? ((stat_bank[1] > stat_bank[0]) ? 1 : 0)
              : expect_bank;
  stat = stat_bank[best_bank];
  best_f = bank_f[best_bank];
  best_a = bank_a[best_bank];

  //
  // Always report both banks. If neither ever rises, the signal is not
  // reaching the correlator at all; if one is clearly better but still
  // under threshold, the search is on the right track and the thresholds
  // or the integration are what need attention.
  //
  double rms = 0.0;

  for (int k = 0; k < RADE_CORR_NMF; k++) {
    rms += cabs2(ring_get(ring0, ringtotal - 1 - k));
  }

  rms = sqrt(rms / (double)RADE_CORR_NMF);
  t_print("%s: acq %s carrier =%0.2f (need %0.2f after %d passes) "
          "f=%+0.1f rms=%0.2e\n",
          __func__, best_bank ? "above" : "below", stat, need, acq_passes,
          acq_freq[best_f], rms);

#ifdef RADE_DEBUG_STAT
  t_print("ACQ: stat=%.2f f=%0.1f\n", stat, acq_freq[best_f]);
#endif

  rade_corr_quality = RADE_STAT_TO_Q(stat);

  if (rade_corr_quality < 0.0) { rade_corr_quality = 0.0; }

  if (rade_corr_quality > 1.0) { rade_corr_quality = 1.0; }

  if (stat < need) {
    if (!last_check) {
      //
      // Keep the grid and carry on integrating towards the next check.
      //
      return 0;
    }

    //
    // Out of passes. Start a fresh integration rather than carrying a
    // stale grid forward.
    //
    memset(acq_grid, 0, sizeof(acq_grid));
    acq_passes = 0;
    acq_check = 0;
    return 0;
  }

  //
  // Refine the timing to the sample, around the coarse cell.
  //
  double fine_best = -1.0;
  int64_t fine_a = best_a;

  for (int64_t a = best_a - RADE_ACQ_TREFINE; a <= best_a + RADE_ACQ_TREFINE; a++) {
    if (a < 0 || a + RADE_CORR_M > ringtotal || ringtotal - a > RADE_RING) { continue; }

    double m = cabs2(rade_correlate(ring0, a, pilot_w[best_bank][best_f]));

    if (m > fine_best) {
      fine_best = m;
      fine_a = a;
    }
  }

  best_a = fine_a;
  memset(acq_grid, 0, sizeof(acq_grid));
  acq_passes = 0;
  acq_check = 0;
  //
  // A candidate, not yet a lock: rade_track() has to like it for
  // RADE_PROBATION frames before any weight comes out of it.
  //
  probation = RADE_PROBATION;
  prev_valid = 0;
  lock_a = best_a;
  lock_bank = best_bank;
  rade_corr_mirrored = best_bank;
  lock_f = acq_freq[best_f];
  rade_corr_freq_off = lock_f;
  drop_count = 0;
  return 1;
}

//
// Build the frequency-rotated pilot for an arbitrary offset, used once
// locked so the frequency is no longer quantised to the search grid.
//
static void rade_pilot_at(double f, cplx *out) {
  double w = 2.0 * M_PI * f / (double)RADE_CORR_FS;

  for (int n = 0; n < RADE_CORR_M; n++) {
    cplx rot = cset(cos(w * n), sin(w * n));
    cplx p = (lock_bank == 0) ? pilot[n] : cconj(pilot[n]);
    out[n] = cmul(p, rot);
  }
}

//
// MVDR: w = R^-1 h, normalised so the arm 0 coefficient is 1, then
// expressed as the weight the combiner applies to arm 1.
//
// The solve itself is div_mvdr2() in diversity_auto.c, shared with the
// Digital I/Q reference, which reaches the same covariance and channel
// from spectral occupancy instead of from pilot correlations. Only the
// route to R and h differs, so there is one copy of the algebra.
//
// No sideband correction on the way in: the samples were never
// conjugated, so whichever pilot bank won, h0 and h1 describe the real
// arms directly.
//
static void rade_mvdr_weight(double *wr, double *wi) {
  //
  // h0 = |a0|^2 S and h1 = a1 conj(a0) S: the two channels scaled by one
  // common factor, which is all the solve needs since it normalises arm 0
  // to unity. The same shape div_digital_solve() hands it from bin_xx and
  // bin_xy.
  //
  div_mvdr2(acc_r00, acc_r11, acc_r01.re, acc_r01.im,
            acc_x00, 0.0, acc_x01.re, acc_x01.im,
            wr, wi);
}

//
// Once locked, measure the channel on both arms at the tracked timing and
// frequency, update the covariance of what is left over, and solve.
//
static int rade_track(double tau, double hang, double *wr, double *wi) {
  cplx pw[RADE_CORR_M];
  rade_pilot_at(lock_f, pw);
  //
  // Nudge the timing by a sample either way if that correlates better.
  // The pilot is 160 samples long so this tracks slow clock drift without
  // a full re-acquisition.
  //
  double best = -1.0;
  int64_t best_a = lock_a;

  for (int64_t a = lock_a - 1; a <= lock_a + 1; a++) {
    if (a < 0 || a + RADE_CORR_M > ringtotal || ringtotal - a > RADE_RING) { continue; }

    double m = cabs2(rade_correlate(ring0, a, pw));

    if (m > best) {
      best = m;
      best_a = a;
    }
  }

  nudged = (best_a != lock_a);
  lock_a = best_a;
  cplx d0 = rade_correlate(ring0, lock_a, pw);
  cplx d1 = rade_correlate(ring1, lock_a, pw);
  double mag = sqrt(cabs2(d0));
  //
  // Correlation floor: the same span and the same pilot, but rotated to
  // frequencies far outside the lock range, where a real pilot cannot
  // contribute and noise and interference contribute exactly as much as
  // they do on frequency. See RADE_FLOOR_DF for why this is not probed
  // off-pilot in time instead.
  //
  // The ratio below is therefore dimensionless and independent of signal
  // level. It is not the acquisition statistic and its threshold does not
  // mean the same thing.
  //
  double fl = 0.0;
  int fn = 0;

  for (int k = 0; k < RADE_FLOOR_PROBES; k++) {
    static const double df[RADE_FLOOR_PROBES] = { -2.0, -1.0, 1.0, 2.0 };
    cplx pf[RADE_CORR_M];
    rade_pilot_at(lock_f + df[k] * RADE_FLOOR_DF, pf);
    fl += sqrt(cabs2(rade_correlate(ring0, lock_a, pf)));
    fn++;
  }

  if (fn > 0) { fl /= (double)fn; }

  if (mag_avg <= 0.0) {
    mag_avg = mag;
    floor_avg = fl;
    use_mag = mag;
    use_floor = fl;
  } else {
    mag_avg   += RADE_MAG_ALPHA * (mag - mag_avg);
    floor_avg += RADE_MAG_ALPHA * (fl - floor_avg);
    use_mag   += RADE_USE_ALPHA * (mag - use_mag);
    use_floor += RADE_USE_ALPHA * (fl - use_floor);
  }

  double ratio = (floor_avg > 1e-30) ? (mag_avg / floor_avg) : 0.0;
  double use_ratio = (use_floor > 1e-30) ? (use_mag / use_floor) : 0.0;

  if (probation > 0) {
    //
    // Confirming a candidate from the search. The same test the tracker
    // uses every frame, applied to the one cell the search picked - which
    // is all a confirmation needs to be, and costs one correlation and
    // four probes instead of another full blind search.
    //
    // No weight is produced while this runs, so a false alarm costs about
    // a second and never touches the combiner.
    //
    // The test is on the smoothed ratio at the end of the probation, not
    // on every frame. RADE_USE_ALPHA averages over about the same second,
    // so by the last frame it is an average of the whole confirmation -
    // and a real but weak signal that dips below the threshold in one
    // frame of eight should not be thrown away, which is what a per-frame
    // test would do.
    if (--probation > 0) { return 0; }

    if (use_ratio < RADE_USE_RATIO) {
      t_print("%s: candidate did not confirm (pilot/floor %0.2f over %d "
              "frames, need %0.1f), searching again\n", __func__, use_ratio,
              RADE_PROBATION, RADE_USE_RATIO);
      rade_corr_reset();
      return 0;
    }

    rade_corr_locked = 1;
    rade_corr_confirming = 0;
    t_print("%s: RADE pilot LOCK confirmed  modem %s carrier  f=%+0.1f Hz  "
            "pilot/floor %0.2f\n", __func__,
            lock_bank ? "above" : "below", lock_f, use_ratio);
    return 0;
  }

  //
  // Frequency tracking.
  //
  // The pilot correlation turns by 2*pi*df*T from one modem frame to the
  // next, T being 120 ms, so its phase advance measures the residual
  // frequency error directly and unambiguously over +/-4.17 Hz. That
  // covers both what acquisition leaves behind - half a 5 Hz grid step -
  // and any drift a station on frequency will ever show.
  //
  // Skipped when the timing moved, because a one-sample nudge rotates the
  // correlation on its own by more than any frequency error would.
  //
  if (prev_valid && !nudged) {
    cplx r = cmul(d0, cconj(prev_d0));
    //
    // Subtract the advance the tracked offset already accounts for.
    //
    // rade_pilot_at() rebuilds the reference from n = 0 every frame while
    // the received pilot advances with lock_a, so the raw phase step is
    // 2*pi*f*T for the *absolute* offset f, whatever lock_f already
    // holds. Used directly it made this an integrator with no error
    // signal in it: lock_f gained alpha*f per frame and walked away
    // instead of converging, which is what the on-air captures show it
    // doing (+20 Hz to +8 Hz over 25 s on one of them) until the matched
    // filter had drifted far enough off to cost the lock.
    //
    // With the expected advance removed this measures the residual, which
    // is what the note at RADE_FREQ_ALPHA has always claimed it measures
    // - and the +/-4.17 Hz unambiguous range is about lock_f rather than
    // about zero.
    //
    double dphi = atan2(r.im, r.re) - 2.0 * M_PI * lock_f * RADE_FRAME_SECS;
    dphi = remainder(dphi, 2.0 * M_PI);
    double df = dphi / (2.0 * M_PI * RADE_FRAME_SECS);
    lock_f += RADE_FREQ_ALPHA * df;

    if (lock_f >  RADE_FREQ_LIMIT) { lock_f =  RADE_FREQ_LIMIT; }

    if (lock_f < -RADE_FREQ_LIMIT) { lock_f = -RADE_FREQ_LIMIT; }

    rade_corr_freq_off = lock_f;
  }

  prev_d0 = d0;
  prev_valid = 1;

  if (use_ratio < RADE_USE_RATIO) {
    //
    // Nothing worth measuring in this frame. Keep the weight exactly
    // where it was - do not let noise move it - and start counting
    // towards the operator's hang time.
    //
    if (!frozen) {
      frozen = 1;
      t_print("%s: pilot lost, holding last weight (%+0.1f dB %+0.0f deg) "
              "for up to %0.0f s\n", __func__, div_gain, div_phase, hang);
    }

    //
    // At least one frame, whatever the caller passed: a hang shorter than
    // the second RADE_USE_ALPHA averages over would end a lock on a
    // single noisy frame, which is the failure the smoothing exists to
    // prevent.
    //
    int limit = (int)lround(hang / RADE_FRAME_SECS);

    if (limit < 1) { limit = 1; }

    if (++drop_count >= limit) {
      t_print("%s: lost RADE pilot lock (pilot/floor %0.2f, gone %0.1f s), "
              "searching again\n", __func__, ratio,
              drop_count * RADE_FRAME_SECS);
      rade_corr_reset();
    }

    return 0;
  }

  drop_count = 0;

  if (frozen) {
    frozen = 0;
    t_print("%s: pilot back, resuming\n", __func__);
  }

  //
  // Roughly every five seconds, so a lock that is holding can be seen to
  // be holding.
  //
  if (++track_report >= 40) {
    track_report = 0;
    t_print("%s: tracking  pilot/floor %0.2f  f=%+0.1f Hz  "
            "pilot %0.0f%% / %+0.1f dB  w=%+0.1f dB %+0.0f deg  "
            "avg=%0.1fs hang=%0.1fs%s\n",
            __func__, ratio, lock_f,
            100.0 * rade_corr_quality, rade_corr_snr, div_gain, div_phase,
            tau, hang, frozen ? "  FROZEN" : "");
  }
  //
  // The channel, as a cross-spectrum rather than as two coherent means.
  //
  // d1*conj(d0) and |d0|^2 are h1*conj(h0) and |h0|^2 up to one common
  // real scale, which is all div_mvdr2() needs. What that buys is that
  // both are invariant to a rotation the two arms share - and the pilot
  // correlation carries a large one, for the reason set out at the
  // frequency discriminator above: d0 turns by 2*pi*f*T from frame to
  // frame whatever f is.
  //
  // Averaging d0 and d1 coherently through that is averaging a spinning
  // phasor, and on air it showed: at the operator's 10.5 s averaging the
  // coherent part sat 16 to 28 dB below the per-frame |h0| and its phase
  // was dragged 36 to 51 degrees off. Worse, it degraded further the
  // longer the operator set Averaging, which is the opposite of what that
  // control promises. See docs/diversity-measurements.md.
  //
  cplx h0 = cscale(d0, 1.0 / pilot_energy);
  cplx x01 = cmul(d1, cconj(d0));
  double x00 = cabs2(d0);
  //
  // Interference covariance, from the bins the modem does not occupy.
  //
  // This used to be the residual x - h*pw over the pilot span, on the
  // reasoning that removing the wanted signal leaves the interference. It
  // does not. One scalar h is fitted across the whole symbol, so
  // everything else inside the decimator's +/-3 kHz view stays in the
  // residual - and on air that is dominated by whatever occupies the
  // *rejected* sideband, a station of comparable power that WDSP filters
  // away and the operator never hears.
  //
  // Measured against recorded captures on two bands the residual's
  // inter-arm coherence ran 0.61 to 0.80 where the true noise was 0.11 to
  // 0.49, with the phase wrong as well. MVDR did exactly what it was told
  // and steered its null onto that - and the null landed close to the
  // wanted signal's own inter-arm phase, so the combiner subtracted the
  // signal it was there to combine. Decode-scored, it cost 0.5 to 3.4 dB
  // against simply using the better antenna.
  //
  // So the covariance is measured where a pilot cannot contribute instead
  // - see RADE_GUARD_LO0. The guard bins are placed relative to lock_f so
  // the set follows the station, and mirrored for bank 1, whose carriers
  // are below the tuned frequency in this frame rather than above it.
  //
  double e0 = 0.0, e1 = 0.0;
  cplx e01 = cset(0.0, 0.0);
  const double gsign = (lock_bank == 0) ? 1.0 : -1.0;
  const double dbin = (double)RADE_CORR_FS / (double)RADE_CORR_M;

  for (int k = RADE_GUARD_LO0; k <= RADE_GUARD_HI1; k++) {
    if (k >= RADE_CARRIER_K0 && k <= RADE_CARRIER_K1) { continue; }

    const double hz = lock_f + gsign * (double)k * dbin;
    cplx g0 = rade_dft_bin(ring0, lock_a, hz);
    cplx g1 = rade_dft_bin(ring1, lock_a, hz);
    e0  += cabs2(g0);
    e1  += cabs2(g1);
    e01  = cadd(e01, cmul(g0, cconj(g1)));
  }

  e0 /= (double)RADE_GUARD_BINS;
  e1 /= (double)RADE_GUARD_BINS;
  e01 = cscale(e01, 1.0 / (double)RADE_GUARD_BINS);
  //
  // The pilot's own energy over the span, which is what the residual loop
  // used to accumulate a term at a time.
  //
  const double sigpow = cabs2(h0) * pilot_energy;
  //
  // Per modem frame, from the operator's averaging time.
  //
  double alpha = 1.0 - exp(-RADE_FRAME_SECS / (tau > 0.05 ? tau : 0.05));

  if (!acc_valid) { alpha = 1.0; }

  acc_valid = 1;
  acc_x01 = cadd(cscale(acc_x01, 1.0 - alpha), cscale(x01, alpha));
  acc_x00 += alpha * (x00 - acc_x00);
  acc_r00 += alpha * (e0 - acc_r00);
  acc_r11 += alpha * (e1 - acc_r11);
  acc_r01 = cadd(cscale(acc_r01, 1.0 - alpha), cscale(e01, alpha));
  acc_sig += alpha * (sigpow - acc_sig);

  if (acc_r00 > 1e-20 && acc_sig > 0.0) {
    rade_corr_snr = 10.0 * log10(acc_sig / acc_r00);
    //
    // Report the fraction of the span energy the pilot itself accounts
    // for, against the interference estimated off-carrier beside it. The
    // sigma statistic above is the right thing for the lock decision but
    // makes a poor display: a strong interferer inflates the
    // timing-domain floor it is measured against, so it pins to zero
    // while the correlator is in fact tracking perfectly well.
    //
    // Both this and rade_corr_snr read higher than they did before the
    // covariance moved off the pilot-span residual, and should: a station
    // in the rejected sideband is no longer counted as interference to
    // the one being received.
    //
    rade_corr_quality = acc_sig / (acc_sig + acc_r00);
  }

  rade_mvdr_weight(wr, wi);
  return 1;
}

int rade_corr_process(const float *arm0, const float *arm1, int n,
                      int expect_bank, double frame_off, double tau,
                      double hang, double *wr, double *wi) {
  if (!running) { return 0; }

  //
  // Shift so the tuned carrier sits at zero, decimate to 8 kHz, and push
  // into the ring. The NCO runs at the DDC rate; its phase is kept
  // between blocks so the rotation is continuous.
  //
  // The rotation advances by complex multiply rather than by calling
  // cos() and sin() per sample. At 384 kHz the per-sample form cost
  // 768 000 transcendental calls a second - one to two percent of a core
  // - for work that is six flops done this way. wdsp/shift.c does the
  // same thing for the same reason.
  //
  // The exact phase is still accumulated separately and the rotator
  // re-seeded from it at the top of every block, so the error inherent in
  // stepping a rotation cannot build up beyond one block.
  //
  //
  // The tuned signal sits at -frame_off in the tapped buffer, because the
  // buffer is inverted with respect to RF, so bringing it to zero means
  // rotating *up* by that much. See the frequency bookkeeping note in
  // diversity_auto.c.
  //
  const double dphi = 2.0 * M_PI * frame_off / (double)(decim * RADE_CORR_FS);
  const double cd = cos(dphi), sd = sin(dphi);
  double c = cos(nco_phase), s = sin(nco_phase);

  for (int i = 0; i < n; i++) {
    double i0 = arm0[2 * i], q0 = arm0[2 * i + 1];
    double i1 = arm1[2 * i], q1 = arm1[2 * i + 1];
    //
    // Shift into the frame where the tuned carrier sits at zero. Nothing
    // else: the spectral sense is handled by choosing a pilot bank, not
    // by touching the samples.
    //
    double r0 = i0 * c - q0 * s, m0 = i0 * s + q0 * c;
    double r1 = i1 * c - q1 * s, m1 = i1 * s + q1 * c;
    dline0[2 * dpos    ] = (float)r0;
    dline0[2 * dpos + 1] = (float)m0;
    dline1[2 * dpos    ] = (float)r1;
    dline1[2 * dpos + 1] = (float)m1;
    //
    // Step the rotator by exp(j*dphi).
    //
    double ct = c;
    c = ct * cd - s * sd;
    s = ct * sd + s * cd;
    dpos++;

    if (dpos >= ntaps) { dpos = 0; }

    if (++phase < decim) { continue; }

    phase = 0;
    //
    // One output sample: FIR over the delay line, newest first.
    //
    double a0r = 0.0, a0i = 0.0, a1r = 0.0, a1i = 0.0;
    int idx = dpos - 1;

    for (int k = 0; k < ntaps; k++) {
      if (idx < 0) { idx += ntaps; }

      double tk = taps[k];
      a0r += tk * dline0[2 * idx    ];
      a0i += tk * dline0[2 * idx + 1];
      a1r += tk * dline1[2 * idx    ];
      a1i += tk * dline1[2 * idx + 1];
      idx--;
    }

    ring0[2 * ringw    ] = (float)a0r;
    ring0[2 * ringw + 1] = (float)a0i;
    ring1[2 * ringw    ] = (float)a1r;
    ring1[2 * ringw + 1] = (float)a1i;
    ringw++;
    ringtotal++;

    if (ringw >= RADE_RING) { ringw = 0; }
  }

  //
  // Advance the exact phase by the whole block, so the next call re-seeds
  // the rotator from a value that has not drifted.
  //
  nco_phase = fmod(nco_phase + dphi * (double)n, 2.0 * M_PI);

  //
  // Do not look until the ring holds a full acquisition span.
  //
  if (ringtotal < RADE_ACQ_SPAN) { return 0; }

  if (!tracking) {
    //
    // Rate limit the search: it is by far the most expensive thing here,
    // and there is no point running it more than once per modem frame.
    //
    if (ringtotal < next_process) { return 0; }

    next_process = ringtotal + RADE_CORR_NMF;

    if (!rade_acquire(expect_bank)) { return 0; }

    //
    // A candidate. rade_track() follows it for RADE_PROBATION frames
    // before rade_corr_locked goes up and any weight comes out.
    //
    tracking = 1;
    rade_corr_confirming = 1;
    t_print("%s: RADE pilot candidate  modem %s carrier  a=%lld  f=%+0.1f Hz\n",
            __func__, lock_bank ? "above" : "below", (long long)lock_a, lock_f);
  }

  //
  // Locked: step the pilot forward one modem frame at a time and measure
  // each one as soon as the samples for it have arrived. A block can
  // carry more than one frame, so this loops.
  //
  int updated = 0;

  while (lock_a + RADE_CORR_M <= ringtotal) {
    if (ringtotal - lock_a > RADE_RING) {
      //
      // Fallen out of the ring - we were starved of CPU for long enough
      // to lose the thread of it. Start again.
      //
      t_print("%s: RADE pilot ran off the ring, re-acquiring\n", __func__);
      rade_corr_reset();
      return 0;
    }

    int ok = rade_track(tau, hang, wr, wi);

    //
    // Advance whatever happened. The pilot moves on by exactly one modem
    // frame every 120 ms regardless of whether we liked this one, and an
    // earlier version returned here without stepping - which pinned
    // lock_a while ringtotal kept growing, so a single marginal frame
    // ended the lock a second or two later with "ran off the ring".
    //
    lock_a += RADE_CORR_NMF;

    if (!tracking) {
      //
      // rade_track() gave up and reset us.
      //
      return 0;
    }

    if (ok) { updated = 1; }
  }

  return updated;
}
