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

#include <string.h>

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
//        |  NCO shift by +offset, so the tuned carrier lands at 0 -
//        |  the same frame WDSP works in after xshift()
//        v
//   [ conjugate, if lower sideband ]
//        |
//        v
//   polyphase FIR decimator, DDC rate -> 8000 Hz
//        |
//        v
//   ring buffer -> pilot correlation over (timing, frequency)
//        |
//        v
//   h0, h1  and the residual covariance Rnn  ->  MVDR weight
//
// ----------------------------------------------------------------------
// Sideband, and why it is measured rather than derived
// ----------------------------------------------------------------------
//
// RADE is received through an SSB passband, so the modem occupies
// 750..2200 Hz on one side of the tuned carrier and the mirror image of
// that on the other. Which way round depends on the sideband in use.
//
// An earlier version of this decided that from vfo[].mode and conjugated
// the input for the lower-sideband modes. It never acquired on air. The
// convention relating the raw DDC stream to the frame WDSP works in
// after xshift() is genuinely difficult to pin down by reading: the
// direction implied by xshift() and the direction implied by the sign of
// the USB and LSB filter edges do not agree, and guessing wrong is
// completely silent - the correlator simply looks at the mirror image of
// the signal and finds nothing, for ever.
//
// So it is no longer derived. Correlating a mirrored stream against the
// pilot is the same as correlating the original stream against a
// *mirrored pilot*, and conj(p) is exactly the pilot with its carriers
// reflected about zero. Acquisition therefore searches both pilot banks
// over the same decimated stream and keeps whichever actually correlates,
// at twice the acquisition cost and no extra front end.
//
// That also removes the conjugation from the sample path entirely, so
// there is no longer a weight to conjugate back: the channel estimates
// come from the real, untouched arms whichever bank wins, and the MVDR
// solution applies to them directly.
//
// The detected sense is reported in the UI, which is the only way we get
// to find out what the convention actually is.
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
// a few thousand cells, so 6 to acquire and 4 to hold sit either side of
// "could plausibly be noise".
//
#define RADE_LOCK_SIGMA     6.0
#define RADE_LOCK_FRAMES    3
#define RADE_DROP_FRAMES    84      // ~10 s: ride out fades, not absences

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
// should force a re-acquisition, so the hold time is measured in tens of
// seconds.
//
// 1.35 is chosen well below what a working lock shows: with a strong
// in-band interferer inflating the floor, the synthetic case still holds
// 1.9 to 2.2. A ratio of 1.0 would mean the pilot correlates no better
// than the data symbols do, i.e. no detectable pilot at all. Erring low
// is right - holding a stale weight for a few extra seconds after a
// station stops is harmless, dropping during a fade is not.
//
#define RADE_HOLD_RATIO     1.35    // pilot / floor, below which we are unhappy
#define RADE_MAG_ALPHA      0.02    // ~6 s at 8.33 modem frames/s
#define RADE_FLOOR_PROBES   4

//
// Timing cells within this many samples of the peak are excluded from the
// floor estimate, so the pilot does not contaminate its own reference.
//
#define RADE_FLOOR_GUARD    12

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
// Forgetting factor for the channel and covariance estimates, per modem
// frame (120 ms). 0.08 gives roughly a 1.5 s time constant, which sits
// under the fading rate we are trying to track.
//
#define RADE_ALPHA          0.08

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

typedef struct {
  double re, im;
} cplx;

static inline cplx cset(double r, double i)      { cplx c = {r, i}; return c; }
static inline cplx cadd(cplx a, cplx b)          { return cset(a.re + b.re, a.im + b.im); }
static inline cplx csub(cplx a, cplx b)          { return cset(a.re - b.re, a.im - b.im); }
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
static long   ringtotal = 0;         // total samples ever written

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
static long   lock_a = 0;            // absolute sample index of the pilot
static double lock_f = 0.0;          // Hz
static int    lock_count = 0;
static int    drop_count = 0;
static long   next_process = 0;      // ringtotal at which to look again

#define RADE_ACQ_NCELL  (RADE_CORR_NMF / RADE_ACQ_TSTEP)

static double acq_grid[2][RADE_ACQ_NCELL][RADE_ACQ_NFREQ];
static int    acq_passes = 0;
static int    lock_bank = 0;      // which pilot bank actually correlates

static double mag_avg = 0.0;         // smoothed pilot correlation
static double floor_avg = 0.0;       // smoothed off-pilot correlation
static int    track_report = 0;

static cplx   acc_h0, acc_h1;
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
  rade_corr_quality = 0.0;
}

void rade_corr_reset(void) {
  rade_corr_locked = 0;
  lock_count = 0;
  drop_count = 0;
  lock_a = 0;
  lock_bank = 0;
  lock_f = 0.0;
  acc_h0 = acc_h1 = cset(0.0, 0.0);
  acc_r01 = cset(0.0, 0.0);
  acc_r00 = acc_r11 = 0.0;
  acc_sig = 0.0;
  acc_valid = 0;
  mag_avg = 0.0;
  floor_avg = 0.0;
  track_report = 0;
  rade_corr_quality = 0.0;
  rade_corr_snr = 0.0;
  next_process = 0;
  memset(acq_grid, 0, sizeof(acq_grid));
  acq_passes = 0;
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
static inline cplx ring_get(const float *r, long a) {
  int idx = (int)(a % RADE_RING);

  return cset(r[2 * idx], r[2 * idx + 1]);
}

//
// Correlate one arm against the pilot starting at absolute index a.
// Returns sum rx * conj(p_w), which is h * pilot_energy.
//
static cplx rade_correlate(const float *r, long a, const cplx *pw) {
  cplx acc = cset(0.0, 0.0);

  for (int n = 0; n < RADE_CORR_M; n++) {
    acc = cadd(acc, cmul(ring_get(r, a + n), cconj(pw[n])));
  }

  return acc;
}


//
// Coarse then fine search for the pilot on arm 0.
//
static int rade_acquire(int lsb_hint) {
  long best_a = 0;
  int best_f = 0;
  //
  // The latest pilot pair we could be looking at ends here.
  //
  const long limit = ringtotal - RADE_CORR_NMF - RADE_CORR_M;

  if (limit < RADE_CORR_NMF) { return 0; }

  //
  // The grid is indexed by absolute sample index modulo one modem frame,
  // not by position within the buffer. That keeps a given pilot in a
  // fixed cell from pass to pass, which is what makes accumulating across
  // passes meaningful.
  //
  for (int cell = 0; cell < RADE_ACQ_NCELL; cell++) {
    long phase_off = (long)cell * RADE_ACQ_TSTEP;
    long a = limit - ((limit - phase_off) % RADE_CORR_NMF);

    if (a - RADE_CORR_NMF < 0 || ringtotal - (a - RADE_CORR_NMF) > RADE_RING) { continue; }

    for (int bank = 0; bank < 2; bank++) {
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

  if (++acq_passes < RADE_ACQ_PASSES) {
    //
    // Keep integrating. Report progress so the UI does not look stalled.
    //
    rade_corr_quality = 0.0;
    return 0;
  }

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
  int best_bank = 0;

  for (int bank = 0; bank < 2; bank++) {
    for (int f = 0; f < RADE_ACQ_NFREQ; f++) {
      int pk = 0;

      for (int cell = 1; cell < RADE_ACQ_NCELL; cell++) {
        if (acq_grid[bank][cell][f] > acq_grid[bank][pk][f]) { pk = cell; }
      }

      double sum = 0.0, sum2 = 0.0;
      long n = 0;

      for (int cell = 0; cell < RADE_ACQ_NCELL; cell++) {
        int d = cell - pk;

        if (d < 0) { d = -d; }

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

      if (sf > stat_bank[bank]) { stat_bank[bank] = sf; }

      if (sf > stat) {
        stat = sf;
        best_bank = bank;
        best_f = f;
        best_a = limit - ((limit - (long)pk * RADE_ACQ_TSTEP) % RADE_CORR_NMF);
      }
    }
  }

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
  t_print("%s: acq normal=%0.2f mirrored=%0.2f best=%0.2f (need %0.1f) "
          "f=%+0.1f rms=%0.2e %s\n",
          __func__, stat_bank[0], stat_bank[1], stat, RADE_LOCK_SIGMA,
          acq_freq[best_f], rms, lsb_hint ? "mode=LSB" : "mode=USB");

#ifdef RADE_DEBUG_STAT
  t_print("ACQ: stat=%.2f f=%0.1f\n", stat, acq_freq[best_f]);
#endif

  rade_corr_quality = RADE_STAT_TO_Q(stat);

  if (rade_corr_quality < 0.0) { rade_corr_quality = 0.0; }

  if (rade_corr_quality > 1.0) { rade_corr_quality = 1.0; }

  if (stat < RADE_LOCK_SIGMA) {
    //
    // Not there yet. Start a fresh integration rather than carrying a
    // stale grid forward.
    //
    memset(acq_grid, 0, sizeof(acq_grid));
    acq_passes = 0;
    lock_count = 0;
    return 0;
  }

  //
  // Refine the timing to the sample, around the coarse cell.
  //
  double fine_best = -1.0;
  long fine_a = best_a;

  for (long a = best_a - RADE_ACQ_TREFINE; a <= best_a + RADE_ACQ_TREFINE; a++) {
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

  if (++lock_count < RADE_LOCK_FRAMES) { return 0; }

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
// For R = [[r00, r01], [conj(r01), r11]] and h = [h0, h1]:
//
//   g0 = (r11*h0 - r01*h1)      / det
//   g1 = (r00*h1 - conj(r01)*h0) / det
//
// The combiner forms y = g^H z, so the arm 1 weight relative to arm 0 is
// conj(g1/g0). With R diagonal and equal this reduces to conj(h1/h0),
// which is the maximum ratio combining answer - so this degenerates
// gracefully to the same thing the wideband "Sum" mode does when there is
// no correlated interference to null.
//
static void rade_mvdr_weight(double *wr, double *wi) {
  cplx r01 = acc_r01;
  double r00 = acc_r00;
  double r11 = acc_r11;
  //
  // Diagonal loading. Without it, a noise covariance that is nearly
  // singular - two arms seeing almost identical noise - produces an
  // enormous weight from what is mostly estimation error.
  //
  double load = 0.01 * (r00 + r11) + 1e-20;
  r00 += load;
  r11 += load;
  cplx num = csub(cscale(acc_h1, r00), cmul(cconj(r01), acc_h0));
  cplx den = csub(cscale(acc_h0, r11), cmul(r01, acc_h1));
  double d2 = cabs2(den);

  if (d2 < 1e-30) {
    *wr = 0.0;
    *wi = 0.0;
    return;
  }

  //
  // num/den, then conjugate for the g^H combining sense.
  //
  //
  // num/den, then conjugate for the g^H combining sense. No sideband
  // correction: the samples were never conjugated, so whichever pilot
  // bank won, h0 and h1 describe the real arms directly.
  //
  cplx q = cscale(cmul(num, cconj(den)), 1.0 / d2);
  q = cconj(q);
  *wr = q.re;
  *wi = q.im;
}

//
// Once locked, measure the channel on both arms at the tracked timing and
// frequency, update the covariance of what is left over, and solve.
//
static int rade_track(double *wr, double *wi) {
  cplx pw[RADE_CORR_M];
  rade_pilot_at(lock_f, pw);
  //
  // Nudge the timing by a sample either way if that correlates better.
  // The pilot is 160 samples long so this tracks slow clock drift without
  // a full re-acquisition.
  //
  double best = -1.0;
  long best_a = lock_a;

  for (long a = lock_a - 1; a <= lock_a + 1; a++) {
    if (a < 0 || a + RADE_CORR_M > ringtotal || ringtotal - a > RADE_RING) { continue; }

    double m = cabs2(rade_correlate(ring0, a, pw));

    if (m > best) {
      best = m;
      best_a = a;
    }
  }

  lock_a = best_a;
  cplx d0 = rade_correlate(ring0, lock_a, pw);
  cplx d1 = rade_correlate(ring1, lock_a, pw);
  //
  // Estimate the correlation floor from positions inside the data
  // symbols, where there is no pilot. Same statistic as acquisition uses,
  // so the lock and drop thresholds mean the same thing in both.
  //
  double mag = sqrt(cabs2(d0));
  //
  // Correlation floor from positions inside the data symbols, where by
  // construction there is no pilot. Same span, same template, so the
  // ratio below is dimensionless and independent of signal level.
  //
  double fl = 0.0;
  int fn = 0;

  for (int k = 1; k <= RADE_FLOOR_PROBES; k++) {
    long a = lock_a + (long)k * (RADE_CORR_NMF / (RADE_FLOOR_PROBES + 1));

    if (a + RADE_CORR_M > ringtotal) { continue; }

    fl += sqrt(cabs2(rade_correlate(ring0, a, pw)));
    fn++;
  }

  if (fn > 0) { fl /= (double)fn; }

  if (mag_avg <= 0.0) {
    mag_avg = mag;
    floor_avg = fl;
  } else {
    mag_avg   += RADE_MAG_ALPHA * (mag - mag_avg);
    floor_avg += RADE_MAG_ALPHA * (fl - floor_avg);
  }

  double ratio = (floor_avg > 1e-30) ? (mag_avg / floor_avg) : 0.0;

  if (ratio < RADE_HOLD_RATIO) {
    if (++drop_count >= RADE_DROP_FRAMES) {
      t_print("%s: lost RADE pilot lock (pilot/floor %0.2f for %0.0f s)\n",
              __func__, ratio, (double)RADE_DROP_FRAMES / 8.33);
      rade_corr_reset();
      return 0;
    }
  } else {
    drop_count = 0;
  }

  //
  // Roughly every five seconds, so a lock that is holding can be seen to
  // be holding.
  //
  if (++track_report >= 40) {
    track_report = 0;
    t_print("%s: tracking  pilot/floor %0.2f (drop below %0.1f)  f=%+0.1f Hz  "
            "pilot %0.0f%% / %+0.1f dB  w=%+0.1f dB %+0.0f deg\n",
            __func__, ratio, RADE_HOLD_RATIO, lock_f,
            100.0 * rade_corr_quality, rade_corr_snr, div_gain, div_phase);
  }
  //
  // h = correlation / pilot energy
  //
  cplx h0 = cscale(d0, 1.0 / pilot_energy);
  cplx h1 = cscale(d1, 1.0 / pilot_energy);
  //
  // Residual over the pilot span: whatever is not the wanted signal.
  // This is the whole point of using the pilot - it separates the RADE
  // signal from noise and QRM, so the covariance below describes the
  // interference alone and the solution nulls it instead of the signal.
  //
  double e0 = 0.0, e1 = 0.0;
  cplx e01 = cset(0.0, 0.0);
  double sigpow = 0.0;

  for (int n = 0; n < RADE_CORR_M; n++) {
    cplx x0 = ring_get(ring0, lock_a + n);
    cplx x1 = ring_get(ring1, lock_a + n);
    cplx ref = pw[n];
    cplx r0 = csub(x0, cmul(h0, ref));
    cplx r1 = csub(x1, cmul(h1, ref));
    e0  += cabs2(r0);
    e1  += cabs2(r1);
    e01  = cadd(e01, cmul(r0, cconj(r1)));
    sigpow += cabs2(cmul(h0, ref));
  }

  double alpha = acc_valid ? RADE_ALPHA : 1.0;
  acc_valid = 1;
  acc_h0 = cadd(cscale(acc_h0, 1.0 - alpha), cscale(h0, alpha));
  acc_h1 = cadd(cscale(acc_h1, 1.0 - alpha), cscale(h1, alpha));
  acc_r00 += alpha * (e0 - acc_r00);
  acc_r11 += alpha * (e1 - acc_r11);
  acc_r01 = cadd(cscale(acc_r01, 1.0 - alpha), cscale(e01, alpha));
  acc_sig += alpha * (sigpow - acc_sig);

  if (acc_r00 > 1e-20 && acc_sig > 0.0) {
    rade_corr_snr = 10.0 * log10(acc_sig / acc_r00);
    //
    // Report the fraction of the pilot-span energy the pilot itself
    // accounts for. The sigma statistic above is the right thing for the
    // lock decision but makes a poor display: a strong interferer inflates
    // the timing-domain floor it is measured against, so it pins to zero
    // while the correlator is in fact tracking perfectly well.
    //
    rade_corr_quality = acc_sig / (acc_sig + acc_r00);
  }

  rade_mvdr_weight(wr, wi);
  return 1;
}

int rade_corr_process(const float *arm0, const float *arm1, int n,
                      int lsb, double offset_hz, double *wr, double *wi) {
  if (!running) { return 0; }

  //
  // Shift so the tuned carrier sits at zero, decimate to 8 kHz, and push
  // into the ring. The NCO runs at the DDC rate; its phase is kept
  // between blocks so the rotation is continuous.
  //
  const double dphi = 2.0 * M_PI * offset_hz / (double)(decim * RADE_CORR_FS);

  for (int i = 0; i < n; i++) {
    double c = cos(nco_phase), s = sin(nco_phase);
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
    nco_phase += dphi;

    if (nco_phase >  M_PI) { nco_phase -= 2.0 * M_PI; }

    if (nco_phase < -M_PI) { nco_phase += 2.0 * M_PI; }

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
  // Do not look until the ring holds a full acquisition span.
  //
  if (ringtotal < RADE_ACQ_SPAN) { return 0; }

  if (!rade_corr_locked) {
    //
    // Rate limit the search: it is by far the most expensive thing here,
    // and there is no point running it more than once per modem frame.
    //
    if (ringtotal < next_process) { return 0; }

    next_process = ringtotal + RADE_CORR_NMF;

    if (!rade_acquire(lsb)) { return 0; }

    rade_corr_locked = 1;
    t_print("%s: RADE pilot LOCK  bank=%s  a=%ld  f=%+0.1f Hz  (mode says %s)\n",
            __func__, lock_bank ? "mirrored" : "normal", lock_a, lock_f,
            lsb ? "LSB" : "USB");
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

    int ok = rade_track(wr, wi);

    //
    // Advance whatever happened. The pilot moves on by exactly one modem
    // frame every 120 ms regardless of whether we liked this one, and an
    // earlier version returned here without stepping - which pinned
    // lock_a while ringtotal kept growing, so a single marginal frame
    // ended the lock a second or two later with "ran off the ring".
    //
    lock_a += RADE_CORR_NMF;

    if (!rade_corr_locked) {
      //
      // rade_track() gave up and reset us.
      //
      return 0;
    }

    if (ok) { updated = 1; }
  }

  return updated;
}
