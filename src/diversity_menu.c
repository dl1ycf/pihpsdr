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
#include "vfo.h"

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
static GtkWidget *mcontainer = NULL;
static GtkWidget *acontainer = NULL;
static GtkWidget *status_label = NULL;
static GtkWidget *arm_label = NULL;

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
// so they are fixed and new ones go on the end; the list is ordered by
// how often a reference is reached for, which puts the two general
// purpose references first and the two that need a particular signal to
// be present after them. The table below is the only place the two
// orders meet - everything else works in DIV_REF_* - so adding a
// reference means adding one line here.
//

static double gain_coarse, gain_fine;
static double phase_coarse, phase_fine;

static guint status_timer = 0;

//
// Set while a settings block from the radio is being pushed into the
// widgets, so the "changed" handlers do not bounce it straight back.
//
static int updating_from_server = 0;

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
  if (updating_from_server) { return; }

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

//
// Set while the status timer pushes automatically determined values into
// the gain/phase sliders, so that the "value_changed" handlers below can
// tell an operator adjustment from one of our own.
//
static int updating_from_auto = 0;

//
// Set while ref_changed_cb() is driving the objective combo. Without it,
// modo_changed_cb() sees div_auto_mode already changed and concludes the
// engine does not need starting - so selecting a RADE reference with Auto
// set to Off silently started nothing at all.
//
static int updating_ref = 0;

static void cleanup(void) {
  if (status_timer != 0) {
    g_source_remove(status_timer);
    status_timer = 0;
  }

  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    //
    // Hold is an operating state with no indicator outside this dialog,
    // so leaving it set with the dialog shut would silently stop the loop
    // applying anything with nothing on screen to explain it.
    //
    diversity_auto_set_hold(0);
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void enable_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  //
  // This starts or stops the analysis thread, so what the controls below
  // may be used for changes with it.
  //
  radio_set_diversity(state);
}

static void att_cb(GtkWidget *widget, gpointer data) {
  radio_set_adc_attenuation(GPOINTER_TO_INT(data),
                            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget)));
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

    if (div_auto_mode == DIV_AUTO_BEST) {
      snprintf(sel, sizeof(sel), "  using ADC%d", div_auto_arm_pick);
    }

    snprintf(text, sizeof(text), "Antennas  ADC%d better by %4.1f dB%s",
             (div_auto_arm_db > 0.0) ? 1 : 0, d, sel);
  }

  gtk_label_set_text(GTK_LABEL(arm_label), text);
}

static int status_update_cb(gpointer data) {
  if (dialog == NULL) {
    status_timer = 0;
    return G_SOURCE_REMOVE;
  }

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

  div_arm_status_set();

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
      //
      // One decimal, and none at all past 10 kHz: the field is ten
      // characters and "+400000 Hz" is exactly that.
      //
      snprintf(detail, sizeof(detail),
               (fabs(div_auto_carrier) < 10000.0) ? "%+.1f Hz" : "%+.0f Hz",
               div_auto_carrier);
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
      // That is a fade, not a loss - the lock is kept for the Hang time.
      //
      state = div_auto_hold ? "HOLD" : (div_auto_holding ? "fade" : "LOCK");
      snprintf(detail, sizeof(detail), "%s %3.0f%%",
               div_rade_side_text(), 100.0 * rade_corr_quality);
    } else {
      state = rade_corr_confirming ? "confrm" : "search";
      snprintf(detail, sizeof(detail), "%s", div_rade_side_text());
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

static void mode_changed_cb(GtkWidget *widget, gpointer data) {
  int previous = div_auto_mode;
  div_auto_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (div_auto_mode == DIV_MANUAL) {
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
  // This sends a signal, so auto_cb does the rest
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
// The window controls are modal: the Window, Carrier and FSK/Digital
// references each keep their own centre and width, so aiming the carrier
// tracker at a station 5 kHz away does not destroy the window set up for
// wideband work, and going back restores it.
//
#if 0
//
// Push the settings globals into the widgets, without the handlers
// bouncing them straight back to the radio.
//
static void div_populate_from_settings(void) {
  if (dialog == NULL) { return; }

  updating_from_server = 1;

  if (auto_combo)   { gtk_combo_box_set_active(GTK_COMBO_BOX(auto_combo), div_auto_mode); }

  if (ref_combo)    { gtk_combo_box_set_active(GTK_COMBO_BOX(ref_combo), div_ref_to_row(div_auto_ref)); }

  if (follow_b)     { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow_b), div_auto_follow_filter); }

  if (centre_spin)  { gtk_spin_button_set_value(GTK_SPIN_BUTTON(centre_spin), div_auto_centre); }

  if (width_spin)   { gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), div_auto_width); }

  if (weight_combo) { gtk_combo_box_set_active(GTK_COMBO_BOX(weight_combo), div_auto_weighting); }

  if (tau_scale)    { gtk_range_set_value(GTK_RANGE(tau_scale), div_tau_to_pos(div_auto_tau)); }

  if (hang_scale)   { gtk_range_set_value(GTK_RANGE(hang_scale), div_auto_hang); }

  if (coh_scale)    { gtk_range_set_value(GTK_RANGE(coh_scale), 100.0 * div_auto_coherence_min); }

  if (hold_b)       { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hold_b), div_auto_hold); }

  if (res_combo) {
    //
    // The combo is an index into { 12, 6, 3 } Hz; the setting is the bin
    // width itself. Match on midpoints so a value that has been through a
    // double round trip still lands on its own entry.
    //
    const int i = (div_auto_resolution < 4.5) ? 2 : (div_auto_resolution < 9.0) ? 1 : 0;
    gtk_combo_box_set_active(GTK_COMBO_BOX(res_combo), i);
  }

  updating_from_server = 0;
}
#endif

//
// The other end changed something. Used on the radio when a client moves
// a control, so a menu open on both stays in step.
//
void diversity_menu_refresh(void) {
  //div_populate_from_settings();
}

//
// The radio changed mode and diversity_auto_mode_changed() has swapped
// one block of modal settings for another. See diversity_menu.h.
//
gboolean diversity_menu_settings_changed(gpointer data) {
  (void)data;
  //div_populate_from_settings();
  div_send_settings(DIV_ACTION_NONE);
  return G_SOURCE_REMOVE;
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
  st.indep_att       = d->indep_att;
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
  diversity_auto_apply_status(&st);

  g_free(data);
  return G_SOURCE_REMOVE;
}

//
// The engine owns the swap itself - window pair and coherence threshold
// together - because the threshold has to follow the reference on every
// path, not only this one. These wrap it and move the widgets afterwards.
//
static void div_window_store(int ref) {
  diversity_auto_ref_store(ref);
}

#if 0
static void div_window_recall(int ref) {
  diversity_auto_ref_recall(ref);

  if (coh_scale) {
    updating_from_auto = 1;
    gtk_range_set_value(GTK_RANGE(coh_scale), 100.0 * div_auto_coherence_min);
    updating_from_auto = 0;
  }

  if (centre_spin) {
    updating_from_auto = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(centre_spin), div_auto_centre);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), div_auto_width);
    updating_from_auto = 0;
  }
}
#endif

static void ref_changed_cb(GtkWidget *widget, gpointer data) {
  int previous = div_auto_ref;
  int was_off = (div_auto_mode == DIV_MANUAL);
  div_window_store(previous);
  div_auto_ref = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  //div_window_recall(div_auto_ref);

  //
  // On RADE V1 the wanted signal is the one the pilot correlator is
  // pointing at, so the sensible objective is to maximise its SNR rather
  // than to null the strongest correlated thing in the window. Default to
  // Sum on the way in; the operator can still choose otherwise
  // afterwards.
  //
  if (div_auto_ref == DIV_REF_RADE_V1 && previous != DIV_REF_RADE_V1) {
    div_auto_mode = DIV_AUTO_SUM;
    updating_ref = 1;
    //gtk_combo_box_set_active(GTK_COMBO_BOX(auto_combo), div_auto_mode);
    updating_ref = 0;
  }

  //
  // Restart if the analysis thread has to come up or go down - which
  // includes the case just above, where selecting a RADE reference moved
  // the objective off Off - or if the pilot correlator's own front end
  // has to be built or torn down.
  //
  if (was_off != (div_auto_mode == DIV_MANUAL) ||
      div_auto_ref == DIV_REF_RADE_V1 || previous == DIV_REF_RADE_V1) {
    diversity_auto_restart();
  }

  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

static void follow_cb(GtkWidget *widget, gpointer data) {
  div_auto_follow_filter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

static void centre_cb(GtkWidget *widget, gpointer data) {
  if (updating_from_auto) { return; }

  div_auto_centre = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  div_window_store(div_auto_ref);
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

static void width_cb(GtkWidget *widget, gpointer data) {
  if (updating_from_auto) { return; }

  div_auto_width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  div_window_store(div_auto_ref);
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

static void hang_cb(GtkWidget *widget, gpointer data) {
  (void)data;
  div_auto_hang = gtk_range_get_value(GTK_RANGE(widget));
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
  // div_window_recall() moves this slider when the reference changes, and
  // the slider's 5 % step would quantise the recalled value on the way
  // back in.
  //
  if (updating_from_auto) { return; }

  div_auto_coherence_min = 0.01 * gtk_range_get_value(GTK_RANGE(widget));
  //
  // Straight into the selected reference's own slot as well, so that a
  // settings block sent from here carries the new value rather than
  // whatever was stored at the last reference change.
  //
  diversity_auto_ref_store(div_auto_ref);
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

static void weight_changed_cb(GtkWidget *widget, gpointer data) {
  div_auto_weighting = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_NONE);
}

// cppcheck-suppress constParameterCallback
static void reset_cb(GtkWidget *widget, gpointer data) {
  //
  // The one control that changes no setting, so it cannot be seen as a
  // difference between two blocks and travels as an action instead.
  //
  diversity_auto_reset();
  div_send_settings(DIV_ACTION_RESET);
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
    lbl = gtk_label_new("ATT ADC0:");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
    btn = gtk_spin_button_new_with_range(0.0, 31.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), adc[0].attenuation);
    g_signal_connect(btn, "value_changed", G_CALLBACK(att_cb), GINT_TO_POINTER(0));
    gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 4, 1);
    lbl = gtk_label_new("ADC1:");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 6, row, 1, 1);
    btn = gtk_spin_button_new_with_range(0.0, 31.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), adc[1].attenuation);
    g_signal_connect(btn, "value_changed", G_CALLBACK(att_cb), GINT_TO_POINTER(1));
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
  GtkWidget *gain_coarse_label = gtk_label_new("Gain (dB, coarse)");
  gtk_widget_set_name(gain_coarse_label, "boldlabel");
  gtk_widget_set_halign(gain_coarse_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(mgrid), gain_coarse_label, 0, 0, 2, 1);
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
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "Window (wideband)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "FSK/Digital (occupancy MVDR)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "Carrier (AM/SAM)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "RADE V1 pilot (MVDR)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), div_auto_ref);
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 0, 4, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(ref_changed_cb), NULL);
  btn = gtk_check_button_new_with_label("Window follows RX filter");
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
  btn = gtk_spin_button_new_with_range(-400000.0, 400000.0, 10.0);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(btn), 0);
  //gtk_widget_set_tooltip_text(centre_spin,
  //                            "Offset from the signal you are tuned to. In CW that is "
  //                            "the zero-beat note, one CW pitch away from the dial "
  //                           "frequency, so a centre of 0 sits on what you are "
  //                            "listening to in every mode.");
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), div_auto_centre);
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 1, 4, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(centre_cb), NULL);
  lbl = gtk_label_new("Width");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 6, 1, 1, 1);
  btn = gtk_spin_button_new_with_range(20.0, 40000.0, 10.0);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(btn), 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), div_auto_width);
  gtk_grid_attach(GTK_GRID(agrid), btn, 7, 1, 4, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(width_cb), NULL);
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
  lbl = gtk_label_new("Weighting");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  //gtk_grid_attach(GTK_GRID(agrid), lbl, 6, 2, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "Weighting: Flat");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "Weighting: Coherence");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), div_auto_weighting);
  //gtk_widget_set_tooltip_text(weight_combo,
  //                            "Coherence weights each frequency bin by how well the two "
  //                            "antennas agree in it, so a wide window can be used on "
  //                            "speech without the noise-only parts of it diluting the "
  //                            "answer. Flat is the older behaviour.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 7, 2, 4, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(weight_changed_cb), NULL);
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
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 4, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 95.0, 5.0);
  gtk_range_set_value(GTK_RANGE(btn), 100.0 * div_auto_coherence_min);
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 4, 6, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(coh_cb), NULL);
  lbl = gtk_label_new("Hang (s)");
  //gtk_widget_set_tooltip_text(hang_label,
  //                            "How long a RADE lock is held after the pilot stops "
  //                            "being detectable, before the correlator gives up and "
  //                            "searches again. Long rides out a fade on one station. "
  //                            "Short is what a frequency several stations take turns "
  //                            "on wants: each has its own best gain and phase, and "
  //                            "until the lock is dropped the previous station's is "
  //                            "still being applied.");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(agrid), lbl, 0, 5, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 30.0, 0.5);
  gtk_range_set_value(GTK_RANGE(btn), div_auto_hang);
  gtk_grid_attach(GTK_GRID(agrid), btn, 2, 5, 6, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(hang_cb), NULL);
  //
  // "Status" info at the bottom
  //
  btn = gtk_button_new_with_label("Restart averaging");
  //gtk_widget_set_tooltip_text(reset_b,
  //                            "Discard the accumulated statistics and start the "
  //                            "estimate again from nothing.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 8, 3, 3, 1);
  g_signal_connect(btn, "clicked", G_CALLBACK(reset_cb), NULL);
  btn = gtk_toggle_button_new_with_label("Hold");
  //gtk_widget_set_tooltip_text(hold_b,
  //                            "Stop applying the loop's answer without stopping the "
  //                            "loop. The gain and phase controls become yours while "
  //                            "it is held, and releasing puts the tracked answer in "
  //                            "place in one step. The status line shows the tracked "
  //                            "value meanwhile, so the two can be compared.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 8, 4, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(hold_cb), NULL);
  btn = gtk_button_new_with_label("Invert");
  //gtk_widget_set_tooltip_text(invert_b,
  //                            "Swap Null and Sum. The two answers are 180 degrees "
  //                            "apart, so this is the quick way to tell whether the "
  //                            "array is pointed at the wanted signal or at the "
  //                            "interference. Does not apply to Best, which selects "
  //                            "an antenna rather than steering a null.");
  gtk_grid_attach(GTK_GRID(agrid), btn, 8, 5, 3, 1);
  g_signal_connect(btn, "clicked", G_CALLBACK(invert_cb), NULL);
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
  //
 gtk_widget_hide(div_auto_mode > 0 ? mcontainer : acontainer);
 gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
 status_timer = g_timeout_add(250, status_update_cb, NULL);
}
