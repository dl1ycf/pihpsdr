/*
 * The Digital I/Q reference: occupancy split, then MVDR.
 *
 * The claim being tested is not "it produces a weight" - test_modes_live
 * covers that - but that knowing the noise separately is worth something.
 * The wideband Sum objective, w = +Sxy/Sxx, is maximum ratio combining
 * only when the two branches carry equal, uncorrelated noise. On a real
 * station they do not: ADC1 is usually a small loop or a whip on a bare
 * rear-panel input, and both feedlines pick up the same common-mode hash.
 *
 * A digital signal is narrow and sits in a passband that is mostly empty,
 * so the empty part can be measured directly and used as the noise
 * covariance. These tests check that this helps where it should, does
 * nothing where it should not, and never invents a weight from noise.
 *
 * The signal is 2-FSK rather than a tone on purpose. A full-scale pure
 * tone leaks over the whole region even through a Blackman-Harris window,
 * and that leakage is correlated between the arms, so the "noise" bins
 * would carry a coherent copy of the signal and every result here would
 * be measuring window sidelobes.
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

static void settle(void) { g_usleep(15000); }

/* the channel from arm 0 to arm 1, as everywhere else in this suite */
static const double hr = 0.62, hi = -0.48;

#define RATE  48000
#define NFFT  4096

/*
 * A 45 baud, +/-85 Hz 2-FSK carrier at +1500 Hz - RTTY, near enough.
 *
 * Positive baseband frequencies with an LSB passband: the tapped buffer
 * is inverted with respect to RF, so that is the combination that puts
 * the analysis region on top of the signal. See the frequency
 * bookkeeping note in diversity_auto.c.
 */
static double fsk_phase = 0.0;
static int    fsk_count = 0;
static double fsk_freq  = 1585.0;

static void fsk_reset(void) {
  fsk_phase = 0.0;
  fsk_count = 0;
  fsk_freq = 1585.0;
}

static void fsk_next(double *re, double *im) {
  if (--fsk_count <= 0) {
    fsk_count = RATE / 45;
    fsk_freq = (rand() & 1) ? 1585.0 : 1415.0;
  }

  fsk_phase += 2.0 * M_PI * fsk_freq / (double)RATE;

  if (fsk_phase > 2.0 * M_PI) { fsk_phase -= 2.0 * M_PI; }

  *re = cos(fsk_phase);
  *im = sin(fsk_phase);
}

/*
 * Band-limited noise as a bank of random-phase tones. Used for an
 * interferer that has to sit inside the analysis region but clear of the
 * wanted signal, which white noise cannot do.
 */
#define QRM_TONES 48
static double qrm_ph[QRM_TONES], qrm_dph[QRM_TONES];

static void qrm_reset(double flo, double fhi) {
  for (int i = 0; i < QRM_TONES; i++) {
    const double f = flo + (fhi - flo) * (double)i / (double)(QRM_TONES - 1);
    qrm_ph[i] = 2.0 * M_PI * ((double)rand() / RAND_MAX);
    qrm_dph[i] = 2.0 * M_PI * f / (double)RATE;
  }
}

static void qrm_next(double *re, double *im) {
  double a = 0.0, b = 0.0;

  for (int i = 0; i < QRM_TONES; i++) {
    qrm_ph[i] += qrm_dph[i];
    a += cos(qrm_ph[i]);
    b += sin(qrm_ph[i]);
  }

  *re = a / sqrt((double)QRM_TONES);
  *im = b / sqrt((double)QRM_TONES);
}

/*
 * What the combiner would actually deliver, given a weight.
 *
 * y = z0 + w*z1 with z0 = s + n0 and z1 = h*s + n1, so the wanted power
 * is |1 + w*h|^2 * S and what rides along with it is n0 + w*n1. This is
 * the number the modes are being compared on: a weight that is "closer to
 * conj(h)" is not better if conj(h) is not the right answer.
 */
static double out_sinr_db(double wr, double wi, double sig,
                          double n0, double n1) {
  /* the wanted signal adds coherently through 1 + w*h */
  const double gr = 1.0 + (wr * hr - wi * hi);
  const double gi =       (wr * hi + wi * hr);
  const double s  = (gr * gr + gi * gi) * sig;
  /* the two branch noises are independent, so they add in power */
  const double n  = n0 + (wr * wr + wi * wi) * n1;
  return 10.0 * log10(s / (n + 1e-30));
}

/*
 * One run. Returns the weight the loop settled on.
 *
 * ref     - DIV_REF_DIGITAL_IQ or DIV_REF_BAND, so the two can be driven
 *           with byte-identical data and compared.
 * n0, n1  - independent noise amplitude on each arm
 * qrm     - band-limited correlated interferer amplitude, placed at
 *           +2100..+2700, inside the region and clear of the signal
 * fill    - 1 to replace the FSK with a signal that covers the whole
 *           region, so occupancy has nothing to find
 */
static void run(int ref, int obj, double n0, double n1, double qrm, int fill,
                double *wr, double *wi) {
  rx0.sample_rate = RATE;
  rx0.filter_low = -2800;
  rx0.filter_high = -200;
  vfo[0].mode = modeLSB;
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  vfo[0].offset = 0;
  div_auto_ref = ref;
  div_auto_mode = obj;
  div_auto_follow_filter = 1;
  div_auto_tau = 3.0;
  div_auto_coherence_min = 0.30;
  div_auto_weighting = DIV_WEIGHT_COHERENCE;
  div_auto_resolution = 12.0;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  srand(17);
  fsk_reset();
  qrm_reset(2100.0, 2700.0);
  diversity_auto_start();

  for (int b = 0; b < 140; b++) {
    for (int n = 0; n < NFFT; n++) {
      double sr, si;

      if (fill) {
        /*
         * Noise-like and correlated between the arms, spread across the
         * whole region: the "filter set snugly round the signal" case,
         * where the median is the signal and occupancy finds nothing.
         */
        qrm_next(&sr, &si);
      } else {
        fsk_next(&sr, &si);
      }

      double qr = 0.0, qi = 0.0;

      if (qrm > 0.0) { qrm_next(&qr, &qi); }

      /* arm 0 */
      const double a0r = sr + qrm * qr + n0 * frand();
      const double a0i = si + qrm * qi + n0 * frand();
      /* arm 1: the same sources through the same channel, its own noise */
      const double cr = sr + qrm * qr;
      const double ci = si + qrm * qi;
      const double a1r = hr * cr - hi * ci + n1 * frand();
      const double a1i = hr * ci + hi * cr + n1 * frand();
      diversity_auto_sample(a0r, a0i, a1r, a1i);
    }

    settle();
  }

  g_usleep(400000);
  /* read before stopping: the weight lives in the engine's globals */
  const double mag = pow(10.0, div_gain / 20.0);
  const double ph = div_phase * M_PI / 180.0;
  *wr = mag * cos(ph);
  *wi = mag * sin(ph);
  diversity_auto_stop();
}

static void wdb(double wr, double wi, double *g, double *p) {
  *g = 20.0 * log10(sqrt(wr * wr + wi * wi) + 1e-30);
  *p = atan2(wi, wr) * 180.0 / M_PI;
}

int main(int argc, char **argv) {
  verbose = (argc > 1);
  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0;
  memset(vfo, 0, sizeof(vfo));
  int fails = 0;
  /* what conj(h) is, which is where Sum should land with equal noise */
  const double want_g = 20.0 * log10(hypot(hr, hi));
  const double want_p = atan2(-hi, hr) * 180.0 / M_PI;
  printf("Digital I/Q reference: occupancy split and MVDR\n");
  printf("channel h = %.2f%+.2fj, conj(h) = %+.2f dB %+.1f deg\n\n",
         hr, hi, want_g, want_p);
  /* ---------------------------------------------------------------- */
  /* 1. equal, uncorrelated branch noise: MVDR must reduce to Sum      */
  /* ---------------------------------------------------------------- */
  {
    double dr, di, br, bi, dg, dp, bg, bp;
    run(DIV_REF_DIGITAL_IQ, DIV_AUTO_SUM, 0.05, 0.05, 0.0, 0, &dr, &di);
    run(DIV_REF_BAND,       DIV_AUTO_SUM, 0.05, 0.05, 0.0, 0, &br, &bi);
    wdb(dr, di, &dg, &dp);
    wdb(br, bi, &bg, &bp);
    /*
     * Against the wideband reference on identical data rather than
     * against conj(h): the point is that the extra machinery changes
     * nothing when there is nothing for it to know.
     */
    const int ok = (fabs(dg - bg) < 0.5) && (fabs(dp - bp) < 3.0);
    printf("1. equal noise, MVDR degenerates to Sum\n");
    printf("   digital %+6.2f dB %+6.1f deg   window %+6.2f dB %+6.1f deg   %s\n\n",
           dg, dp, bg, bp, ok ? "OK" : "FAIL");

    if (!ok) { fails++; }
  }
  /* ---------------------------------------------------------------- */
  /* 2. arm 1 ten times noisier: MVDR should back it off, Sum cannot   */
  /* ---------------------------------------------------------------- */
  {
    double dr, di, br, bi, dg, dp, bg, bp;
    const double n0 = 0.03, n1 = 0.30;
    run(DIV_REF_DIGITAL_IQ, DIV_AUTO_SUM, n0, n1, 0.0, 0, &dr, &di);
    run(DIV_REF_BAND,       DIV_AUTO_SUM, n0, n1, 0.0, 0, &br, &bi);
    wdb(dr, di, &dg, &dp);
    wdb(br, bi, &bg, &bp);
    const double ds = out_sinr_db(dr, di, 1.0, n0 * n0, n1 * n1);
    const double bs = out_sinr_db(br, bi, 1.0, n0 * n0, n1 * n1);
    /*
     * Sum estimates conj(h) whatever arm 1's noise is. The SINR-optimal
     * weight is conj(h)*N0/N1, which with a 20 dB noise power ratio is
     * -42 dB - past the engine's own -27 dB floor, so the digital answer
     * sits on that clamp. Being clamped short of the optimum still leaves
     * it far quieter than Sum, and the SINR is what is asserted.
     */
    const int ok = (ds > bs + 1.0) && (dg < bg - 3.0);
    printf("2. arm 1 noisier by %.0f dB, MVDR backs it off (optimum %+.0f dB, clamped at -27)\n",
           20.0 * log10(n1 / n0), want_g + 40.0 * log10(n0 / n1));
    printf("   digital %+6.2f dB -> SINR %+6.2f dB\n", dg, ds);
    printf("   window  %+6.2f dB -> SINR %+6.2f dB   %s\n\n", bg, bs,
           ok ? "OK" : "FAIL");

    if (!ok) { fails++; }
  }
  /* ---------------------------------------------------------------- */
  /* 3. correlated interferer in the empty part of the region          */
  /* ---------------------------------------------------------------- */
  {
    double dr, di, br, bi;
    run(DIV_REF_DIGITAL_IQ, DIV_AUTO_SUM, 0.03, 0.03, 0.60, 0, &dr, &di);
    run(DIV_REF_BAND,       DIV_AUTO_SUM, 0.03, 0.03, 0.60, 0, &br, &bi);
    /*
     * The interferer reaches both arms through the same channel as the
     * signal, so no weight can null it and hold the signal - that is the
     * limit this mode has, and it is documented. What is being checked is
     * narrower and is the thing that would actually break: the wideband
     * reference weights every coherent bin, so the interferer - which is
     * the stronger of the two - takes the answer over. Occupancy keeps
     * the digital answer on the signal.
     */
    double dg, dp, bg, bp;
    wdb(dr, di, &dg, &dp);
    wdb(br, bi, &bg, &bp);
    const int ok = (fabs(dp - want_p) < 6.0);
    printf("3. correlated interferer at +2100..2700, signal at +1500\n");
    printf("   digital %+6.2f dB %+6.1f deg   window %+6.2f dB %+6.1f deg\n",
           dg, dp, bg, bp);
    printf("   digital stays on the signal (want %+.1f deg)   %s\n\n",
           want_p, ok ? "OK" : "FAIL");

    if (!ok) { fails++; }
  }
  /* ---------------------------------------------------------------- */
  /* 4. the occupied span, and which way round the frame is            */
  /* ---------------------------------------------------------------- */
  {
    double wr, wi;
    run(DIV_REF_DIGITAL_IQ, DIV_AUTO_SUM, 0.05, 0.05, 0.0, 0, &wr, &wi);
    /*
     * The signal is built at +1500 Hz in the tapped buffer, which is
     * -1500 Hz in the shifted frame the controls and the panadapter use.
     * Getting this sign wrong is the mistake this code has made twice, so
     * it is asserted rather than assumed.
     */
    const double mid = 0.5 * (div_auto_occ_lo + div_auto_occ_hi);
    const double wid = div_auto_occ_hi - div_auto_occ_lo;
    const int ok = div_auto_occ_valid && fabs(mid + 1500.0) < 120.0
                   && wid > 80.0 && wid < 900.0;
    printf("4. occupancy span\n");
    printf("   %+.0f .. %+.0f Hz, centre %+.0f, width %.0f (want centre -1500)  %s\n\n",
           div_auto_occ_lo, div_auto_occ_hi, mid, wid, ok ? "OK" : "FAIL");

    if (!ok) { fails++; }
  }
  /* ---------------------------------------------------------------- */
  /* 5. noise only: no weight, at all                                  */
  /* ---------------------------------------------------------------- */
  {
    double g, p;
    rx0.sample_rate = RATE;
    div_cos = 1.0;
    div_sin = 0.0;
    div_gain = 0.0;
    div_phase = 0.0;
    /* no signal on either arm, and the two arms uncorrelated */
    div_auto_ref = DIV_REF_DIGITAL_IQ;
    div_auto_mode = DIV_AUTO_SUM;
    div_auto_follow_filter = 1;
    div_auto_tau = 3.0;
    div_auto_coherence_min = 0.30;
    div_auto_resolution = 12.0;
    rx0.filter_low = -2800;
    rx0.filter_high = -200;
    vfo[0].mode = modeLSB;
    srand(31);
    diversity_auto_start();

    for (int b = 0; b < 140; b++) {
      for (int n = 0; n < NFFT; n++) {
        diversity_auto_sample(frand(), frand(), frand(), frand());
      }

      settle();
    }

    g_usleep(400000);
    g = div_gain;
    p = div_phase;
    diversity_auto_stop();
    /*
     * The suite's negative-case thresholds. This one matters more than
     * any other here: a weight conjured out of noise is applied to the
     * whole passband, not just to the region it was invented in.
     */
    const int moved = (fabs(g) > 0.5) || (fabs(p) > 5.0);
    printf("5. noise only, no signal\n");
    printf("   gain %+6.2f dB  phase %+6.1f deg  coherence %3.0f%%  %s\n\n",
           g, p, 100.0 * div_auto_coherence, moved ? "FAIL - moved" : "OK");

    if (moved) { fails++; }
  }
  /* ---------------------------------------------------------------- */
  /* 6. signal fills the region: occupancy finds nothing to narrow to  */
  /* ---------------------------------------------------------------- */
  {
    double dr, di, dg, dp;
    run(DIV_REF_DIGITAL_IQ, DIV_AUTO_SUM, 0.05, 0.05, 0.0, 1, &dr, &di);
    wdb(dr, di, &dg, &dp);
    /*
     * A filter set snugly round the signal. The median is then the signal
     * and nothing clears it, so the mode has to notice that the region is
     * full rather than empty and fall back to plain maximum ratio
     * combining - which is conj(h).
     */
    const int ok = (fabs(dg - want_g) < 1.0) && (fabs(dp - want_p) < 5.0);
    printf("6. signal fills the region, falls back to MRC\n");
    printf("   %+6.2f dB %+6.1f deg (want %+.2f dB %+.1f deg)   %s\n\n",
           dg, dp, want_g, want_p, ok ? "OK" : "FAIL");

    if (!ok) { fails++; }
  }
  /* ---------------------------------------------------------------- */
  /* 7. a strong signal stops: how long before the loop stops tracking  */
  /* ---------------------------------------------------------------- */
  {
    /*
     * The accumulators forget exponentially at div_auto_tau, but the
     * occupancy test is a ratio against the median floor and so is scale
     * invariant - it does not notice the level collapsing at all. The
     * coherence gate does not catch it either: Sxy, Sxx and Syy all decay
     * together, so gamma^2 stays near 1 the whole way down.
     *
     * Left alone, a 30 dB signal at tau = 2 s therefore keeps the loop
     * "tracking" for 5.8 tau - about twelve seconds - while what it is
     * actually measuring is noise, and the weight degrades over that time.
     * On air this is every gap between transmissions.
     */
    rx0.sample_rate = RATE;
    rx0.filter_low = -2800;
    rx0.filter_high = -200;
    vfo[0].mode = modeLSB;
    vfo[0].frequency = 7100000;
    vfo[0].ctun_frequency = 7100000;
    vfo[0].offset = 0;
    div_auto_ref = DIV_REF_DIGITAL_IQ;
    div_auto_mode = DIV_AUTO_SUM;
    div_auto_follow_filter = 1;
    div_auto_tau = 2.0;
    div_auto_coherence_min = 0.30;
    div_auto_resolution = 12.0;
    div_cos = 1.0;
    div_sin = 0.0;
    div_gain = 0.0;
    div_phase = 0.0;
    srand(17);
    fsk_reset();
    diversity_auto_start();
    /* a signal 30 dB out of the noise, run to convergence */
    const double nz = 0.03;

    for (int b = 0; b < 90; b++) {
      for (int n = 0; n < NFFT; n++) {
        double sr, si;
        fsk_next(&sr, &si);
        const double a0r = sr + nz * frand(), a0i = si + nz * frand();
        const double a1r = hr * sr - hi * si + nz * frand();
        const double a1i = hr * si + hi * sr + nz * frand();
        diversity_auto_sample(a0r, a0i, a1r, a1i);
      }

      settle();
    }

    g_usleep(300000);
    const double g0 = div_gain, p0 = div_phase;
    /* the signal stops dead; nothing but noise from here */
    const double blockms = 1000.0 * (double)NFFT / (double)RATE;
    int held = -1;

    for (int b = 0; b < 200; b++) {
      for (int n = 0; n < NFFT; n++) {
        diversity_auto_sample(nz * frand(), nz * frand(),
                              nz * frand(), nz * frand());
      }

      settle();
      g_usleep(3000);

      if (held < 0 && div_auto_holding) { held = b + 1; }
    }

    g_usleep(300000);
    const double drift_g = fabs(div_gain - g0);
    double drift_p = div_phase - p0;

    while (drift_p >  180.0) { drift_p -= 360.0; }

    while (drift_p < -180.0) { drift_p += 360.0; }

    drift_p = fabs(drift_p);
    diversity_auto_stop();
    const double secs = (held < 0) ? -1.0 : held * blockms / 1000.0;
    /*
     * Two seconds is the budget: long enough not to trip on ordinary
     * fading, short enough that a gap between transmissions costs
     * nothing. The drift is the thing that actually matters - a loop
     * that holds late has spent that time walking the weight away from
     * the answer it had.
     */
    const int ok = (held > 0) && (secs < 2.0) && (drift_g < 0.5) && (drift_p < 5.0);
    printf("7. strong signal stops, tau %.0f s\n", div_auto_tau);

    if (held < 0) {
      printf("   never stopped tracking in %.1f s\n", 200 * blockms / 1000.0);
    } else {
      printf("   held after %.2f s (%d blocks)\n", secs, held);
    }

    printf("   weight drift %.2f dB %.1f deg   %s\n\n",
           drift_g, drift_p, ok ? "OK" : "FAIL");

    if (!ok) { fails++; }
  }
  printf("%s\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
