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

static GtkWidget *dialog = NULL;
static GtkWidget *gain_coarse_scale = NULL;
static GtkWidget *gain_fine_scale = NULL;
static GtkWidget *phase_fine_scale = NULL;
static GtkWidget *phase_coarse_scale = NULL;

static GtkWidget *auto_combo = NULL;
static GtkWidget *ref_combo = NULL;
static GtkWidget *follow_b = NULL;
static GtkWidget *centre_spin = NULL;
static GtkWidget *width_spin = NULL;
static GtkWidget *tau_scale = NULL;
static GtkWidget *coh_scale = NULL;
static GtkWidget *status_label = NULL;

static double gain_coarse, gain_fine;
static double phase_coarse, phase_fine;

static guint status_timer = 0;

//
// Set while the status timer pushes automatically determined values into
// the gain/phase sliders, so that the "value_changed" handlers below can
// tell an operator adjustment from one of our own.
//
static int updating_from_auto = 0;

static void cleanup(void) {
  if (status_timer != 0) {
    g_source_remove(status_timer);
    status_timer = 0;
  }

  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gain_coarse_scale = NULL;
    gain_fine_scale = NULL;
    phase_coarse_scale = NULL;
    phase_fine_scale = NULL;
    auto_combo = NULL;
    ref_combo = NULL;
    follow_b = NULL;
    centre_spin = NULL;
    width_spin = NULL;
    tau_scale = NULL;
    coh_scale = NULL;
    status_label = NULL;
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

static void diversity_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_diversity(state);
}

static void gain_coarse_changed_cb(GtkWidget *widget, gpointer data) {
  if (updating_from_auto) { return; }

  gain_coarse = gtk_range_get_value(GTK_RANGE(widget));
  div_gain = gain_coarse + gain_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, div_gain, div_phase);
    return;
  }
  radio_calc_div_params();
}

static void gain_fine_changed_cb(GtkWidget *widget, gpointer data) {
  if (updating_from_auto) { return; }

  gain_fine = gtk_range_get_value(GTK_RANGE(widget));
  div_gain = gain_coarse + gain_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, div_gain, div_phase);
    return;
  }
  radio_calc_div_params();
}

static void phase_coarse_changed_cb(GtkWidget *widget, gpointer data) {
  if (updating_from_auto) { return; }

  phase_coarse = gtk_range_get_value(GTK_RANGE(widget));
  div_phase = phase_coarse + phase_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, div_gain, div_phase);
    return;
  }
  radio_calc_div_params();
}

static void phase_fine_changed_cb(GtkWidget *widget, gpointer data) {
  if (updating_from_auto) { return; }

  phase_fine = gtk_range_get_value(GTK_RANGE(widget));
  div_phase = phase_coarse + phase_fine;
  if (radio_is_remote) {
    send_diversity(cl_sock_tcp, diversity_enabled, div_gain, div_phase);
    return;
  }
  radio_calc_div_params();
}


//
// Which sideband the RADE window has placed itself on, for the status
// line. Worth showing: if this reads the wrong way round the correlator
// is looking at the mirror image of the signal and will never lock.
//
static const char *div_rade_side_text(void) {
  return (div_rade_side_get() < 0) ? "below" : "above";
}

static const char *div_mode_lsb_text(void) {
  int m = vfo[0].mode;
  return (m == modeLSB || m == modeDIGL || m == modeCWL) ? "LSB" : "USB";
}

//
// Manual gain/phase only make sense while the automatic loop is not
// driving them, so they are greyed out when it is.
//
static void update_manual_sensitivity(void) {
  gboolean manual = (div_auto_mode == DIV_AUTO_OFF);

  if (gain_coarse_scale)  { gtk_widget_set_sensitive(gain_coarse_scale, manual); }

  if (gain_fine_scale)    { gtk_widget_set_sensitive(gain_fine_scale, manual); }

  if (phase_coarse_scale) { gtk_widget_set_sensitive(phase_coarse_scale, manual); }

  if (phase_fine_scale)   { gtk_widget_set_sensitive(phase_fine_scale, manual); }

  //
  // The RADE and SAM-carrier references place their own window, so the
  // manual placement controls do not apply to them.
  //
  gboolean placed = (div_auto_ref == DIV_REF_BAND);
  gboolean manual_window = placed && !div_auto_follow_filter;

  if (follow_b)    { gtk_widget_set_sensitive(follow_b, placed); }

  if (centre_spin) { gtk_widget_set_sensitive(centre_spin, manual_window); }

  if (width_spin)  { gtk_widget_set_sensitive(width_spin,  manual_window); }
}

//
// Reflect what the analysis thread is doing. It only ever writes plain
// scalars, so the GUI polls them here rather than having a worker thread
// touch widgets.
//
static int status_update_cb(gpointer data) {
  char text[256];

  if (dialog == NULL) {
    status_timer = 0;
    return G_SOURCE_REMOVE;
  }

  if (div_auto_mode != DIV_AUTO_OFF) {
    //
    // Track the automatically determined values in the manual sliders so
    // the operator can see where the loop has settled, and so the sliders
    // start from there if auto is switched off.
    //
    updating_from_auto = 1;
    gain_coarse = 2.0 * round(0.5 * div_gain);

    if (gain_coarse >  25.0) { gain_coarse =  25.0; }

    if (gain_coarse < -25.0) { gain_coarse = -25.0; }

    gain_fine = div_gain - gain_coarse;
    phase_coarse = 4.0 * round(div_phase * 0.25);
    phase_fine = div_phase - phase_coarse;

    if (gain_fine >  2.0) { gain_fine =  2.0; }

    if (gain_fine < -2.0) { gain_fine = -2.0; }

    if (phase_fine >  5.0) { phase_fine =  5.0; }

    if (phase_fine < -5.0) { phase_fine = -5.0; }

    gtk_range_set_value(GTK_RANGE(gain_coarse_scale), gain_coarse);
    gtk_range_set_value(GTK_RANGE(gain_fine_scale), gain_fine);
    gtk_range_set_value(GTK_RANGE(phase_coarse_scale), phase_coarse);
    gtk_range_set_value(GTK_RANGE(phase_fine_scale), phase_fine);
    updating_from_auto = 0;
  }

  if (!div_auto_running) {
    snprintf(text, sizeof(text), "Auto off");
  } else if (div_auto_ref == DIV_REF_RADE_V1) {
    if (!rade_corr_locked) {
      snprintf(text, sizeof(text),
               "RADE V1: searching for pilot   (mode %s) - see log for correlation",
               div_mode_lsb_text());
    } else {
      //
      // "pilot" is the share of the energy in the pilot span that the
      // pilot itself accounts for, so it reads low under strong QRM even
      // though the correlator is tracking perfectly well - which is the
      // situation this mode exists for. Lock state is the thing to watch.
      //
      snprintf(text, sizeof(text),
               "RADE V1 LOCK  %s spectrum (mode %s)   pilot %3.0f%% / %+0.1f dB   %+0.1f Hz   %+0.1f dB %+0.0f deg",
               rade_corr_mirrored ? "mirrored" : "normal", div_mode_lsb_text(),
               100.0 * rade_corr_quality, rade_corr_snr,
               rade_corr_freq_off, div_gain, div_phase);
    }
  } else if (div_auto_ref == DIV_REF_RADE_BAND) {
    snprintf(text, sizeof(text),
             "RADE band %s carrier (mode %s)   coherence %3.0f%%   %s   %+0.1f dB %+0.0f deg",
             div_rade_side_text(), div_mode_lsb_text(), 100.0 * div_auto_coherence,
             div_auto_holding ? "HOLD" : "track", div_gain, div_phase);
  } else if (div_auto_ref == DIV_REF_CARRIER && !div_auto_carrier_valid) {
    //
    // The SAM PLL is only run in SAM; in plain AM the demodulator is an
    // envelope detector and there is no carrier frequency to be had.
    //
    snprintf(text, sizeof(text), "Needs SAM mode for the carrier PLL");
  } else if (div_auto_ref == DIV_REF_CARRIER) {
    snprintf(text, sizeof(text), "Carrier %+0.1f Hz   coherence %3.0f%%   %s   %+0.1f dB  %+0.0f deg",
             div_auto_carrier, 100.0 * div_auto_coherence,
             div_auto_holding ? "HOLD" : "track", div_gain, div_phase);
  } else {
    snprintf(text, sizeof(text), "Coherence %3.0f%%   %s   %+0.1f dB  %+0.0f deg",
             100.0 * div_auto_coherence,
             div_auto_holding ? "HOLD" : "track",
             div_gain, div_phase);
  }

  gtk_label_set_text(GTK_LABEL(status_label), text);
  return G_SOURCE_CONTINUE;
}

static void auto_changed_cb(GtkWidget *widget, gpointer data) {
  int previous = div_auto_mode;
  div_auto_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

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
  if ((previous == DIV_AUTO_OFF) != (div_auto_mode == DIV_AUTO_OFF)) {
    diversity_auto_restart();
  } else {
    //
    // Apply the new objective at once rather than slewing to it: this is
    // a deliberate operator action, usually to compare the two.
    //
    diversity_auto_jump();
  }

  update_manual_sensitivity();
}

static void ref_changed_cb(GtkWidget *widget, gpointer data) {
  int previous = div_auto_ref;
  div_auto_ref = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  //
  // On RADE the wanted signal is the one we are pointing at, so the
  // sensible objective is to maximise its SNR rather than to null the
  // strongest correlated thing in the window. Default to Sum on the way
  // in; the operator can still choose otherwise afterwards.
  //
  if (DIV_REF_IS_RADE(div_auto_ref) && !DIV_REF_IS_RADE(previous)) {
    div_auto_mode = DIV_AUTO_SUM;
    gtk_combo_box_set_active(GTK_COMBO_BOX(auto_combo), div_auto_mode);
  }

  //
  // The pilot correlator has its own front end, so it has to be brought
  // up or torn down when this changes rather than just re-aimed.
  //
  if (div_auto_ref == DIV_REF_RADE_V1 || previous == DIV_REF_RADE_V1) {
    diversity_auto_restart();
  }

  diversity_auto_reset();
  update_manual_sensitivity();
}

static void follow_cb(GtkWidget *widget, gpointer data) {
  div_auto_follow_filter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  diversity_auto_reset();
  update_manual_sensitivity();
}

static void centre_cb(GtkWidget *widget, gpointer data) {
  div_auto_centre = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  diversity_auto_reset();
}

static void width_cb(GtkWidget *widget, gpointer data) {
  div_auto_width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  diversity_auto_reset();
}

static void tau_cb(GtkWidget *widget, gpointer data) {
  div_auto_tau = gtk_range_get_value(GTK_RANGE(widget));
}

static void coh_cb(GtkWidget *widget, gpointer data) {
  div_auto_coherence_min = 0.01 * gtk_range_get_value(GTK_RANGE(widget));
}

static void reset_cb(GtkWidget *widget, gpointer data) {
  diversity_auto_reset();
}

void diversity_menu(GtkWidget *parent) {
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
  if (div_gain >  27.0) { div_gain = 27.0; }
  if (div_gain < -27.0) { div_gain = -27.0; }
  while (div_phase >  180.0) { div_phase -= 360.0; }
  while (div_phase < -180.0) { div_phase += 360.0; }
  gain_coarse = 2.0 * round(0.5 * div_gain);
  if (div_gain >  25.0) { gain_coarse = 25.0; }
  if (div_gain < -25.0) { gain_coarse = -25.0; }
  gain_fine = div_gain - gain_coarse;
  phase_coarse = 4.0 * round(div_phase * 0.25);
  phase_fine = div_phase - phase_coarse;
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *diversity_b = gtk_check_button_new_with_label("Diversity Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (diversity_b), diversity_enabled);
  gtk_widget_show(diversity_b);
  gtk_grid_attach(GTK_GRID(grid), diversity_b, 1, 0, 1, 1);
  g_signal_connect(diversity_b, "toggled", G_CALLBACK(diversity_cb), NULL);
  GtkWidget *gain_coarse_label = gtk_label_new("Gain (dB, coarse)");
  gtk_widget_set_name(gain_coarse_label, "boldlabel");
  gtk_widget_set_halign(gain_coarse_label, GTK_ALIGN_END);
  gtk_misc_set_alignment (GTK_MISC(gain_coarse_label), 0, 0);
  gtk_widget_show(gain_coarse_label);
  gtk_grid_attach(GTK_GRID(grid), gain_coarse_label, 0, 1, 1, 1);
  gain_coarse_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -25.0, +25.0, 0.5);
  gtk_widget_set_size_request (gain_coarse_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(gain_coarse_scale), gain_coarse);
  gtk_widget_show(gain_coarse_scale);
  gtk_grid_attach(GTK_GRID(grid), gain_coarse_scale, 1, 1, 1, 1);
  g_signal_connect(G_OBJECT(gain_coarse_scale), "value_changed", G_CALLBACK(gain_coarse_changed_cb), NULL);
  GtkWidget *gain_fine_label = gtk_label_new("Gain (dB, fine)");
  gtk_widget_set_name(gain_fine_label, "boldlabel");
  gtk_widget_set_halign(gain_fine_label, GTK_ALIGN_END);
  gtk_misc_set_alignment (GTK_MISC(gain_fine_label), 0, 0);
  gtk_widget_show(gain_fine_label);
  gtk_grid_attach(GTK_GRID(grid), gain_fine_label, 0, 2, 1, 1);
  gain_fine_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -2.0, +2.0, 0.05);
  gtk_widget_set_size_request (gain_fine_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(gain_fine_scale), gain_fine);
  gtk_widget_show(gain_fine_scale);
  gtk_grid_attach(GTK_GRID(grid), gain_fine_scale, 1, 2, 1, 1);
  g_signal_connect(G_OBJECT(gain_fine_scale), "value_changed", G_CALLBACK(gain_fine_changed_cb), NULL);
  GtkWidget *phase_coarse_label = gtk_label_new("Phase (coarse)");
  gtk_widget_set_name(phase_coarse_label, "boldlabel");
  gtk_widget_set_halign(phase_coarse_label, GTK_ALIGN_END);
  gtk_misc_set_alignment (GTK_MISC(phase_coarse_label), 0, 0);
  gtk_widget_show(phase_coarse_label);
  gtk_grid_attach(GTK_GRID(grid), phase_coarse_label, 0, 3, 1, 1);
  phase_coarse_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -180.0, 180.0, 2.0);
  gtk_widget_set_size_request (phase_coarse_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(phase_coarse_scale), phase_coarse);
  gtk_widget_show(phase_coarse_scale);
  gtk_grid_attach(GTK_GRID(grid), phase_coarse_scale, 1, 3, 1, 1);
  g_signal_connect(G_OBJECT(phase_coarse_scale), "value_changed", G_CALLBACK(phase_coarse_changed_cb), NULL);
  GtkWidget *phase_fine_label = gtk_label_new("Phase (fine)");
  gtk_widget_set_name(phase_fine_label, "boldlabel");
  gtk_widget_set_halign(phase_fine_label, GTK_ALIGN_END);
  gtk_misc_set_alignment (GTK_MISC(phase_fine_label), 0, 0);
  gtk_widget_show(phase_fine_label);
  gtk_grid_attach(GTK_GRID(grid), phase_fine_label, 0, 4, 1, 1);
  phase_fine_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -5.0, 5.0, 0.1);
  gtk_widget_set_size_request (phase_fine_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(phase_fine_scale), phase_fine);
  gtk_widget_show(phase_fine_scale);
  gtk_grid_attach(GTK_GRID(grid), phase_fine_scale, 1, 4, 1, 1);
  g_signal_connect(G_OBJECT(phase_fine_scale), "value_changed", G_CALLBACK(phase_fine_changed_cb), NULL);
  //
  // ------------------------------------------------------------------
  // Automatic phasing
  // ------------------------------------------------------------------
  //
  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_attach(GTK_GRID(grid), sep, 0, 5, 2, 1);
  GtkWidget *auto_label = gtk_label_new("Auto");
  gtk_widget_set_name(auto_label, "boldlabel");
  gtk_widget_set_halign(auto_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), auto_label, 0, 6, 1, 1);
  auto_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_combo), "Off (manual)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_combo), "Null (cancel common signal)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auto_combo), "Sum (co-phase antennas)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(auto_combo), div_auto_mode);
  gtk_grid_attach(GTK_GRID(grid), auto_combo, 1, 6, 1, 1);
  g_signal_connect(auto_combo, "changed", G_CALLBACK(auto_changed_cb), NULL);
  GtkWidget *ref_label = gtk_label_new("Measure on");
  gtk_widget_set_name(ref_label, "boldlabel");
  gtk_widget_set_halign(ref_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), ref_label, 0, 7, 1, 1);
  ref_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ref_combo), "Window (wideband)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ref_combo), "SAM carrier (PLL)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ref_combo), "RADE passband");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ref_combo), "RADE V1 pilot (MVDR)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(ref_combo), div_auto_ref);
  gtk_grid_attach(GTK_GRID(grid), ref_combo, 1, 7, 1, 1);
  g_signal_connect(ref_combo, "changed", G_CALLBACK(ref_changed_cb), NULL);
  follow_b = gtk_check_button_new_with_label("Window follows RX filter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow_b), div_auto_follow_filter);
  gtk_grid_attach(GTK_GRID(grid), follow_b, 1, 8, 1, 1);
  g_signal_connect(follow_b, "toggled", G_CALLBACK(follow_cb), NULL);
  GtkWidget *centre_label = gtk_label_new("Window centre (Hz)");
  gtk_widget_set_name(centre_label, "boldlabel");
  gtk_widget_set_halign(centre_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), centre_label, 0, 9, 1, 1);
  centre_spin = gtk_spin_button_new_with_range(-20000.0, 20000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(centre_spin), div_auto_centre);
  gtk_grid_attach(GTK_GRID(grid), centre_spin, 1, 9, 1, 1);
  g_signal_connect(centre_spin, "value_changed", G_CALLBACK(centre_cb), NULL);
  GtkWidget *width_label = gtk_label_new("Window width (Hz)");
  gtk_widget_set_name(width_label, "boldlabel");
  gtk_widget_set_halign(width_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), width_label, 0, 10, 1, 1);
  width_spin = gtk_spin_button_new_with_range(20.0, 40000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), div_auto_width);
  gtk_grid_attach(GTK_GRID(grid), width_spin, 1, 10, 1, 1);
  g_signal_connect(width_spin, "value_changed", G_CALLBACK(width_cb), NULL);
  GtkWidget *tau_label = gtk_label_new("Averaging (s)");
  gtk_widget_set_tooltip_text(tau_label,
                              "Time constant for the gain/phase estimate. "
                              "Longer is steadier but follows fading more slowly. "
                              "RADE over an HF path usually wants several seconds.");
  gtk_widget_set_name(tau_label, "boldlabel");
  gtk_widget_set_halign(tau_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), tau_label, 0, 11, 1, 1);
  tau_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.2, 30.0, 0.1);
  gtk_widget_set_size_request(tau_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(tau_scale), div_auto_tau);
  gtk_grid_attach(GTK_GRID(grid), tau_scale, 1, 11, 1, 1);
  g_signal_connect(G_OBJECT(tau_scale), "value_changed", G_CALLBACK(tau_cb), NULL);
  GtkWidget *coh_label = gtk_label_new("Min coherence (%)");
  gtk_widget_set_name(coh_label, "boldlabel");
  gtk_widget_set_halign(coh_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), coh_label, 0, 12, 1, 1);
  coh_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 95.0, 5.0);
  gtk_widget_set_size_request(coh_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(coh_scale), 100.0 * div_auto_coherence_min);
  gtk_grid_attach(GTK_GRID(grid), coh_scale, 1, 12, 1, 1);
  g_signal_connect(G_OBJECT(coh_scale), "value_changed", G_CALLBACK(coh_cb), NULL);
  GtkWidget *reset_b = gtk_button_new_with_label("Restart averaging");
  gtk_grid_attach(GTK_GRID(grid), reset_b, 0, 13, 1, 1);
  g_signal_connect(reset_b, "clicked", G_CALLBACK(reset_cb), NULL);
  status_label = gtk_label_new("");
  gtk_widget_set_halign(status_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), status_label, 1, 13, 1, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  update_manual_sensitivity();

  if (radio_is_remote) {
    //
    // The samples are combined on the server, so there is nothing here to
    // analyse. Manual gain/phase still work, they are sent over the wire.
    //
    gtk_widget_set_sensitive(auto_combo, FALSE);
    gtk_widget_set_sensitive(ref_combo, FALSE);
    gtk_widget_set_sensitive(follow_b, FALSE);
    gtk_widget_set_sensitive(centre_spin, FALSE);
    gtk_widget_set_sensitive(width_spin, FALSE);
    gtk_widget_set_sensitive(tau_scale, FALSE);
    gtk_widget_set_sensitive(coh_scale, FALSE);
    gtk_widget_set_sensitive(reset_b, FALSE);
    gtk_label_set_text(GTK_LABEL(status_label), "Auto phasing runs on the radio side only");
    return;
  }

  status_timer = g_timeout_add(250, status_update_cb, NULL);
}
