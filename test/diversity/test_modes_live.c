//
// Drive the real diversity_auto engine with synthetic two-antenna data
// and check that every reference mode actually produces a weight.
//
// This exists because a mode that silently never starts is the failure
// this code keeps producing: the carrier reference once sat on
// "searching" for ever because its bin range was computed before the
// transform that its carrier tracker needed.
//
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

// ---- stubs for the piHPSDR globals the engine touches ------------------
// The real struct types are used deliberately: stub structs with a
// plausible-looking subset of fields read the wrong offsets and silently
// produce nonsense.
static RECEIVER rx0;
RECEIVER *receiver[8] = { &rx0 };
int receivers = 2;
int diversity_enabled = 1;
int radio_is_remote = 0;
int cw_keyer_sidetone_frequency = 800;
double div_cos = 1.0, div_sin = 0.0, div_gain = 0.0, div_phase = 0.0;
struct _vfo vfo[MAX_VFOS];
void t_print(const char *fmt, ...){ va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); }
const char *getProperty(const char *n){ (void)n; return NULL; }
void setProperty(const char *n, const char *v){ (void)n; (void)v; }
double myatof(const char *s){ return atof(s); }

int main(void) {
  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0; rx0.sample_rate = 192000;
  rx0.filter_low = -8000; rx0.filter_high = 8000;
  memset(vfo, 0, sizeof(vfo));
  vfo[0].frequency = 7100000; vfo[0].ctun_frequency = 7100000;
  vfo[0].offset = 0; vfo[0].mode = modeAM;

  const double hr = 0.62, hi = -0.48;   // arm1 = h * arm0
  struct { const char *name; int ref; int obj; } cases[] = {
    { "Window/Null",  DIV_REF_BAND,      DIV_AUTO_NULL },
    { "Window/Sum",   DIV_REF_BAND,      DIV_AUTO_SUM  },
    { "Carrier/Sum",  DIV_REF_CARRIER,   DIV_AUTO_SUM  },
    { "Digital/Sum",  DIV_REF_DIGITAL_IQ, DIV_AUTO_SUM  },
    { "Digital/Null", DIV_REF_DIGITAL_IQ, DIV_AUTO_NULL },
    //
    // Best does not share the Null/Sum path: div_apply_best() holds
    // instead of producing a weight whenever div_auto_arm_valid is 0, and
    // div_arm_from_floor() has two independent gates that can leave it
    // there indefinitely. So "Best silently never starts" is exactly the
    // failure this test exists to catch, on a reference that places its
    // own window and on one that does not.
    //
    { "Window/Best",  DIV_REF_BAND,       DIV_AUTO_BEST },
    { "Digital/Best", DIV_REF_DIGITAL_IQ, DIV_AUTO_BEST },
  };
  int fails = 0;
  for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
    div_auto_ref = cases[c].ref;
    div_auto_mode = cases[c].obj;
    div_auto_follow_filter = 1;
    div_auto_tau = 1.0;
    div_auto_coherence_min = 0.1;
    div_cos = 1.0; div_sin = 0.0; div_gain = 0.0; div_phase = 0.0;
    diversity_auto_start();
    // a carrier at +37 Hz plus a little noise, on both arms
    // A carrier near the tuned frequency for the carrier tracker, plus a
    // component inside the RADE passband so that window has signal too.
    double ph = 0, ph2 = 0;
    srand(9);
    for (int blk = 0; blk < 400; blk++) {
      for (int n = 0; n < 512; n++) {
        ph  += 2.0*M_PI*37.0/192000.0;
        ph2 += 2.0*M_PI*1500.0/192000.0;
        double s = cos(ph) + 0.7*cos(ph2), t = sin(ph) + 0.7*sin(ph2);
        double n0 = 0.01*(2.0*rand()/RAND_MAX-1.0);
        double n1 = 0.01*(2.0*rand()/RAND_MAX-1.0);
        double a0r = s + n0,            a0i = t + n0;
        double a1r = hr*s - hi*t + n1,  a1i = hr*t + hi*s + n1;
        diversity_auto_sample(a0r, a0i, a1r, a1i);
      }
      g_usleep(200);
    }
    g_usleep(400000);
    int moved = (fabs(div_gain) > 0.01) || (fabs(div_phase) > 0.5);
    printf("%-14s -> gain %+7.2f dB  phase %+7.1f deg  coherence %3.0f%%  %s\n",
           cases[c].name, div_gain, div_phase, 100.0*div_auto_coherence,
           moved ? "OK" : "*** NEVER PRODUCED A WEIGHT ***");
    if (!moved) fails++;
    diversity_auto_stop();
  }
  printf("%s\n", fails ? "FAIL" : "PASS - every mode produced a weight");
  return fails ? 1 : 0;
}
