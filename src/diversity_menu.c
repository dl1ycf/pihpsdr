/* Copyright (C)
*  2016 - John Melton, G0ORX/N6LYT
*  2025 - Christoph van Wüllen, DL1YCF
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

#include "client_server.h"
#include "diversity_auto.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "rade_correlator.h"
#include "receiver.h"
#include "rx_panadapter.h"
#include "vfo.h"

#ifdef DIVERSITY_CAPTURE
  //
  // DEVELOPMENT TOOL - remove with the rest of the capture instrument.
  // See test/diversity/devtools/README.md.
  //
  #include "diversity_capture.h"
#endif

//
// These texts contain useful information that must go to the manual,
// but as they stand they pop up a window that is too large and
// covers important parts of the menu
//
#define gtk_widget_set_tooltip_text(x,y)

static GtkWidget *dialog = NULL;
static GtkWidget *gain_coarse_scale = NULL;
static GtkWidget *gain_fine_scale = NULL;
static GtkWidget *phase_fine_scale = NULL;
static GtkWidget *phase_coarse_scale = NULL;

static GtkWidget *auto_btn = NULL;
static GtkWidget *win_width_btn = NULL;
static GtkWidget *win_centre_btn = NULL;
static GtkWidget *coh_scale = NULL;
static GtkWidget *mcontainer = NULL;
static GtkWidget *acontainer = NULL;
static GtkWidget *status_label = NULL;
static GtkWidget *coh_label = NULL;
static GtkWidget *arm_label = NULL;
static GtkWidget *hold_b = NULL;
static GtkWidget *level_b = NULL;

static void hold_cb(GtkWidget *widget, gpointer data);

//
// The Averaging slider is geometric, not linear.
//
// The control spans 0.2 to 30 s and the interesting part of it is the
// short end: Null wants the shortest average that still passes the
// coherence gate, and the difference between 0.2 and 0.5 s is worth more
// than the difference between 20 and 30 s. Laid out linearly, everything
// below five seconds sits in the first sixth of the travel and cannot be
// set accurately; laid out geometrically - equal ratio per pixel, which
// is the natural spacing for a time constant - 0.2 to 5 s occupies 64 %
// of it. See Findings 18 and 21 in docs/diversity-measurements.md.
//
// The widget therefore carries a position, 0 to DIV_TAU_STEPS, and the
// two functions below convert. A thousand steps over a 150:1 range is
// half a percent a step, so a round trip through the widget moves tau by
// less than a quarter of a percent.
//
#define DIV_TAU_MIN     0.2
#define DIV_TAU_MAX    30.0
#define DIV_TAU_STEPS  1000.0

static double div_tau_from_pos(double pos) {
  return DIV_TAU_MIN * pow(DIV_TAU_MAX / DIV_TAU_MIN, pos / DIV_TAU_STEPS);
}

static double div_tau_to_pos(double tau) {
  if (tau < DIV_TAU_MIN) { tau = DIV_TAU_MIN; }

  if (tau > DIV_TAU_MAX) { tau = DIV_TAU_MAX; }

  return DIV_TAU_STEPS * log(tau / DIV_TAU_MIN) / log(DIV_TAU_MAX / DIV_TAU_MIN);
}

//
//
// The "Measure on" list.
//
// The order the operator sees is not the order of the DIV_REF_* values.
// Those are what land in the props file and go over the wire to a client,
// so they are fixed and new ones go on the end. The list puts the general
// purpose Window first, then the references for particular signals in
// the order an operator tends to meet them: CW, FSK/Digital, a carrier
// (AM/SAM), RADE V1. The table at div_ref_rows[] is the only place the
// two orders meet - everything else works in DIV_REF_* - so adding a
// reference means adding one line there.
//

static double gain_coarse, gain_fine;
static double phase_coarse, phase_fine;

static guint status_timer = 0;

//
// Ship the whole control state whenever any one control moves.
//
// One message rather than one per control: the block is small, it is
// idempotent, and it lets the radio work out what the change means by
// comparing against what it has - so the client never has to reason about
// restarts, resets or which objectives are 180 degrees apart. The action
// byte carries the one control that changes no setting.
//
static void div_send_settings(int action) {
  DIV_SETTINGS set;
  diversity_auto_get_settings(&set);

  if (radio_is_remote) {
    send_div_settings(cl_sock_tcp, &set, action);
  } else if (remoteclient.running) {
    //
    // The other direction: someone moved a control on the radio's own
    // panel and a client is watching. It adopts rather than acts, so the
    // action byte is not passed on - the radio has already done it.
    //
    send_div_settings(remoteclient.sock_tcp, &set, DIV_ACTION_NONE);
  }
}

#ifdef DIVERSITY_CAPTURE
//
// ===================================================================
//  DEVELOPMENT TOOL - NOT PART OF THE DIVERSITY FEATURE.
//  Compiled only under "make DIVCAP=1", never sent upstream. Delete
//  this block, the #include near the top, the one in cleanup(), the one
//  in status_update_cb() and the one beside the Invert button to remove
//  it.
//  See test/diversity/devtools/README.md.
// ===================================================================
//
// Records the analysis blocks to a file so a real signal can be replayed
// through the engine offline. Where the file goes, how long it runs and
// what note is stored with it come from the environment
// (PIHPSDR_DIVCAP_DIR / _SECONDS / _NOTE) rather than from properties,
// so nothing about it survives in an operator's config.
//
static GtkWidget *divcap_b = NULL;

//
// Declared here rather than in diversity_auto.h: nfft is private to
// diversity_auto.c, so the arming call has to live there, but the header
// is a permanent file and this is not.
//
extern int diversity_auto_capture_start(void);

static void divcap_cb(GtkWidget *widget, gpointer data) {
  (void)data;

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    //
    // Switch diversity on first if it is off, so the capture starts cold
    // and records acquisition and settling - arming the recorder and then
    // reaching for the Diversity tick means every capture starts already
    // converged.
    //
    const int started_here = !diversity_enabled;

    if (started_here) { radio_set_diversity(1); }

    if (!diversity_auto_capture_start()) {
      //
      // No analysis thread running - the objective is Manual, so there is
      // nothing to record - or the file would not open. Come back out, and
      // leave the radio as it was found.
      //
      if (started_here) { radio_set_diversity(0); }

      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
    }
  } else {
    diversity_capture_stop();
  }
}
#endif

static void cleanup(void) {
  if (status_timer != 0) {
    g_source_remove(status_timer);
    status_timer = 0;
  }

  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    //
    // Hold is deliberately left as it is. A held weight is a valid way to
    // keep a local noise source nulled, or to keep a peak that favours one
    // direction, so it stays in force until the operator releases it or
    // diversity is switched off and on again (see radio_set_diversity()).
    //
    hold_b = NULL;
    level_b = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    win_centre_btn = NULL;
    win_width_btn = NULL;
    coh_scale = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
#ifdef DIVERSITY_CAPTURE
    //
    // DEVELOPMENT TOOL. The capture carries on - closing the menu is not a
    // reason to stop recording, and it stops itself at its budget. Only the
    // widget goes.
    //
    divcap_b = NULL;
#endif
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void sanitize_man_values() {
  //
  // set coarse/fine values from "sanitized" actual values
  //
  if (man_div_gain >  27.0) { man_div_gain = 27.0; }
  if (man_div_gain < -27.0) { man_div_gain = -27.0; }
  while (man_div_phase >  180.0) { man_div_phase -= 360.0; }
  while (man_div_phase < -180.0) { man_div_phase += 360.0; }
  gain_coarse = 2.0 * round(0.5 * man_div_gain);
  if (man_div_gain >  25.0) { gain_coarse = 25.0; }
  if (man_div_gain < -25.0) { gain_coarse = -25.0; }
  gain_fine = man_div_gain - gain_coarse;
  phase_coarse = 4.0 * round(man_div_phase * 0.25);
  phase_fine = man_div_phase - phase_coarse;
}

//
// "Level output" is greyed out whenever the normaliser is not acting, so
// the tick never looks like it does something it does not. It acts only
// with diversity on, in Sum or Best: Null is excluded on purpose, Manual
// never runs the loop, and RADE V1 never runs the transform its powers
// come from (see div_norm_refresh()). It cannot be set from a remote
// client at all.
//
static void div_level_sensitive(void) {
  if (level_b == NULL) { return; }

  const int active = !radio_is_remote && diversity_enabled
                     && (div_auto_mode == DIV_AUTO_SUM || div_auto_mode == DIV_AUTO_BEST)
                     && div_auto_ref != DIV_REF_RADE_V1;

  if (gtk_widget_get_sensitive(level_b) != active) { gtk_widget_set_sensitive(level_b, active); }
}

static void enable_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  //
  // This starts or stops the analysis thread, so what the controls below
  // may be used for changes with it.
  //
  radio_set_diversity(state);
  div_level_sensitive();
}

static void att_cb(GtkWidget *widget, gpointer data) {
  suppress_popup_sliders++;
  radio_set_adc_attenuation(GPOINTER_TO_INT(data),
                            (int) (0.5+gtk_range_get_value(GTK_RANGE(widget))));
  suppress_popup_sliders--;
}

static void gain_coarse_changed_cb(GtkWidget *widget, gpointer data) {
  gain_coarse = gtk_range_get_value(GTK_RANGE(widget));
  man_div_gain = gain_coarse + gain_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, man_div_gain, man_div_phase);
    return;
  }
  radio_calc_div_params();
}

static void gain_fine_changed_cb(GtkWidget *widget, gpointer data) {
  gain_fine = gtk_range_get_value(GTK_RANGE(widget));
  man_div_gain = gain_coarse + gain_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, man_div_gain, man_div_phase);
    return;
  }
  radio_calc_div_params();
}

static void phase_coarse_changed_cb(GtkWidget *widget, gpointer data) {
  phase_coarse = gtk_range_get_value(GTK_RANGE(widget));
  man_div_phase = phase_coarse + phase_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, man_div_gain, man_div_phase);
    return;
  }
  radio_calc_div_params();
}

static void phase_fine_changed_cb(GtkWidget *widget, gpointer data) {
  phase_fine = gtk_range_get_value(GTK_RANGE(widget));
  man_div_phase = phase_coarse + phase_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, man_div_gain, man_div_phase);
    return;
  }
  radio_calc_div_params();
}


//
// Which sideband the RADE modem is on, as the operator set it. Shown
// because it is the one thing about this mode they can get wrong: with
// the passband on the wrong sideband the correlator is looking at the
// mirror image of the signal and will never lock.
//
// Internally this is "the modem is below/above the carrier"; LSB and USB
// is what an operator reads.
//
static const char *div_rade_side_text(void) {
  return (div_rade_side_get() < 0) ? "LSB" : "USB";
}

//
// The status line.
//
// It is the widest thing in the dialog, so it sets the minimum window
// width, and it must not grow. Every line is therefore built to exactly
// DIV_STATUS_CHARS characters out of four fixed fields - what is being
// measured, what the loop is doing, one detail belonging to the mode, and
// the weight - each printed with a precision that truncates as well as a
// width that pads. Nothing that arrives at run time can widen it.
//
// It is set in a monospace face for the same reason: fixed character
// counts only line up in a fixed-width font, and a status line whose
// columns wander is harder to read at a glance than an unaligned one.
//
// The predecessor was a printf per mode, the longest around a hundred
// characters, and it dictated a dialog half again as wide as the controls
// needed.
//
#define DIV_STATUS_TAG    9
#define DIV_STATUS_STATE  6
#define DIV_STATUS_DETAIL 10
//
// Three fields, three separating spaces, and a 16-character weight:
//   "%+6.1f dB %+5.0f°"
//
// The degree sign is one character but two bytes, so this is a count of
// *characters* - which is what gtk_label_set_width_chars() wants, and
// what the fields are padded to. It is not strlen().
//
#define DIV_STATUS_CHARS  (DIV_STATUS_TAG + DIV_STATUS_STATE + DIV_STATUS_DETAIL + 3 + 16)

//
// A small breathing space at each end. The label is the widest thing in
// the dialog, so this is the only reason it is not hard against both
// window edges.
//
#define DIV_STATUS_MARGIN 6

static void div_status_set(const char *tag, const char *state, const char *detail,
                           double g, double p) {
  char text[128];
  snprintf(text, sizeof(text), "%-*.*s %-*.*s %-*.*s %+6.1f dB %+5.0f°",
           DIV_STATUS_TAG, DIV_STATUS_TAG, tag,
           DIV_STATUS_STATE, DIV_STATUS_STATE, state,
           DIV_STATUS_DETAIL, DIV_STATUS_DETAIL, detail,
           g, p);
  gtk_label_set_text(GTK_LABEL(status_label), text);
}

//
// Reflect what the analysis thread is doing. It only ever writes plain
// scalars, so the GUI polls them here rather than having a worker thread
// touch widgets.
//
//
// The second status line: which antenna is measuring better, and which
// one the selection objective is using.
//
// Worth its own line in every mode, not just in Best. An antenna that
// reads 12 dB down because it is deaf and one that reads 12 dB down
// because it is quiet look identical on the panadapter and want opposite
// weights - which is the case the 60 m captures turned up. See Finding 13
// in docs/diversity-measurements.md.
//
// Held to DIV_STATUS_CHARS by construction, like the line above it: the
// longest string this can produce is exactly that wide.
//
static void div_arm_status_set(void) {
  char text[96];

  if (arm_label == NULL) { return; }

  if (!div_auto_running || !div_auto_arm_valid) {
    snprintf(text, sizeof(text), "Antennas  measuring");
  } else {
    double d = fabs(div_auto_arm_db);
    char sel[20];
    sel[0] = 0;

    if (d > 99.9) { d = 99.9; }

    //
    // Named as the hardware and the attenuator row name them, ADC1 and
    // ADC2, and by converter rather than by arm: arm 0 is the ADC RX1 is
    // set to, so with RX1 on ADC2 arm 0 is ADC2.
    //
    const int rxadc = receiver[0]->adc;

    if (div_auto_mode == DIV_AUTO_BEST) {
      snprintf(sel, sizeof(sel), "  using ADC%d", (div_auto_arm_pick ^ rxadc) + 1);
    }

    snprintf(text, sizeof(text), "Antennas  ADC%d better by %4.1f dB%s",
             (((div_auto_arm_db > 0.0) ? 1 : 0) ^ rxadc) + 1, d, sel);
  }

  gtk_label_set_text(GTK_LABEL(arm_label), text);
}

//
// Keep the bottom of the Min coherence slider on the gate's noise floor.
//
// The floor moves with the reference, the window (or the RX filter it
// follows), the occupied span on FSK/Digital, the bin width and the
// averaging time - see diversity_auto_coh_floor(). Following it here, on
// the status tick, catches every one of those however it changed,
// including from outside the menu.
//
// The setting itself is not touched. The slider shows the larger of the
// setting and the floor, which is what the gate compares against, and
// when the floor comes down again the operator's own value reappears.
// coh_cb() is blocked while the slider is moved, so the floor is not
// taken for an operator setting and filed as one.
//
static void coh_cb(GtkWidget *widget, gpointer data);

static void div_coh_range_update(void) {
  if (coh_scale == NULL) { return; }

  const double lo = 100.0 * diversity_auto_coh_floor(div_auto_ref);
  GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(coh_scale));
  const double want = fmax(100.0 * div_auto_coherence_min, lo);

  if (fabs(gtk_adjustment_get_lower(adj) - lo) < 0.05
      && fabs(gtk_range_get_value(GTK_RANGE(coh_scale)) - want) < 0.05) { return; }

  g_signal_handlers_block_by_func(coh_scale, coh_cb, NULL);
  gtk_range_set_range(GTK_RANGE(coh_scale), lo, 95.0);
  gtk_range_set_value(GTK_RANGE(coh_scale), want);
  g_signal_handlers_unblock_by_func(coh_scale, coh_cb, NULL);
}

//
// The tracked carrier or CW tone, for the status line, measured from the
// zero beat - so a correctly tuned signal reads near zero in every mode.
// That is the shifted frame's own zero everywhere but CW. There
// rx_set_filter() folds the sidetone into the passband, so
// div_auto_carrier - in the same frame as filter_low and filter_high - sits
// one pitch away from the note being listened to, and the readout showed
// about +800 Hz for a signal tuned exactly right. div_window_zero() takes
// it back out, the same correction the panadapter's carrier line and the
// hand-placed window use.
//
// One decimal, and none at all past 10 kHz: the field is ten characters
// and "+400000 Hz" is exactly that.
//
static void div_tone_detail(char *detail, size_t len) {
  const double f = div_auto_carrier - div_window_zero(vfo[0].mode, cw_keyer_sidetone_frequency);
  snprintf(detail, len, (fabs(f) < 10000.0) ? "%+.1f Hz" : "%+.0f Hz", f);
}

static int status_update_cb(gpointer data) {
  if (dialog == NULL) {
    status_timer = 0;
    return G_SOURCE_REMOVE;
  }

  div_coh_range_update();
  //
  // On the tick as well as in the callbacks below: Enable can change from
  // outside the menu.
  //
  div_level_sensitive();

  //
  // Whether the loop is running is not something this dialog is told
  // about. It changes on the Diversity Enable tick, on a resolution
  // change, and from outside the menu altogether - a toolbar action or a
  // remote client can call radio_set_diversity() while it is open. Every
  // one of those used to need its own call to keep the controls honest,
  // and the Diversity Enable tick did not have one: enabling diversity
  // from inside the dialog started the loop and left Hold and Invert
  // greyed out until something else happened to refresh them.
  //
  // Doing it on the tick instead makes that class of bug impossible.
  // gtk_widget_set_sensitive() returns immediately when the state is
  // unchanged, so this is six comparisons four times a second.
  //
  //
  // Track the automatically determined values in the manual sliders so
  // the operator can see where the loop has settled, and so the sliders
  // start from there if auto is switched off.
  //
  // Not under Hold: the sliders belong to the operator then, and moving
  // them underneath would make the control useless.
  //

#ifdef DIVERSITY_CAPTURE

  //
  // DEVELOPMENT TOOL. The block count goes on the button rather than into
  // the status line, which is held to exactly DIV_STATUS_CHARS.
  //
  if (divcap_b != NULL) {
    char cap[48];
    diversity_capture_status(cap, sizeof(cap));
    gtk_button_set_label(GTK_BUTTON(divcap_b), (cap[0] != '\0') ? cap : "Capture");

    if (!div_capture_active && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(divcap_b))) {
      //
      // It reached its block budget and closed itself. Follow it out.
      //
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(divcap_b), FALSE);
    }
  }

#endif
  div_arm_status_set();

  //
  // Hold can be released from outside this dialog - switching diversity
  // off, or a remote client - so keep the button in step with it.
  //
  if (hold_b && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(hold_b)) != div_auto_hold) {
    g_signal_handlers_block_by_func(hold_b, G_CALLBACK(hold_cb), NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hold_b), div_auto_hold);
    g_signal_handlers_unblock_by_func(hold_b, G_CALLBACK(hold_cb), NULL);
  }

  if (!div_auto_running) {
    div_status_set("Auto off", "", "", auto_div_gain, auto_div_phase);
    return G_SOURCE_CONTINUE;
  }

  char tag[32], detail[32];
  const char *state;
  //
  // Under Hold the weight shown is the one the loop has tracked to, not
  // the one being applied - seeing the two apart is the point of it. The
  // sliders show what is applied.
  //
  const double g = div_auto_hold ? div_track_gain  : auto_div_gain;
  const double p = div_auto_hold ? div_track_phase : auto_div_phase;
  //
  // "*" on the tag: the window ran past the Nyquist limit for this sample
  // rate and was clamped, so it is not the one that was asked for.
  //
  const char *clamp = div_auto_clamped ? "*" : "";
  detail[0] = 0;

  switch (div_auto_ref) {
  case DIV_REF_CARRIER:
    snprintf(tag, sizeof(tag), "Car %.0fHz%s", div_auto_binhz, clamp);

    if (!div_auto_carrier_valid) {
      state = "search";
    } else {
      state = div_auto_hold ? "HOLD" : (div_auto_holding ? "wait" : "track");
      div_tone_detail(detail, sizeof(detail));
    }

    break;

  case DIV_REF_RADE_V1:
    snprintf(tag, sizeof(tag), "RADE V1");

    if (rade_corr_locked) {
      //
      // The pilot percentage is the share of the energy in the pilot span
      // that the pilot itself accounts for, so it reads low under strong
      // QRM even while the correlator tracks perfectly well - which is
      // the situation this mode exists for. Lock is the thing to watch.
      //
      //
      // "fade": locked, but the pilot is not currently strong enough to
      // measure from, so the weight is frozen at its last good value.
      // That is a fade, not a loss - the lock is kept until a new one
      // replaces it, and meanwhile the correlator searches for one.
      //
      state = div_auto_hold ? "HOLD" : (div_auto_holding ? "fade" : "LOCK");
      snprintf(detail, sizeof(detail), "%s %3.0f%%",
               div_rade_side_text(), 100.0 * rade_corr_quality);
    } else {
      state = rade_corr_confirming ? "confrm" : "search";
      snprintf(detail, sizeof(detail), "%s", div_rade_side_text());
    }

    break;

  case DIV_REF_CW:
    snprintf(tag, sizeof(tag), "CW %.0fHz%s", div_auto_binhz, clamp);

    if (!div_auto_carrier_valid) {
      state = div_auto_hold ? "HOLD" : "search";
    } else {
      //
      // The tone being tracked: the same readout as the carrier tracker,
      // and the shaded span on the panadapter.
      //
      state = div_auto_hold ? "HOLD" : (div_auto_holding ? "wait" : "track");
      div_tone_detail(detail, sizeof(detail));
    }

    break;

  case DIV_REF_DIGITAL_IQ:
    snprintf(tag, sizeof(tag), "Dig %.0fHz%s", div_auto_binhz, clamp);

    if (!div_auto_occ_valid) {
      //
      // Nothing in the region stands above its own noise floor. This is
      // the ordinary no-signal state, not a fault, and it is the one
      // worth distinguishing: it says the region is in the right place
      // but empty, where "wait" would say something was found and then
      // rejected for incoherence.
      //
      state = div_auto_hold ? "HOLD" : "search";
      snprintf(detail, sizeof(detail), "no signal");
    } else {
      state = div_auto_hold ? "HOLD" : (div_auto_holding ? "wait" : "track");
      //
      // The occupied width rather than the coherence: it is what the
      // occupancy split decided, and it is checkable against the darker
      // band on the panadapter.
      //
      snprintf(detail, sizeof(detail), "occ %4.0fHz",
               div_auto_occ_hi - div_auto_occ_lo);
    }

    break;

  default:
    snprintf(tag, sizeof(tag), "Win %.0fHz%s", div_auto_binhz, clamp);
    state = div_auto_hold ? "HOLD" : (div_auto_holding ? "wait" : "track");
    snprintf(detail, sizeof(detail), "coh %3.0f%%", 100.0 * div_auto_coherence);
    break;
  }

  div_status_set(tag, state, detail, g, p);
  return G_SOURCE_CONTINUE;
}

//
// The "Measure on" combo lists the references in its own order, which is
// not the order of the DIV_REF_* enum. Every conversion between a combo
// row and a reference goes through these two.
//
static const struct {
  int ref;
  const char *label;
} div_ref_rows[] = {
  { DIV_REF_BAND,       "Window (wideband)"            },
  { DIV_REF_CW,         "CW / Morse (keyed tone)"      },
  { DIV_REF_DIGITAL_IQ, "FSK/Digital (occupancy MVDR)" },
  { DIV_REF_CARRIER,    "Carrier (AM/SAM)"             },
  { DIV_REF_RADE_V1,    "RADE V1 pilot (MVDR)"         }
};

#define DIV_REF_NROWS ((int)(sizeof(div_ref_rows) / sizeof(div_ref_rows[0])))

static int div_ref_to_row(int ref) {
  for (int i = 0; i < DIV_REF_NROWS; i++) {
    if (div_ref_rows[i].ref == ref) { return i; }
  }

  return 0;
}

static int div_row_to_ref(int row) {
  if (row < 0 || row >= DIV_REF_NROWS) { return DIV_REF_BAND; }

  return div_ref_rows[row].ref;
}

static void mode_changed_cb(GtkWidget *widget, gpointer data) {
  int previous = div_auto_mode;
  div_auto_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  div_level_sensitive();

  if (div_auto_mode == DIV_MANUAL) {
    //
    // Copy the "auto" parameters to "manual". To do so, re-calculate
    // the coarse/fine value and change the GUI elements, which then
    // will fire the callbacks
    //
    man_div_gain = auto_div_gain;
    man_div_phase = auto_div_phase;
    sanitize_man_values();
    gtk_range_set_value(GTK_RANGE(gain_coarse_scale), gain_coarse);
    gtk_range_set_value(GTK_RANGE(gain_fine_scale), gain_fine);
    gtk_range_set_value(GTK_RANGE(phase_coarse_scale), phase_coarse);
    gtk_range_set_value(GTK_RANGE(phase_fine_scale), phase_fine);
    gtk_widget_hide(acontainer);
    gtk_widget_show(mcontainer);
  } else {
    gtk_widget_hide(mcontainer);
    gtk_widget_show(acontainer);
  }
  gtk_window_resize(GTK_WINDOW(dialog), 1, 1);

  //
  // Null and Sum are two formulas over the same accumulated cross and
  // auto spectra - only the sign and which power normalises it differ, so
  // the answers are 180 degrees apart. Nothing about the analysis depends
  // on which is selected.
  //
  // So do not restart the engine here. Restarting resets those
  // accumulators, and with a long averaging time both objectives then
  // spent seconds re-converging from nothing, which made switching
  // between them look like it did nothing at all.
  //
  // Only whether the analysis thread exists depends on this control.
  //
  if ((previous == DIV_MANUAL) != (div_auto_mode == DIV_MANUAL)) {
    diversity_auto_restart();
  } else if ((previous == DIV_AUTO_NULL && div_auto_mode == DIV_AUTO_SUM) ||
             (previous == DIV_AUTO_SUM && div_auto_mode == DIV_AUTO_NULL)) {
    //
    // Null <-> Sum. Turn the weight in force through 180 degrees now, as
    // well as changing which answer the loop computes: the two objectives
    // are that far apart, and waiting for the loop to get there does not
    // work when it is not applying anything - see diversity_auto_invert().
    //
    // This is the same path the Invert button takes, deliberately, so the
    // button and the combo cannot behave differently.
    //
    // On a client both calls are refused and the radio does this instead,
    // drawing the same conclusion from the settings block below. The
    // inverted weight arrives with the next status push.
    //
    diversity_auto_invert();
  }

  div_send_settings(DIV_ACTION_NONE);
}

//
// Null and Sum are the same measurement with the sign of the answer and
// the power that normalises it exchanged, so they are 180 degrees apart.
// Swapping between them is the quickest way to tell whether the array is
// pointed at the wanted signal or at the interference, which is worth a
// button of its own rather than a trip through the combo.
//
// It does nothing but move the combo: everything else - turning the
// weight in force through 180 degrees, telling the loop not to slew, and
// putting the new value in the sliders - happens in mode_changed_cb(), so
// there is exactly one description of what changing the objective does.
//
// Null and Sum are the whole of it. Best has no opposite - it selects an
// antenna rather than steering a null - and mode_changed_cb() has no
// inversion to perform for a Best -> Null move, so the button would
// change objective and nothing else. update_manual_sensitivity() greys it
// out there; this is the belt to that pair of braces.
//
// cppcheck-suppress constParameterCallback
static void invert_cb(GtkWidget *widget, gpointer data) {
  if (div_auto_mode != DIV_AUTO_SUM && div_auto_mode != DIV_AUTO_NULL) { return; }

  //
  // This sends a signal, so mode_changed_cb does the rest
  //
  gtk_combo_box_set_active(GTK_COMBO_BOX(auto_btn),
                           (div_auto_mode == DIV_AUTO_NULL) ? DIV_AUTO_SUM : DIV_AUTO_NULL);
}

// cppcheck-suppress constParameterCallback
static void hold_cb(GtkWidget *widget, gpointer data) {
  diversity_auto_set_hold(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
  div_send_settings(DIV_ACTION_NONE);
}

//
// Remote client: the radio has sent its settings - on connect, or because
// someone moved a control on the radio's own panel. Adopt them and show
// them. Runs on the GTK thread, put there by the client read loop.
//
gboolean diversity_client_set_settings(gpointer data) {
  const DIV_SETTINGS_COMMAND *c = (const DIV_SETTINGS_COMMAND *)data;
  DIV_SETTINGS set;
  set.mode           = c->mode;
  set.ref            = c->ref;
  set.follow_filter  = c->follow_filter;
  set.weighting      = c->weighting;
  set.hold           = c->hold;
  set.centre         = from_double(c->centre);
  set.width          = from_double(c->width);
  set.tau            = from_double(c->tau);
  set.hang           = from_double(c->hang);
  set.coherence_min  = from_double(c->coherence_min);
  set.resolution     = from_double(c->resolution);
  set.band_centre    = from_double(c->band_centre);
  set.band_width     = from_double(c->band_width);
  set.carrier_centre = from_double(c->carrier_centre);
  set.carrier_width  = from_double(c->carrier_width);
  set.digital_centre = from_double(c->digital_centre);
  set.digital_width  = from_double(c->digital_width);
  diversity_auto_apply_settings(&set, DIV_ACTION_NONE);
  //div_populate_from_settings();
  g_free(data);
  return G_SOURCE_REMOVE;
}

//
// Remote client: what the loop is measuring. Written straight into the
// globals the status line, the antenna line and the panadapter overlay
// already read, so none of them needs to know where it came from.
//
gboolean diversity_client_set_status(gpointer data) {
  const DIV_STATUS_DATA *d = (const DIV_STATUS_DATA *)data;
  DIV_STATUS st;
  st.enabled         = d->enabled;
  st.running         = d->running;
  st.holding         = d->holding;
  st.clamped         = d->clamped;
  st.arm_valid       = d->arm_valid;
  st.arm_pick        = d->arm_pick;
  st.carrier_valid   = d->carrier_valid;
  st.occ_valid       = d->occ_valid;
  st.rade_locked     = d->rade_locked;
  st.rade_confirming = d->rade_confirming;
  st.rade_side       = d->rade_side;
  st.att0            = d->att0;
  st.att1            = d->att1;
  st.binhz        = from_double(d->binhz);
  st.coherence    = from_double(d->coherence);
  st.carrier      = from_double(d->carrier);
  st.arm_db       = from_double(d->arm_db);
  st.occ_lo       = from_double(d->occ_lo);
  st.occ_hi       = from_double(d->occ_hi);
  st.gain         = from_double(d->gain);
  st.phase        = from_double(d->phase);
  st.track_gain   = from_double(d->track_gain);
  st.track_phase  = from_double(d->track_phase);
  st.rade_quality = from_double(d->rade_quality);
  //
  // Whether the panadapter overlay should be on screen, before and after
  // adopting this block: rx_panadapter.c draws it while diversity is on
  // and the objective is not Manual.
  //
  const int overlay_was = (diversity_enabled && div_auto_mode != DIV_MANUAL);
  diversity_auto_apply_status(&st);
  const int overlay_now = (diversity_enabled && div_auto_mode != DIV_MANUAL);

  //
  // The panadapter repaints its whole surface every frame, so on the radio
  // the overlay clears itself with the next frame. A client only draws
  // when a spectrum packet arrives and passes client_thread.c's tests, so
  // switching diversity off at the radio could leave the last frame, green
  // box and all, on screen until those agreed again. Repaint once here.
  //
  if (overlay_was && !overlay_now && receivers > 0 && receiver[0] != NULL
      && receiver[0]->display_panadapter && receiver[0]->panadapter_surface != NULL) {
    rx_panadapter_update(receiver[0]);
  }

  g_free(data);
  return G_SOURCE_REMOVE;
}

//
// Some parameters of the engine are stored individually "by the reference"
// So when changing the reference, one has to store the old parameters in
// their individual slot, and restore them from the new slot.
//
static void store_ref_values(int ref) {
  switch (ref) {
  case DIV_REF_CARRIER:
    div_carrier_centre = div_auto_centre;
    div_carrier_width  = div_auto_width;
    div_carrier_cohmin = div_auto_coherence_min;
    break;
  case DIV_REF_BAND:
    div_band_centre = div_auto_centre;
    div_band_width  = div_auto_width;
    div_band_cohmin = div_auto_coherence_min;
    break;
  case DIV_REF_DIGITAL_IQ:
    div_digital_centre = div_auto_centre;
    div_digital_width  = div_auto_width;
    div_digital_cohmin = div_auto_coherence_min;
    break;
  case DIV_REF_CW:
    div_cw_centre = div_auto_centre;
    div_cw_width  = div_auto_width;
    div_cw_cohmin = div_auto_coherence_min;
    break;
  case DIV_REF_RADE_V1:
    //
    // No window of its own - the correlator decides what it looks at -
    // and no threshold to file any more: its slot is pinned at zero in
    // div_settings_validate(), and storing the live value into it here
    // would let a value that arrived by some other route stick.
    //
    break;
  }
}

static void centre_cb(GtkWidget *widget, gpointer data);
static void width_cb(GtkWidget *widget, gpointer data);
static void coh_cb(GtkWidget *widget, gpointer data);

//
// Show the window and threshold now in force. The handlers are blocked,
// so that this is not taken for the operator moving the controls and
// filed straight back.
//
static void div_ref_widgets_show(void) {
  if (win_centre_btn) {
    g_signal_handlers_block_by_func(win_centre_btn, centre_cb, NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(win_centre_btn), div_auto_centre);
    g_signal_handlers_unblock_by_func(win_centre_btn, centre_cb, NULL);
  }

  if (win_width_btn) {
    g_signal_handlers_block_by_func(win_width_btn, width_cb, NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(win_width_btn), div_auto_width);
    g_signal_handlers_unblock_by_func(win_width_btn, width_cb, NULL);
  }

  if (coh_scale) {
    g_signal_handlers_block_by_func(coh_scale, coh_cb, NULL);
    gtk_range_set_value(GTK_RANGE(coh_scale), 100.0 * div_auto_coherence_min);
    g_signal_handlers_unblock_by_func(coh_scale, coh_cb, NULL);
  }
}

static void restore_ref_values(int ref) {
  //
  // In addition to just restoring the values,
  // GUI elements have to be updated. To this end,
  // block/unblock the respective signals.
  //
  switch (ref) {
  case DIV_REF_CARRIER:
    div_auto_centre = div_carrier_centre;
    div_auto_width = div_carrier_width;
    div_auto_coherence_min = div_carrier_cohmin;
    break;
  case DIV_REF_BAND:
    div_auto_centre = div_band_centre;
    div_auto_width  = div_band_width;
    div_auto_coherence_min = div_band_cohmin;
    break;
  case DIV_REF_DIGITAL_IQ:
    div_auto_centre = div_digital_centre;
    div_auto_width  = div_digital_width;
    div_auto_coherence_min = div_digital_cohmin;
    break;
  case DIV_REF_CW:
    div_auto_centre = div_cw_centre;
    div_auto_width  = div_cw_width;
    div_auto_coherence_min = div_cw_cohmin;
    break;
  case DIV_REF_RADE_V1:
    div_auto_coherence_min = 0.0;   // retired - see div_settings_validate()
    break;
  }

  //
  // Without this the controls kept showing the previous reference's
  // window and threshold, and the next move of one of them filed those
  // under the new reference.
  //
  div_ref_widgets_show();
}

static void ref_changed_cb(GtkWidget *widget, gpointer data) {
  //
  // Store the "current values" to the "old ref values",
  // then restore the "current values" from the "new ref values"
  //
  // After restore, modify the GUI elements as to reflect the new values
  //
  store_ref_values(div_auto_ref);
  div_auto_ref = div_row_to_ref(gtk_combo_box_get_active(GTK_COMBO_BOX(widget)));
  restore_ref_values(div_auto_ref);
  div_level_sensitive();

  //
  // On RADE V1 the wanted signal is the one the pilot correlator is
  // pointing at, so the sensible objective is to maximise its SNR rather
  // than to null the strongest correlated thing in the window. Default to
  // Sum on the way in; the operator can still choose otherwise
  // afterwards.
  //
  if (div_auto_ref == DIV_REF_RADE_V1) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(auto_btn), DIV_AUTO_SUM);
  }

  //
  // RADE V1 has no threshold of its own any more - the pilot already
  // gates - so its row goes. See div_settings_validate().
  //
  if (coh_label) { gtk_widget_set_visible(coh_label, div_auto_ref != DIV_REF_RADE_V1); }

  if (coh_scale) { gtk_widget_set_visible(coh_scale, div_auto_ref != DIV_REF_RADE_V1); }

  //
  // Restart if the analysis thread has to come up or go down - which
  // includes the case just above, where selecting a RADE reference moved
  // the objective off Off - or if the pilot correlator's own front end
  // has to be built or torn down.
  //
  diversity_auto_restart();
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

//
// The output-level normaliser. Radio-side only: it is not on the wire, so
// it is insensitive on a client.
//
static void normalise_cb(GtkWidget *widget, gpointer data) {
  (void)data;
  div_auto_normalise = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void follow_cb(GtkWidget *widget, gpointer data) {
  div_auto_follow_filter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  //
  // Handing the window to the operator: start it on the passband it was
  // following, unless they have already placed one of their own.
  //
  if (!div_auto_follow_filter) {
    double centre, width;

    if (diversity_auto_seed_window(&centre, &width)) {
      div_auto_centre = centre;
      div_auto_width  = width;
    }

    store_ref_values(div_auto_ref);
    div_ref_widgets_show();
  }

  gtk_widget_set_sensitive(win_centre_btn, NOT(div_auto_follow_filter));
  gtk_widget_set_sensitive(win_width_btn, NOT(div_auto_follow_filter));
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

static void centre_cb(GtkWidget *widget, gpointer data) {
  div_auto_centre = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  store_ref_values(div_auto_ref);
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

static void width_cb(GtkWidget *widget, gpointer data) {
  div_auto_width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  store_ref_values(div_auto_ref);
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

//
// The widget's own value is a position, so it has to be told what to
// print. Two decimals below a second, where the steps are 5 ms and the
// operator is choosing between 0.20 and 0.25; one above it, where they
// are not.
//
static gchar *tau_format_cb(GtkScale *scale, gdouble value, gpointer data) {
  (void)scale;
  (void)data;
  const double tau = div_tau_from_pos(value);
  return g_strdup_printf(tau < 1.0 ? "%.2f" : "%.1f", tau);
}

static void tau_cb(GtkWidget *widget, gpointer data) {
  div_auto_tau = div_tau_from_pos(gtk_range_get_value(GTK_RANGE(widget)));
  div_send_settings(DIV_ACTION_NONE);
}

static void coh_cb(GtkWidget *widget, gpointer data) {
  (void)data;

  //
  // restore_ref_values() moves this slider when the reference changes,
  // and div_coh_range_update() moves it on the status tick, each with
  // this handler blocked: neither is the operator setting a value, and the
  // slider's half-percent step would quantise the value on the way back in.
  //
  div_auto_coherence_min = 0.01 * gtk_range_get_value(GTK_RANGE(widget));
  store_ref_values(div_auto_ref);
  //
  // Straight into the selected reference's own slot as well, so that a
  // settings block sent from here carries the new value rather than
  // whatever was stored at the last reference change.
  //
  div_send_settings(DIV_ACTION_NONE);
}

static void res_changed_cb(GtkWidget *widget, gpointer data) {
  static const double res[] = { 12.0, 6.0, 3.0 };
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (i < 0 || i > 2) { i = 0; }

  div_auto_resolution = res[i];
  //
  // The transform length changes, so the engine has to be rebuilt.
  //
  diversity_auto_restart();
  div_send_settings(DIV_ACTION_NONE);
}

void diversity_menu(GtkWidget *parent) {
  GtkWidget *btn, *lbl;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Diversity");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  //
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 2, 1);
  //
  // First row: basic controls
  //
  int row=1;
  btn = gtk_check_button_new_with_label("Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), diversity_enabled);
  g_signal_connect(btn, "toggled", G_CALLBACK(enable_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 2, 1);
  auto_btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_btn), "Manual");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_btn), "Null (cancel common signal)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_btn), "Sum (co-phase antennas)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_btn), "Best (use the better antenna)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(auto_btn), div_auto_mode);
  gtk_grid_attach(GTK_GRID(grid), auto_btn, 2, row, 4, 1);
  g_signal_connect(auto_btn, "changed", G_CALLBACK(mode_changed_cb), NULL);
  //
  // Beside the objective it depends on; greyed out when it is not acting -
  // see div_level_sensitive().
  //
  level_b = gtk_check_button_new_with_label("Level output");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(level_b), div_auto_normalise);
  gtk_grid_attach(GTK_GRID(grid), level_b, 7, row, 4, 1);
  g_signal_connect(level_b, "toggled", G_CALLBACK(normalise_cb), NULL);
  //gtk_widget_set_tooltip_text(auto_combo,
  //                          "Sum combines both antennas. Best measures the "
  //                          "signal-to-noise ratio on each and hands the "
  //                          "output to whichever is winning, which is worth "
  //                          "having when one antenna is much better than the "
  //                          "other - and when it is not, Sum is worth about "
  //                          "1.7 dB more. Null is the diagnostic: it cancels "
  //                          "what the two antennas hear in common.");
  if (have_rx_att) {
    row++;
    //
    int rxadc = receiver[0]->adc;
    int otheradc = 1 - rxadc;
    lbl = gtk_label_new("RX1 ATT:");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
    btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_range_set_value(GTK_RANGE(btn), adc[rxadc].attenuation);
    g_signal_connect(btn, "value_changed", G_CALLBACK(att_cb), GINT_TO_POINTER(rxadc));
    gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 4, 1);
    //
    // The other slider belongs to the converter RX1 is not on, so it is
    // ADC2 only while RX1 is on ADC1.
    //
    char otherlbl[16];
    snprintf(otherlbl, sizeof(otherlbl), "ADC%d:", otheradc + 1);
    lbl = gtk_label_new(otherlbl);
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 6, row, 1, 1);
    btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_range_set_value(GTK_RANGE(btn), adc[otheradc].attenuation);
    g_signal_connect(btn, "value_changed", G_CALLBACK(att_cb), GINT_TO_POINTER(otheradc));
    gtk_grid_attach(GTK_GRID(grid), btn, 7, row, 4, 1);
  }
  row++;
  //
  // Container for the "manual" controls
  //
  mcontainer = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), mcontainer, 0, row, 11, 1);
  GtkWidget *mgrid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(mgrid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(mgrid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(mgrid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(mgrid), 5);
  //
  // Sanitize the manual values before setting the sliders
  //
  sanitize_man_values();
  lbl = gtk_label_new("Gain (dB, coarse)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(mgrid), lbl, 0, 0, 2, 1);
  gain_coarse_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -25.0, +25.0, 0.5);
  gtk_range_set_value(GTK_RANGE(gain_coarse_scale), gain_coarse);
  gtk_grid_attach(GTK_GRID(mgrid), gain_coarse_scale, 2, 0, 8, 1);
  g_signal_connect(G_OBJECT(gain_coarse_scale), "value_changed", G_CALLBACK(gain_coarse_changed_cb), NULL);
  GtkWidget *gain_fine_label = gtk_label_new("Gain (dB, fine)");
  gtk_widget_set_name(gain_fine_label, "boldlabel");
  gtk_widget_set_halign(gain_fine_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(mgrid), gain_fine_label, 0, 1, 2, 1);
  gain_fine_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -2.0, +2.0, 0.05);
  gtk_range_set_value(GTK_RANGE(gain_fine_scale), gain_fine);
  gtk_grid_attach(GTK_GRID(mgrid), gain_fine_scale, 2, 1, 8, 1);
  g_signal_connect(G_OBJECT(gain_fine_scale), "value_changed", G_CALLBACK(gain_fine_changed_cb), NULL);
  GtkWidget *phase_coarse_label = gtk_label_new("Phase (coarse)");
  gtk_widget_set_name(phase_coarse_label, "boldlabel");
  gtk_widget_set_halign(phase_coarse_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(mgrid), phase_coarse_label, 0, 2, 2, 1);
  phase_coarse_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -180.0, 180.0, 2.0);
  gtk_range_set_value(GTK_RANGE(phase_coarse_scale), phase_coarse);
  gtk_grid_attach(GTK_GRID(mgrid), phase_coarse_scale, 2, 2, 8, 1);
  g_signal_connect(G_OBJECT(phase_coarse_scale), "value_changed", G_CALLBACK(phase_coarse_changed_cb), NULL);
  GtkWidget *phase_fine_label = gtk_label_new("Phase (fine)");
  gtk_widget_set_name(phase_fine_label, "boldlabel");
  gtk_widget_set_halign(phase_fine_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(mgrid), phase_fine_label, 0, 3, 2, 1);
  phase_fine_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -5.0, 5.0, 0.1);
  gtk_range_set_value(GTK_RANGE(phase_fine_scale), phase_fine);
  gtk_grid_attach(GTK_GRID(mgrid), phase_fine_scale, 2, 3, 8, 1);
  g_signal_connect(G_OBJECT(phase_fine_scale), "value_changed", G_CALLBACK(phase_fine_changed_cb), NULL);
  gtk_container_add(GTK_CONTAINER(mcontainer), mgrid);
  //
  // Container for the "automatic" case
  //
  acontainer = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), acontainer, 0, row, 11, 1);
  GtkWidget *agrid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(agrid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(agrid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(agrid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(agrid), 5);
  lbl = gtk_label_new("Measure on");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 0, 2, 1);
  btn = gtk_combo_box_text_new();

  for (int i = 0; i < DIV_REF_NROWS; i++) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), div_ref_rows[i].label);
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), div_ref_to_row(div_auto_ref));
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 0, 4, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(ref_changed_cb), NULL);
  btn = gtk_check_button_new_with_label("Follow RX Filter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), div_auto_follow_filter);
  gtk_grid_attach(GTK_GRID(agrid), btn, 7, 0, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(follow_cb), NULL);
  lbl = gtk_label_new("Window centre");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 1, 2, 1);
  //
  // Deliberately wide: the window is allowed outside the passband, and how
  // far is a function of the sample rate. div_bin_range() clamps to the
  // Nyquist limit for the rate in use and reports when it had to, which
  // the status line shows - a fixed range here would be wrong at three
  // rates out of four.
  //
  win_centre_btn = gtk_spin_button_new_with_range(-400000.0, 400000.0, 10.0);
  //gtk_widget_set_tooltip_text(centre_spin,
  //                            "Offset from the signal you are tuned to. In CW that is "
  //                            "the zero-beat note, one CW pitch away from the dial "
  //                           "frequency, so a centre of 0 sits on what you are "
  //                            "listening to in every mode.");
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(win_centre_btn), div_auto_centre);
  gtk_grid_attach(GTK_GRID(agrid), win_centre_btn, 2, 1, 4, 1);
  g_signal_connect(win_centre_btn, "value_changed", G_CALLBACK(centre_cb), NULL);
  lbl = gtk_label_new("Width");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 6, 1, 1, 1);
  win_width_btn = gtk_spin_button_new_with_range(20.0, 40000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(win_width_btn), div_auto_width);
  gtk_grid_attach(GTK_GRID(agrid), win_width_btn, 7, 1, 4, 1);
  g_signal_connect(win_width_btn, "value_changed", G_CALLBACK(width_cb), NULL);
  lbl = gtk_label_new("Resolution");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 2, 2, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "12 Hz bins (fast)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "6 Hz bins");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "3 Hz bins (weak signals)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn),
                           div_auto_resolution > 9.0 ? 0 : (div_auto_resolution > 4.5 ? 1 : 2));
  //gtk_widget_set_tooltip_text(res_combo,
  //                            "Finer bins lift a weak carrier further out of the noise, "
  //                            "but each step doubles the block period and so halves the "
  //                            "update rate. The bin width actually achieved is shown in "
  //                            "the status line.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 2, 4, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(res_changed_cb), NULL);
  lbl = gtk_label_new("Averaging (s)");
  //gtk_widget_set_tooltip_text(tau_label,
  //                            "Time constant for the gain/phase estimate. "
  //                            "Longer is steadier but follows fading more slowly. "
  //                            "RADE over an HF path usually wants several seconds; "
  //                            "a fast path - 20 m near the MUF, or the low bands - "
  //                            "wants a fraction of one, and Null wants the shortest "
  //                            "setting that still holds a lock. The scale is "
  //                            "geometric, so most of its travel is below five "
  //                            "seconds.");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 3, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, DIV_TAU_STEPS, 1.0);
  gtk_scale_set_digits(GTK_SCALE(btn), 2);
  g_signal_connect(G_OBJECT(btn), "format-value", G_CALLBACK(tau_format_cb), NULL);
  gtk_range_set_value(GTK_RANGE(btn), div_tau_to_pos(div_auto_tau));
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 3, 6, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(tau_cb), NULL);
  lbl = gtk_label_new("Min coher. (%)");
  coh_label = lbl;
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 4, 2, 1);
  //
  // Half-percent steps: the noise floor at the bottom of the travel is a
  // fraction of a percent on a wide window. See div_coh_range_update().
  //
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 95.0, 0.5);
  gtk_scale_set_digits(GTK_SCALE(btn), 1);
  gtk_range_set_value(GTK_RANGE(btn), 100.0 * div_auto_coherence_min);
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 4, 6, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(coh_cb), NULL);
  coh_scale = btn;
  //
  // "Status" info at the bottom
  //
  btn = gtk_toggle_button_new_with_label("Hold");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), div_auto_hold);
  //gtk_widget_set_tooltip_text(hold_b,
  //                            "Stop applying the loop's answer without stopping the "
  //                            "loop. The gain and phase controls become yours while "
  //                            "it is held, and releasing puts the tracked answer in "
  //                            "place in one step. The status line shows the tracked "
  //                            "value meanwhile, so the two can be compared.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 8, 3, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(hold_cb), NULL);
  hold_b = btn;
  btn = gtk_button_new_with_label("Invert");
  //gtk_widget_set_tooltip_text(invert_b,
  //                            "Swap Null and Sum. The two answers are 180 degrees "
  //                            "apart, so this is the quick way to tell whether the "
  //                            "array is pointed at the wanted signal or at the "
  //                            "interference. Does not apply to Best, which selects "
  //                            "an antenna rather than steering a null.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 8, 4, 3, 1);
  g_signal_connect(btn, "clicked", G_CALLBACK(invert_cb), NULL);
#ifdef DIVERSITY_CAPTURE
  //
  // DEVELOPMENT TOOL. Where the Hang slider was. A capture survives the
  // menu being closed, so the button is set before its handler is
  // connected and does not read as the operator pressing it. It cannot
  // work from a remote client: the file is written by the analysis
  // thread, on the radio.
  //
  divcap_b = gtk_toggle_button_new_with_label("Capture");
  gtk_widget_set_tooltip_text(divcap_b,
                              "Development tool. Record the two antenna streams as "
                              "the analysis thread sees them, for replaying offline. "
                              "Stops by itself at PIHPSDR_DIVCAP_SECONDS (default 60). "
                              "The label counts blocks written.");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(divcap_b), div_capture_active != 0);
  g_signal_connect(divcap_b, "toggled", G_CALLBACK(divcap_cb), NULL);
  gtk_grid_attach(GTK_GRID(agrid), divcap_b, 2, 5, 4, 1);

  if (radio_is_remote) { gtk_widget_set_sensitive(divcap_b, FALSE); }

#endif
  //
  // The status line spans both columns and is held to exactly
  // DIV_STATUS_CHARS characters, so it fits inside the width the controls
  // already need and cannot push the dialog wider whatever it has to say.
  //
  status_label = gtk_label_new("");
  gtk_widget_set_name(status_label, "boldlabel");
  gtk_widget_set_halign(status_label, GTK_ALIGN_FILL);
  gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
  gtk_widget_set_margin_start(status_label, DIV_STATUS_MARGIN);
  gtk_widget_set_margin_end(status_label, DIV_STATUS_MARGIN);
  gtk_label_set_width_chars(GTK_LABEL(status_label), DIV_STATUS_CHARS);
  gtk_label_set_max_width_chars(GTK_LABEL(status_label), DIV_STATUS_CHARS);
  gtk_grid_attach(GTK_GRID(agrid), status_label, 0, 8,10, 1);
  //
  // Second line, same treatment: monospace, the same fixed width, so the
  // two line up and neither can widen the dialog.
  //
  arm_label = gtk_label_new("");
  gtk_widget_set_name(arm_label, "boldlabel");
  gtk_widget_set_halign(arm_label, GTK_ALIGN_FILL);
  gtk_label_set_xalign(GTK_LABEL(arm_label), 0.0);
  gtk_widget_set_margin_start(arm_label, DIV_STATUS_MARGIN);
  gtk_widget_set_margin_end(arm_label, DIV_STATUS_MARGIN);
  gtk_label_set_width_chars(GTK_LABEL(arm_label), DIV_STATUS_CHARS);
  gtk_label_set_max_width_chars(GTK_LABEL(arm_label), DIV_STATUS_CHARS);
  gtk_grid_attach(GTK_GRID(agrid), arm_label, 0, 9, 10, 1);
  gtk_container_add(GTK_CONTAINER(acontainer), agrid);
  //
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  div_level_sensitive();

  //
  // No Min coherence row on RADE V1: see ref_changed_cb().
  //
  if (div_auto_ref == DIV_REF_RADE_V1) {
    gtk_widget_hide(coh_label);
    gtk_widget_hide(coh_scale);
  }

  //
  gtk_widget_set_sensitive(win_centre_btn, NOT(div_auto_follow_filter));
  gtk_widget_set_sensitive(win_width_btn, NOT(div_auto_follow_filter));
  gtk_widget_hide(div_auto_mode > 0 ? mcontainer : acontainer);
  gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
  status_timer = g_timeout_add(250, status_update_cb, NULL);
}
