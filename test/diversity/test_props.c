/*
 * diversity_auto_ref survives the removal of a reference mode.
 *
 * The RADE passband reference was value 2, with RADE V1 at 3 and Digital
 * I/Q at 4. Removing it moved everything above it down, so a stored 2 is
 * either the old RADE passband or the new RADE V1 and a stored 3 either
 * the old RADE V1 or the new Digital I/Q - the two numberings cannot be
 * told apart by inspecting the value. diversity_auto_ref_scheme is what
 * makes the migration a decision rather than a guess, and this checks
 * both numberings resolve to the mode the operator actually chose.
 *
 * Loading the wrong reference is a silent failure: the menu comes up on a
 * plausible-looking mode and measures the wrong thing.
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
int receivers = 2, diversity_enabled = 1, radio_is_remote = 0;
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

/* a tiny two-key property store the test drives directly */
static char refval[32];
static int  have_scheme;
static char schemeval[32];
const char *getProperty(const char *n) {
  if (!strcmp(n, "diversity_auto_ref")) { return refval[0] ? refval : NULL; }

  if (!strcmp(n, "diversity_auto_ref_scheme")) { return have_scheme ? schemeval : NULL; }

  return NULL;
}
void setProperty(const char *n, const char *v) { (void)n; (void)v; }

static int check(const char *what, int stored, int scheme, int want) {
  snprintf(refval, sizeof(refval), "%d", stored);
  have_scheme = (scheme > 0);
  snprintf(schemeval, sizeof(schemeval), "%d", scheme);
  div_auto_ref = -1;
  diversity_auto_restore_state();
  const char *names[] = { "Window", "Carrier", "RADE V1", "Digital I/Q" };
  const int got = div_auto_ref;
  const int ok = (got == want);
  printf("  stored %d, scheme %-7s -> %-12s (want %-12s) %s\n",
         stored, scheme > 0 ? "2" : "absent",
         (got >= 0 && got <= 3) ? names[got] : "??",
         names[want], ok ? "OK" : "FAIL");
  (void)what;
  return ok;
}

int main(void) {
  memset(&rx0, 0, sizeof(rx0));
  memset(vfo, 0, sizeof(vfo));
  printf("diversity_auto_ref migration\n\n");
  int ok = 1;
  /* scheme 1: BAND CARRIER RADE_BAND RADE_V1 DIGITAL_IQ */
  ok &= check("old window",   0, 0, DIV_REF_BAND);
  ok &= check("old carrier",  1, 0, DIV_REF_CARRIER);
  ok &= check("old radeband", 2, 0, DIV_REF_DIGITAL_IQ);
  ok &= check("old radev1",   3, 0, DIV_REF_RADE_V1);
  ok &= check("old digital",  4, 0, DIV_REF_DIGITAL_IQ);
  printf("\n");
  /* scheme 2: values mean themselves */
  ok &= check("new window",   0, 2, DIV_REF_BAND);
  ok &= check("new carrier",  1, 2, DIV_REF_CARRIER);
  ok &= check("new radev1",   2, 2, DIV_REF_RADE_V1);
  ok &= check("new digital",  3, 2, DIV_REF_DIGITAL_IQ);
  printf("\n%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
