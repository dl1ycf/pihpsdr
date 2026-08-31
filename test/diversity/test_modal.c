/*
 * The loop's settings are modal: one block per group of modes.
 *
 * The failure this exists to catch is a mode change handing the loop
 * settings chosen for a signal that is no longer there - the carrier
 * tracker hunting a carrier SSB does not have, or the narrow window set
 * up for one CW note swallowing an SSB passband whole. It is silent:
 * every control has a plausible value, and only the answer is wrong.
 *
 * Three things are checked:
 *
 *   1. Changing mode files the settings in force under the outgoing
 *      group and produces the incoming group's, and going back produces
 *      the first set again.
 *   2. All of them survive a save and restore.
 *   3. A props file written before the blocks existed - flat keys only -
 *      gives every group what the radio was last set to, which is the
 *      old single-block behaviour rather than a set of defaults.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <gtk/gtk.h>
#include "mode.h"
#include "receiver.h"
#include "vfo.h"
#include "diversity_auto.h"

static RECEIVER rx0;
RECEIVER *receiver[8] = { &rx0 };
int receivers = 2, diversity_enabled = 0, radio_is_remote = 0;
int cw_keyer_sidetone_frequency = 800;
double div_cos = 1.0, div_sin = 0.0, div_gain = 0.0, div_phase = 0.0;
struct _vfo vfo[MAX_VFOS];
void t_print(const char *f, ...) { (void)f; }
double myatof(const char *s) { return atof(s); }

/*
 * The engine tells the menu when a mode change swapped one block of modal
 * settings for another. There is no menu here.
 */
gboolean diversity_menu_settings_changed(gpointer data) { (void)data; return G_SOURCE_REMOVE; }

/* ------------------------------------------------------------------ */
/* a property store the test drives directly                          */
/* ------------------------------------------------------------------ */

#define MAXPROPS 512
static struct { char name[128], value[128]; } props[MAXPROPS];
static int nprops;

const char *getProperty(const char *n) {
  for (int i = 0; i < nprops; i++) {
    if (!strcmp(props[i].name, n)) { return props[i].value; }
  }

  return NULL;
}

void setProperty(const char *n, const char *v) {
  for (int i = 0; i < nprops; i++) {
    if (!strcmp(props[i].name, n)) {
      snprintf(props[i].value, sizeof(props[i].value), "%s", v);
      return;
    }
  }

  if (nprops < MAXPROPS) {
    snprintf(props[nprops].name, sizeof(props[nprops].name), "%s", n);
    snprintf(props[nprops].value, sizeof(props[nprops].value), "%s", v);
    nprops++;
  }
}

static void props_clear(void) { nprops = 0; }

/* drop everything the modal blocks wrote, leaving the flat keys */
static void props_drop_groups(void) {
  int k = 0;

  for (int i = 0; i < nprops; i++) {
    if (strncmp(props[i].name, "diversity_group[", 16) != 0) { props[k++] = props[i]; }
  }

  nprops = k;
}

/* ------------------------------------------------------------------ */

static int fails;

static void check(const char *what, double got, double want) {
  const int ok = fabs(got - want) < 1e-6;

  if (!ok) { fails++; }

  printf("    %-24s %10.2f (want %10.2f)  %s\n", what, got, want, ok ? "OK" : "FAIL");
}

/*
 * A recognisable set of settings, so that a block turning up under the
 * wrong group is obvious rather than merely different.
 */
static void set_block(int ref, double centre, double width, double tau) {
  div_auto_ref = ref;
  div_auto_mode = DIV_AUTO_SUM;
  div_auto_follow_filter = 0;
  div_auto_centre = centre;
  div_auto_width = width;
  div_auto_tau = tau;
  div_band_centre = centre;
  div_band_width = width;
}

static void show_block(const char *what) {
  printf("  %s: ref %d, centre %.0f, width %.0f, tau %.1f\n",
         what, div_auto_ref, div_auto_centre, div_auto_width, div_auto_tau);
}

int main(void) {
  memset(&rx0, 0, sizeof(rx0));
  rx0.id = 0;
  rx0.sample_rate = 48000;
  memset(vfo, 0, sizeof(vfo));
  vfo[0].frequency = 7100000;
  vfo[0].ctun_frequency = 7100000;
  printf("Modal settings: one block per group of modes\n\n");
  /* ---------------------------------------------------------------- */
  printf("1. a mode change swaps one block for another\n");
  props_clear();
  diversity_auto_restore_state();
  /* arrive in SSB and set it up */
  diversity_auto_mode_changed(modeUSB);
  set_block(DIV_REF_BAND, -1500.0, 2600.0, 3.0);
  show_block("USB set to  ");
  /* AM: a different reference and a search region of its own */
  diversity_auto_mode_changed(modeAM);
  set_block(DIV_REF_CARRIER, 5000.0, 1000.0, 10.0);
  show_block("AM  set to  ");
  /* CW: a narrow window on one note */
  diversity_auto_mode_changed(modeCWU);
  set_block(DIV_REF_BAND, -700.0, 200.0, 1.0);
  show_block("CW  set to  ");
  diversity_auto_mode_changed(modeLSB);       /* same group as USB */
  show_block("back on LSB ");
  check("SSB ref", div_auto_ref, DIV_REF_BAND);
  check("SSB centre", div_auto_centre, -1500.0);
  check("SSB width", div_auto_width, 2600.0);
  check("SSB tau", div_auto_tau, 3.0);
  diversity_auto_mode_changed(modeSAM);       /* same group as AM */
  show_block("back on SAM ");
  check("AM ref", div_auto_ref, DIV_REF_CARRIER);
  check("AM centre", div_auto_centre, 5000.0);
  check("AM width", div_auto_width, 1000.0);
  check("AM tau", div_auto_tau, 10.0);
  diversity_auto_mode_changed(modeCWL);       /* same group as CWU */
  show_block("back on CWL ");
  check("CW centre", div_auto_centre, -700.0);
  check("CW width", div_auto_width, 200.0);
  /* ---------------------------------------------------------------- */
  printf("\n2. every block survives a save and restore\n");
  diversity_auto_save_state();
  diversity_auto_restore_state();
  diversity_auto_mode_changed(modeUSB);
  show_block("USB after   ");
  check("SSB centre", div_auto_centre, -1500.0);
  check("SSB tau", div_auto_tau, 3.0);
  diversity_auto_mode_changed(modeDIGU);
  diversity_auto_mode_changed(modeAM);
  show_block("AM  after   ");
  check("AM ref", div_auto_ref, DIV_REF_CARRIER);
  check("AM centre", div_auto_centre, 5000.0);
  check("AM tau", div_auto_tau, 10.0);
  /* ---------------------------------------------------------------- */
  printf("\n3. a props file with flat keys only seeds every group\n");
  /*
   * What an operator upgrading has: one set of settings, written before
   * the blocks existed. Every group must come up carrying it, not a
   * default - the settings are theirs and nothing has been said about
   * any particular mode yet.
   */
  props_drop_groups();
  diversity_auto_restore_state();
  const int modes[] = { modeUSB, modeCWL, modeFMN, modeAM, modeDIGL, modeSPEC };
  const char *names[] = { "USB", "CWL", "FMN", "AM ", "DIGL", "SPEC" };

  for (int i = 0; i < 6; i++) {
    diversity_auto_mode_changed(modes[i]);
    printf("    %-4s ref %d centre %7.0f width %7.0f\n",
           names[i], div_auto_ref, div_auto_centre, div_auto_width);

    /* the flat keys were last written under CW, which was in force */
    if (div_auto_centre != -700.0 || div_auto_width != 200.0) { fails++; }
  }

  printf("\n%s\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
