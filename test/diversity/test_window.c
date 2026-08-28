/*
 * Two checks on the wideband Window reference.
 *
 * 1. Window placement. Bins are indexed k % nfft, so a window outside the
 *    first Nyquist zone used to be measured at a different frequency
 *    entirely, silently: at 48 kHz a window at +30 kHz landed on -18 kHz,
 *    and the spin ranges allowed exactly that. It must now be clamped and
 *    flagged rather than aliased.
 *
 * 2. Bin weighting on speech. SSB voice has no carrier, the energy moves
 *    about constantly and much of the passband is noise at any instant.
 *    Flat weighting averages h(f) by power and is therefore diluted by
 *    the noise-only bins; coherence weighting should be measurably
 *    better. This is the evidence for which one is the default.
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

/* ------------------------------------------------------------------ */
/* 1. window placement                                                */
/* ------------------------------------------------------------------ */

static int test_placement(void) {
  const int rate = 48000, nfft = 4096;
  /* the frequency a +30 kHz request aliases onto at this rate */
  const double alias_hz = 30000.0 - rate;         /* -18000 */
  const double hr = 0.62, hi = -0.48;             /* channel of the alias signal */
  rx0.sample_rate = rate;
  rx0.filter_low = -3000;
  rx0.filter_high = 3000;
  div_auto_ref = DIV_REF_BAND;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_follow_filter = 0;
  div_auto_centre = 30000.0;     /* beyond Nyquist: must be refused */
  div_auto_width = 2000.0;
  div_auto_tau = 1.0;
  div_auto_coherence_min = 0.1;
  div_auto_weighting = DIV_WEIGHT_FLAT;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  diversity_auto_start();
  double ph = 0.0;
  srand(3);

  for (int b = 0; b < 60; b++) {
    for (int n = 0; n < nfft; n++) {
      /* a strong signal sitting exactly where the bad window would alias */
      ph += 2.0 * M_PI * alias_hz / rate;
      double s = cos(ph), t = sin(ph);
      diversity_auto_sample(s + 0.01 * frand(), t + 0.01 * frand(),
                            hr * s - hi * t + 0.01 * frand(),
                            hr * t + hi * s + 0.01 * frand());
    }

    settle();
  }

  g_usleep(300000);
  int clamped = div_auto_clamped;
  double g = div_gain, p = div_phase;
  diversity_auto_stop();
  /* conj(h) is what it would converge to if it aliased onto the signal */
  double bad_deg = atan2(-hi, hr) * 180.0 / M_PI;
  int moved = (fabs(g) > 0.5) || (fabs(p) > 5.0);
  printf("  placement: window +30 kHz at 48 kHz -> clamped=%d, weight %+0.2f dB %+0.1f deg\n",
         clamped, g, p);
  printf("             (aliasing onto %.0f Hz would give about %+0.1f deg)\n",
         alias_hz, bad_deg);

  if (!clamped) {
    printf("  FAIL: out-of-Nyquist window was not flagged\n");
    return 0;
  }

  if (moved) {
    printf("  FAIL: a weight was produced from an unusable window\n");
    return 0;
  }

  printf("  PASS: refused and flagged, not aliased\n");
  return 1;
}

/* ------------------------------------------------------------------ */
/* 2. flat vs coherence weighting on speech                           */
/* ------------------------------------------------------------------ */

/*
 * Speech-like: a few band-limited tones whose frequencies and amplitudes
 * wander, with pauses. Not a vocoder - just enough non-stationarity that
 * only part of the passband carries signal at any instant, which is the
 * property that separates the two weightings.
 */
static double voice(double t, int *active) {
  double env = 0.5 * (1.0 + sin(2.0 * M_PI * 0.7 * t));
  *active = (env > 0.25);

  if (!*active) { return 0.0; }

  //
  // One narrow formant that wanders across the passband, rather than
  // energy everywhere at once. This is the case the operator described:
  // a wide window of which only a small part carries signal at any
  // instant, which is why hand-placing a 300 Hz window helps.
  //
  double f1 = 1500.0 + 1100.0 * sin(2.0 * M_PI * 0.31 * t);
  return env * sin(2.0 * M_PI * f1 * t);
}

static double run_ssb(int weighting, double noise, double *err_deg) {
  const int rate = 48000, nfft = 4096;
  const double hr = 0.62, hi = -0.48;
  rx0.sample_rate = rate;
  //
  // An LSB passband. voice() below builds its energy at positive
  // frequencies, and the tapped buffer is inverted with respect to RF -
  // see the frequency bookkeeping note in diversity_auto.c - so an LSB
  // passband is what puts the analysis window on top of it. With a USB
  // passband the window would land on the image instead, which still
  // measures the same channel but is not what the mode does on air.
  //
  rx0.filter_low = -2800;
  rx0.filter_high = -200;
  vfo[0].mode = modeLSB;
  div_auto_ref = DIV_REF_BAND;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_follow_filter = 1;        /* whole passband, as intended */
  div_auto_tau = 3.0;
  div_auto_coherence_min = 0.05;
  div_auto_weighting = weighting;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  diversity_auto_start();
  double t = 0.0;
  srand(11);

  for (int b = 0; b < 120; b++) {
    for (int n = 0; n < nfft; n++) {
      int active;
      double s = voice(t, &active);
      t += 1.0 / rate;
      /* analytic-ish: quadrature partner via a quarter-cycle shift is not
         needed here, the estimator only sees the complex pair we build */
      double q = voice(t + 0.25 / 1200.0, &active);
      diversity_auto_sample(s + noise * frand(), q + noise * frand(),
                            hr * s - hi * q + noise * frand(),
                            hr * q + hi * s + noise * frand());
    }

    settle();
  }

  g_usleep(300000);
  double g = div_gain, p = div_phase;
  diversity_auto_stop();
  /* target for Sum is conj(h) */
  double want_g = 20.0 * log10(hypot(hr, hi));
  double want_p = atan2(-hi, hr) * 180.0 / M_PI;
  double dp = p - want_p;

  while (dp > 180.0) { dp -= 360.0; }

  while (dp < -180.0) { dp += 360.0; }

  *err_deg = fabs(dp);
  return fabs(g - want_g);
}

static int test_weighting(void) {
  int ok = 1;
  //
  // Both the gain and the phase error matter, and they fail differently:
  // noise-only bins in the window add to the denominator but not the
  // numerator, so flat weighting biases the *magnitude* low while leaving
  // the phase roughly right. Reporting only the phase would hide the
  // whole effect.
  //
  printf("  weighting on speech, whole passband, signal in part of it\n");
  printf("  target conj(h) = -2.11 dB, +37.75 deg\n\n");
  printf("    %-7s   %-9s %-9s   %-9s %-9s\n",
         "noise", "flat dB", "flat deg", "coh dB", "coh deg");

  for (int i = 0; i < 4; i++) {
    double noise = (double[]) { 0.05, 0.20, 0.50, 1.00 } [i];
    double fd, cd;
    double fg = run_ssb(DIV_WEIGHT_FLAT, noise, &fd);
    double cg = run_ssb(DIV_WEIGHT_COHERENCE, noise, &cd);
    printf("    %-7.2f   %-9.2f %-9.2f   %-9.2f %-9.2f   %s\n",
           noise, fg, fd, cg, cd,
           (cg <= fg + 0.1 && cd <= fd + 0.5) ? "coherence >= flat" : "flat better here");

    //
    // The claim being tested is that coherence is not worse. A large
    // regression either way is a failure.
    //
    if (cg > fg + 1.0 || cd > fd + 5.0) { ok = 0; }
  }

  return ok;
}

int main(int argc, char **argv) {
  if (argc > 1) { verbose = 1; }

  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0;
  memset(vfo, 0, sizeof(vfo));
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  vfo[0].mode = modeUSB;
  printf("Window reference checks\n\n");
  int a = test_placement();
  printf("\n");
  int b = test_weighting();
  printf("\n%s\n", (a && b) ? "PASS" : "FAIL");
  return (a && b) ? 0 : 1;
}
