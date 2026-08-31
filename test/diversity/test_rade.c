/*
 * RADE V1 correlator: acquisition time, sideband, and the raw-to-shifted
 * frame conversion.
 *
 * Everything here runs through diversity_auto.c rather than calling the
 * correlator directly, because two of the three things being checked are
 * decided there: which pilot bank the operator's sideband implies, and
 * the offset between the raw DDC frame and the frame the passband is
 * expressed in.
 *
 * 1. Acquisition time. It used to be fixed at 11.5 s for every signal,
 *    however strong, because a lock needed three consecutive full 32-pass
 *    searches. It is now a progressive search followed by a cheap
 *    confirmation, so a strong signal should be well under half of that.
 *
 * 2. Sideband. The operator's passband names the pilot bank and is the
 *    only one searched: an LSB passband looks below the tuned frequency
 *    and a USB one above it, and a modem on the other side is not found
 *    at all - a lock outside the passband is of no use to this mode. The
 *    green analysis window on the panadapter is drawn from the same
 *    answer, which is how the operator noticed it was wrong.
 *
 * 3. CTUN. With a non-zero vfo offset the tuned signal is not at zero in
 *    the raw stream, and the correlator has to shift by the right amount
 *    in the right direction to find it. The sign was wrong here, and
 *    nothing acquired under CTUN at all.
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
#include "rade_correlator.h"

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
 * The engine tells the menu when a mode change swapped one block of modal
 * settings for another. There is no menu here.
 */
gboolean diversity_menu_settings_changed(gpointer data) { (void)data; return G_SOURCE_REMOVE; }

static double frand(void) { return 2.0 * ((double)rand() / RAND_MAX) - 1.0; }

#define RATE   48000
#define NFFT   4096
#define DECIM  (RATE / RADE_CORR_FS)
#define FRAMES 30                       /* modem frames in the replay buffer */

/*
 * A continuous RADE-like stream as it arrives in the tapped buffer: pilot
 * symbol then RADE_CORR_NS data symbols, repeating, at the DDC rate.
 *
 * side is where the modem is in *RF* terms - -1 below the dial, as an LSB
 * passband gives, +1 above it. The buffer is inverted with respect to RF
 * (see the frequency bookkeeping note in diversity_auto.c), so below the
 * dial is the un-mirrored, positive-frequency case here and above it is
 * the mirrored one. Getting this backwards is exactly the mistake the
 * on-air logs corrected, so it is spelled out rather than assumed.
 *
 * off_hz is vfo[0].offset: CTUN puts the tuned point at RF dial + off_hz,
 * which is -off_hz in the buffer.
 *
 * The buffer is a whole number of modem frames long so replaying it end
 * to end keeps the pilot timing valid across the wrap.
 */
static long gen_h(float **buf, int side, double off_hz, double noise,
                  double hr, double hi) {
  const long nsym = (long)FRAMES * (RADE_CORR_NS + 1);
  const long n8   = (long)FRAMES * RADE_CORR_NMF;
  const long nd   = n8 * DECIM;
  const double rs = (double)RADE_CORR_FS / RADE_CORR_M;
  const int c1 = (int)lround((1500.0 - rs * RADE_CORR_NC / 2.0) / rs);
  static const double barker13[13] = { 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1 };
  double *s8 = malloc(sizeof(double) * 2 * n8);
  *buf = malloc(sizeof(float) * 4 * nd);
  long idx = 0;

  for (long sym = 0; sym < nsym; sym++) {
    int pilot = ((sym % (RADE_CORR_NS + 1)) == 0);
    double re[RADE_CORR_M], im[RADE_CORR_M];
    memset(re, 0, sizeof(re));
    memset(im, 0, sizeof(im));

    for (int c = 0; c < RADE_CORR_NC; c++) {
      double w = 2.0 * M_PI * (c1 + c) / RADE_CORR_M;
      double a = pilot ? sqrt(2.0) * barker13[c % 13] : ((rand() & 1) ? 1.0 : -1.0);
      double b = pilot ? 0.0 : ((rand() & 1) ? 1.0 : -1.0);

      for (int n = 0; n < RADE_CORR_M; n++) {
        double th = w * n;
        re[n] += (a * cos(th) - b * sin(th)) / RADE_CORR_M;
        im[n] += (a * sin(th) + b * cos(th)) / RADE_CORR_M;
      }
    }

    for (int n = 0; n < RADE_CORR_NCP; n++) {
      s8[2 * idx] = re[RADE_CORR_M - RADE_CORR_NCP + n];
      s8[2 * idx + 1] = im[RADE_CORR_M - RADE_CORR_NCP + n];
      idx++;
    }

    for (int n = 0; n < RADE_CORR_M; n++) {
      s8[2 * idx] = re[n];
      s8[2 * idx + 1] = im[n];
      idx++;
    }
  }

  for (long i = 0; i < nd; i++) {
    double sr = s8[2 * (i / DECIM)];
    double si = s8[2 * (i / DECIM) + 1];

    //
    // Above the dial is the mirror image in this buffer: conj() reflects
    // every carrier about zero.
    //
    if (side > 0) { si = -si; }

    //
    // ... and the tuned point sits at -off_hz.
    //
    double th = -2.0 * M_PI * off_hz * (double)i / (double)RATE;
    double c = cos(th), s = sin(th);
    double tr = sr * c - si * s;
    double ti = sr * s + si * c;
    (*buf)[4 * i + 0] = (float)(tr + noise * frand());
    (*buf)[4 * i + 1] = (float)(ti + noise * frand());
    (*buf)[4 * i + 2] = (float)(hr * tr - hi * ti + noise * frand());
    (*buf)[4 * i + 3] = (float)(hr * ti + hi * tr + noise * frand());
  }

  free(s8);
  return nd;
}

/*
 * The channel every check but the roundtable one uses.
 */
static long gen(float **buf, int side, double off_hz, double noise) {
  return gen_h(buf, side, off_hz, noise, 0.62, -0.48);
}

/*
 * Lock, then keep feeding so a weight is actually produced.
 *
 * run() below stops the moment rade_corr_locked goes true, which is
 * before the correlator has emitted anything: reading div_gain there
 * gives the value it was initialised to, not an answer. Anything checking
 * the weight has to carry on past the lock.
 *
 * Returns the block at which it locked, or -1.
 */
static int run_settled(int obj, double *g, double *p);

/*
 * Run until locked or out of patience. Returns the number of blocks fed,
 * or -1 if it never locked. One block is NFFT samples, 85.3 ms.
 */
static int run(int side, int pb, double off_hz, double noise, int maxblocks, int seed) {
  rx0.sample_rate = RATE;

  if (pb < 0) {
    rx0.filter_low = -2800;
    rx0.filter_high = -200;
    vfo[0].mode = modeDIGL;
  } else {
    rx0.filter_low = 200;
    rx0.filter_high = 2800;
    vfo[0].mode = modeDIGU;
  }

  vfo[0].frequency = 7100000;
  vfo[0].offset = (long long)llround(off_hz);
  vfo[0].ctun_frequency = vfo[0].frequency + vfo[0].offset;
  div_auto_ref = DIV_REF_RADE_V1;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_tau = 2.0;
  div_auto_coherence_min = 0.1;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  srand(seed);
  float *buf = NULL;
  long pos = 0;
  long nd = gen(&buf, side, off_hz, noise);
  diversity_auto_start();
  int blocks = -1;

  for (int b = 0; b < maxblocks; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                            buf[4 * pos + 2], buf[4 * pos + 3]);
      pos = (pos + 1) % nd;
    }

    //
    // Leave the worker room to drain: feeding flat out overruns the queue
    // and a dropped block makes the correlator start again.
    //
    g_usleep(12000);

    if (rade_corr_locked) {
      blocks = b + 1;
      break;
    }
  }

  g_usleep(200000);
  diversity_auto_stop();
  free(buf);
  return blocks;
}

static int run_settled(int obj, double *g, double *p) {
  rx0.sample_rate = RATE;
  rx0.filter_low = -2800;
  rx0.filter_high = -200;
  vfo[0].mode = modeDIGL;
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  vfo[0].offset = 0;
  div_auto_ref = DIV_REF_RADE_V1;
  div_auto_mode = obj;
  div_auto_tau = 2.0;
  div_auto_coherence_min = 0.1;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  srand(7);
  float *buf = NULL;
  long pos = 0;
  long nd = gen(&buf, -1, 0.0, 0.01);
  diversity_auto_start();
  int blocks = -1;

  for (int b = 0; b < 160; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                            buf[4 * pos + 2], buf[4 * pos + 3]);
      pos = (pos + 1) % nd;
    }

    g_usleep(12000);

    if (blocks < 0 && rade_corr_locked) { blocks = b + 1; }
  }

  g_usleep(300000);
  *g = div_gain;
  *p = div_phase;
  diversity_auto_stop();
  free(buf);
  return blocks;
}

static double secs(int blocks) {
  return (double)blocks * (double)NFFT / (double)RATE;
}

/*
 * Two stations taking turns on one frequency.
 *
 * Station A is locked and tracked to a settled weight, then stops dead
 * and station B starts - different data, half a modem frame away in
 * timing, and a different channel, so the cell A was locked to holds
 * nothing but B's data symbols. That is the case the Hang control is
 * for: until the lock on A is given up, B is being combined with A's
 * gain and phase, and each station has its own right answer.
 *
 * Reports the block the lock was dropped at, the block it re-locked on B
 * at, and the weight at each end. The weight is expected to sit still at
 * A's answer for the whole of the hang - that is the documented
 * hold-through-a-fade behaviour, and the hang is the only thing deciding
 * how long a station that is not fading but gone gets the benefit of it.
 */
struct rt_result {
  int  lock_a;        /* block A locked at */
  int  drop;          /* blocks after the switch that the lock was dropped */
  int  relock;        /* blocks after the switch that B was locked */
  double ga, pa;      /* settled weight on A */
  double gb, pb;      /* settled weight on B */
  double kick_g, kick_p;     /* movement while the freeze gate catches up */
  double drift_g, drift_p;   /* movement over the rest of the hang */
};

/*
 * How far apart two weights are, as a complex distance. Comparing dB and
 * degrees separately needs a tolerance on each and gets awkward when the
 * magnitude is small; this is one number and it is the one that matters.
 */
static double wdist(double g1, double p1, double g2, double p2) {
  double m1 = pow(10.0, g1 / 20.0), t1 = p1 * M_PI / 180.0;
  double m2 = pow(10.0, g2 / 20.0), t2 = p2 * M_PI / 180.0;
  return hypot(m1 * cos(t1) - m2 * cos(t2), m1 * sin(t1) - m2 * sin(t2));
}

/*
 * What one station on its own settles to, so the roundtable result can be
 * compared against a measurement of the same code on the same signal
 * rather than against the textbook conj(h). MVDR does not return exactly
 * conj(h) at finite SNR and the departure grows with |h|, so a theory
 * target would need a tolerance wide enough to be worth little.
 */
static int solo(double hr, double hi, int seed, double *g, double *p) {
  rx0.sample_rate = RATE;
  rx0.filter_low = -2800;
  rx0.filter_high = -200;
  vfo[0].mode = modeDIGL;
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  vfo[0].offset = 0;
  div_auto_ref = DIV_REF_RADE_V1;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_tau = 2.0;
  div_auto_hang = 10.0;
  div_auto_coherence_min = 0.1;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  float *buf = NULL;
  srand(seed);
  long nd = gen_h(&buf, -1, 0.0, 0.01, hr, hi);
  long pos = 0;
  diversity_auto_start();
  int blocks = -1;

  for (int b = 0; b < 240; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                            buf[4 * pos + 2], buf[4 * pos + 3]);
      pos = (pos + 1) % nd;
    }

    g_usleep(12000);

    if (blocks < 0 && rade_corr_locked) { blocks = b + 1; }
  }

  g_usleep(300000);
  *g = div_gain;
  *p = div_phase;
  diversity_auto_stop();
  free(buf);
  return blocks;
}

static int roundtable(double hang, struct rt_result *r) {
  rx0.sample_rate = RATE;
  rx0.filter_low = -2800;
  rx0.filter_high = -200;
  vfo[0].mode = modeDIGL;
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  vfo[0].offset = 0;
  div_auto_ref = DIV_REF_RADE_V1;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_tau = 2.0;
  div_auto_hang = hang;
  div_auto_coherence_min = 0.1;
  div_cos = 1.0;
  div_sin = 0.0;
  div_gain = 0.0;
  div_phase = 0.0;
  memset(r, 0, sizeof(*r));
  r->lock_a = r->drop = r->relock = -1;
  //
  // Two independent signals: separate srand() calls give them different
  // data symbols as well as different channels.
  //
  float *bufa = NULL, *bufb = NULL;
  srand(7);
  long nda = gen_h(&bufa, -1, 0.0, 0.01,  0.62, -0.48);
  srand(23);
  long ndb = gen_h(&bufb, -1, 0.0, 0.01,  1.30,  0.35);
  //
  // Half a modem frame, so B's pilot cannot land where A's was: the
  // floor guard only excludes 12 samples either side of the peak.
  //
  long pos = 0, posb = (RADE_CORR_NMF * DECIM) / 2;
  diversity_auto_start();

  for (int b = 0; b < 400; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(bufa[4 * pos + 0], bufa[4 * pos + 1],
                            bufa[4 * pos + 2], bufa[4 * pos + 3]);
      pos = (pos + 1) % nda;
    }

    g_usleep(12000);

    if (rade_corr_locked) { r->lock_a = b; break; }
  }

  if (r->lock_a < 0) { goto done; }

  //
  // Settle on A.
  //
  for (int b = 0; b < 80; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(bufa[4 * pos + 0], bufa[4 * pos + 1],
                            bufa[4 * pos + 2], bufa[4 * pos + 3]);
      pos = (pos + 1) % nda;
    }

    g_usleep(12000);
  }

  g_usleep(200000);
  r->ga = div_gain;
  r->pa = div_phase;
  //
  // A stops, B starts. 700 blocks is a minute, comfortably past the
  // longest hang the slider offers.
  //
  // The freeze gate averages over about a second before it accepts that
  // the pilot has gone, and until it does the accumulators are still
  // being fed. On a signal that simply stops that costs almost nothing,
  // because what they are being fed is noise. Here they are being fed a
  // *different strong station* whose data symbols correlate against the
  // pilot rather well, so the weight gets a real kick before the gate
  // engages. Measure that separately from the freeze it is testing:
  // GATE blocks in, whatever the gate was going to do it has done.
  //
  const int GATE = 24;                  /* ~2 s */
  double base_g = r->ga, base_p = r->pa;
  double kick_g = r->ga, kick_p = r->pa;
  double worst_g = 0.0, worst_p = 0.0;

  for (int b = 0; b < 700; b++) {
    for (int n = 0; n < NFFT; n++) {
      diversity_auto_sample(bufb[4 * posb + 0], bufb[4 * posb + 1],
                            bufb[4 * posb + 2], bufb[4 * posb + 3]);
      posb = (posb + 1) % ndb;
    }

    g_usleep(12000);

    if (r->drop < 0) {
      if (b < GATE) {
        if (fabs(div_gain - r->ga) > fabs(kick_g - r->ga)) { kick_g = div_gain; }

        if (fabs(div_phase - r->pa) > fabs(kick_p - r->pa)) { kick_p = div_phase; }

        base_g = div_gain;
        base_p = div_phase;
      } else {
        //
        // Frozen from here on: nothing should move it until the hang
        // runs out and the lock is given up.
        //
        if (fabs(div_gain - base_g) > fabs(worst_g)) { worst_g = div_gain - base_g; }

        if (fabs(div_phase - base_p) > fabs(worst_p)) { worst_p = div_phase - base_p; }
      }

      if (!rade_corr_locked) { r->drop = b + 1; }
    } else if (r->relock < 0) {
      if (rade_corr_locked) { r->relock = b + 1; }
    } else if (b > r->relock + 160) {
      //
      // 160 blocks is 13.7 s, and it is that long because of the alias
      // resolver. After a re-lock the loop can be sitting a whole modem
      // frame rate off the station - which is what acquisition hands it
      // about half the time - and the resolver needs RADE_ALIAS_MIN
      // frames to see that, plus a couple of averaging times for the
      // accumulators to follow it across. Measured at the 80 blocks this
      // used to wait, the weight is still mid-correction: +4.87 dB
      // against the +1.94 dB it settles to. See Finding 15 in
      // docs/diversity-measurements.md.
      //
      break;
    }
  }

  r->kick_g = kick_g - r->ga;
  r->kick_p = kick_p - r->pa;
  r->drift_g = worst_g;
  r->drift_p = worst_p;
  g_usleep(200000);
  r->gb = div_gain;
  r->pb = div_phase;
done:
  diversity_auto_stop();
  free(bufa);
  free(bufb);
  div_auto_hang = 10.0;
  return r->relock;
}

int main(int argc, char **argv) {
  if (argc > 1) { verbose = 1; }

  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0;
  memset(vfo, 0, sizeof(vfo));
  int ok = 1;
  printf("RADE V1 correlator checks\n\n");
  //
  // The old fixed cost was 3 x 32 x 120 ms = 11.5 s before a lock could
  // be declared on any signal at all. Anything at or above that here has
  // regressed to the old scheme.
  //
  const double was = 11.5;
  //
  // sig is where the modem actually is; pb is the sideband the operator
  // has selected. The last two have them disagreeing, which must not
  // produce a lock: that modem is outside the passband.
  //
  struct {
    const char *name; int sig; int pb; double off; int want; int max;
  } cases[] = {
    { "USB passband",         +1, +1,     0.0, 1, 120 },
    { "LSB passband",         -1, -1,     0.0, 1, 120 },
    { "LSB, CTUN +5 kHz",     -1, -1,  5000.0, 1, 120 },
    { "USB, CTUN -3 kHz",     +1, +1, -3000.0, 1, 120 },
    { "modem above LSB pb",   +1, -1,     0.0, 0, 120 },
    { "modem below USB pb",   -1, +1,     0.0, 0, 120 },
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    int b = run(cases[i].sig, cases[i].pb, cases[i].off, 0.01, cases[i].max, 7 + i);

    if (!cases[i].want) {
      printf("  %-20s  %s\n", cases[i].name,
             (b < 0) ? "not found, as it should not be   OK"
             : "LOCKED OUTSIDE THE PASSBAND   FAIL");

      if (b >= 0) { ok = 0; }

      continue;
    }

    if (b < 0) {
      printf("  %-20s  DID NOT LOCK in %0.1f s   FAIL\n",
             cases[i].name, secs(cases[i].max));
      ok = 0;
      continue;
    }

    int want_mirror = (cases[i].pb > 0);
    int side_ok = (rade_corr_mirrored == want_mirror);
    printf("  %-20s  locked in %5.2f s (%2d blocks)  modem %s carrier  %s\n",
           cases[i].name, secs(b), b,
           rade_corr_mirrored ? "above" : "below",
           side_ok ? (secs(b) < was ? "OK" : "SLOW") : "WRONG SIDEBAND");

    if (!side_ok || secs(b) >= was) { ok = 0; }
  }

  //
  // The objective has to reach this mode too.
  //
  // The correlator only ever solves for the weight that maximises the
  // pilot's SINR, and for a long time that answer was applied whatever
  // the operator had selected. Null and the Invert button therefore did
  // nothing here that lasted: diversity_auto_invert() turned div_cos and
  // div_sin over at once, so the audio changed, and then the next block
  // wrote the un-inverted answer back and slewed straight to it.
  //
  printf("\n");
  {
    double sg, sp, ng, np;
    int b1 = run_settled(DIV_AUTO_SUM,  &sg, &sp);
    int b2 = run_settled(DIV_AUTO_NULL, &ng, &np);
    //
    // Same magnitude, 180 degrees apart: MVDR has one answer and Null is
    // that answer turned over, which is what cancels the signal the pilot
    // is pointing at.
    //
    double d = np - sp;

    while (d >  180.0) { d -= 360.0; }

    while (d < -180.0) { d += 360.0; }

    const int locked = (b1 > 0) && (b2 > 0);
    const int good = locked && fabs(fabs(d) - 180.0) < 5.0 && fabs(ng - sg) < 0.5;
    printf("  Sum  %+0.2f dB %+0.1f deg\n", sg, sp);
    printf("  Null %+0.2f dB %+0.1f deg  (%.0f deg apart)   %s\n",
           ng, np, fabs(d),
           !locked ? "FAIL - did not lock" : (good ? "OK" : "FAIL"));

    if (!good) { ok = 0; }
  }
  //
  // Noise only. A false lock steers the null onto the wanted signal, so
  // this matters more than any of the above.
  //
  printf("\n");
  {
    rx0.sample_rate = RATE;
    rx0.filter_low = -2800;
    rx0.filter_high = -200;
    vfo[0].mode = modeDIGL;
    vfo[0].frequency = 7100000;
    vfo[0].ctun_frequency = 7100000;
    vfo[0].offset = 0;
    div_auto_ref = DIV_REF_RADE_V1;
    div_auto_mode = DIV_AUTO_SUM;
    div_auto_tau = 2.0;
    div_cos = 1.0;
    div_sin = 0.0;
    //
    // Zeroed here as well, not just div_cos/div_sin: this check reads
    // div_gain and div_phase to assert the weight did not move, so
    // whatever the previous check left in them would be read as movement.
    //
    div_gain = 0.0;
    div_phase = 0.0;
    srand(99);
    diversity_auto_start();

    for (int b = 0; b < 150; b++) {
      for (int n = 0; n < NFFT; n++) {
        diversity_auto_sample(frand(), frand(), frand(), frand());
      }

      g_usleep(12000);
    }

    g_usleep(200000);
    int locked = rade_corr_locked;
    double g = div_gain, p = div_phase;
    diversity_auto_stop();
    printf("  noise only, %0.1f s        %s   weight %+0.2f dB %+0.1f deg\n",
           secs(150), locked ? "LOCKED - FAIL" : "no lock  OK", g, p);

    if (locked) { ok = 0; }

    if (fabs(g) > 0.5 || fabs(p) > 5.0) {
      printf("  FAIL: the weight moved with no signal present\n");
      ok = 0;
    }
  }
  //
  // The reported sideband, and with it the green overlay on the
  // panadapter, has to follow the operator's passband - with a signal
  // present and without one. It was once chosen by comparing the energy
  // on the two sides, which on a signal near the noise floor, or on no
  // signal at all, is a coin toss: the overlay could sit above an LSB
  // passband indefinitely.
  //
  // This was written against the wideband RADE passband reference, which
  // has since been retired in favour of Digital I/Q. The property is the
  // same one and DIV_REF_RADE_V1 sets div_rade_side the same way, from
  // div_rade_side_expected(), whether or not it has locked - so the check
  // moved to V1 rather than being dropped with the mode.
  //
  printf("\n");
  {
    struct { const char *name; int side; int sig; } wb[] = {
      { "LSB passband, signal",  -1, 1 },
      { "LSB passband, noise",   -1, 0 },
      { "USB passband, signal",  +1, 1 },
      { "USB passband, noise",   +1, 0 },
    };

    for (unsigned i = 0; i < sizeof(wb) / sizeof(wb[0]); i++) {
      rx0.sample_rate = RATE;

      if (wb[i].side < 0) {
        rx0.filter_low = -2800;
        rx0.filter_high = -200;
        vfo[0].mode = modeDIGL;
      } else {
        rx0.filter_low = 200;
        rx0.filter_high = 2800;
        vfo[0].mode = modeDIGU;
      }

      vfo[0].frequency = 7100000;
      vfo[0].ctun_frequency = 7100000;
      vfo[0].offset = 0;
      div_auto_ref = DIV_REF_RADE_V1;
      div_auto_mode = DIV_AUTO_SUM;
      div_auto_tau = 1.0;
      div_auto_coherence_min = 0.1;
      div_cos = 1.0;
      div_sin = 0.0;
      srand(31 + i);
      float *buf = NULL;
      long pos = 0;
      long nd = gen(&buf, wb[i].side, 0.0, 0.05);
      diversity_auto_start();

      for (int b = 0; b < 40; b++) {
        for (int n = 0; n < NFFT; n++) {
          if (wb[i].sig) {
            diversity_auto_sample(buf[4 * pos + 0], buf[4 * pos + 1],
                                  buf[4 * pos + 2], buf[4 * pos + 3]);
          } else {
            diversity_auto_sample(frand(), frand(), frand(), frand());
          }

          pos = (pos + 1) % nd;
        }

        g_usleep(12000);
      }

      g_usleep(200000);
      int got = div_rade_side_get();
      diversity_auto_stop();
      free(buf);
      printf("  side   %-22s -> %s   %s\n", wb[i].name,
             (got < 0) ? "below" : "above",
             (got == wb[i].side) ? "OK" : "WRONG SIDE");

      if (got != wb[i].side) { ok = 0; }
    }
  }
  //
  // Two stations taking turns, and the Hang control that decides how long
  // the first one's weight outlives it.
  //
  printf("\n");
  {
    struct rt_result fast, slow;
    int r1 = roundtable(2.0,  &fast);
    int r2 = roundtable(10.0, &slow);

    if (r1 < 0 || r2 < 0) {
      printf("  roundtable: did not re-lock on the second station   FAIL\n");
      ok = 0;
    } else {
      //
      // The hang is counted off a gate that averages over about a second,
      // so expect the setting plus roughly that, plus the time to search
      // again from nothing. What must hold is that the setting is what
      // dominates: ten seconds has to cost about eight more than two.
      //
      const double t1 = secs(fast.drop), t2 = secs(slow.drop);
      const double want = 10.0 - 2.0;
      const int scales = fabs((t2 - t1) - want) < 2.0;
      printf("  hang  2 s: dropped after %5.2f s, re-locked %5.2f s later\n",
             t1, secs(fast.relock - fast.drop));
      printf("  hang 10 s: dropped after %5.2f s, re-locked %5.2f s later\n",
             t2, secs(slow.relock - slow.drop));
      printf("  the setting is what decides (%+0.2f s for +8 s of hang)   %s\n",
             t2 - t1, scales ? "OK" : "FAIL");

      if (!scales) { ok = 0; }

      //
      // Held, not tracked, once the freeze gate has engaged. This is the
      // fade behaviour the hang exists to preserve, and it is what makes
      // a short setting safe: nothing is being learned from the wrong
      // station meanwhile.
      //
      // The kick before the gate engages is reported but not asserted on.
      // It is the second's worth of averaging RADE_USE_ALPHA needs to
      // notice, and here it is being fed another station's data symbols
      // rather than the noise a signal that simply stopped would leave -
      // which correlate against the pilot well enough to move the weight.
      // It costs nothing that lasts, because the re-lock that follows
      // starts the estimate again from nothing.
      //
      const int held = fabs(slow.drift_g) < 0.5 && fabs(slow.drift_p) < 5.0;
      printf("  gate engaging:  weight kicked %+0.2f dB %+0.1f deg\n",
             slow.kick_g, slow.kick_p);
      printf("  then held for the rest of the hang: %+0.2f dB %+0.1f deg   %s\n",
             slow.drift_g, slow.drift_p, held ? "OK" : "FAIL");

      if (!held) { ok = 0; }

      //
      // And the point of all of it: the weight ends up on the second
      // station's channel and not the first one's. Measured against what
      // B on its own settles to, so this does not turn on how closely
      // MVDR reproduces conj(h).
      //
      double rg, rp;
      solo(1.30, 0.35, 23, &rg, &rp);
      const double to_b = wdist(fast.gb, fast.pb, rg, rp);
      const double to_a = wdist(fast.gb, fast.pb, fast.ga, fast.pa);
      const double span = wdist(rg, rp, fast.ga, fast.pa);
      const int moved = to_b < 0.25 * span && to_b < 0.25 * to_a;
      printf("  weight A %+0.2f dB %+0.1f deg -> %+0.2f dB %+0.1f deg, "
             "B alone gives %+0.2f dB %+0.1f deg\n",
             fast.ga, fast.pa, fast.gb, fast.pb, rg, rp);
      printf("  %.0f%% of the way from A's answer to B's   %s\n",
             100.0 * (1.0 - to_b / span), moved ? "OK" : "FAIL");

      if (!moved) { ok = 0; }
    }
  }
  printf("\n%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
