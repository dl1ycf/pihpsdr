/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
* 2025 - Christoph van Wüllen, DL1YCF
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "band.h"
#include "bandstack.h"
#include "ext.h"
#include "filter.h"
#include "mode.h"
#include "new_menu.h"
#include "radio.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;

static RECEIVER *myrx;

static GtkWidget *nr_container;
static GtkWidget *nb_container;
static GtkWidget *nr4_container;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static void nb_cb(GtkToggleButton *widget, gpointer data) {
  myrx->nb = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  rx_set_noise(myrx);
}

static void nr_cb(GtkToggleButton *widget, gpointer data) {
  myrx->nr = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  rx_set_noise(myrx);
}

static void anf_cb(GtkWidget *widget, gpointer data) {
  myrx->anf = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  rx_set_noise(myrx);
}

static void post_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_post = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  rx_set_noise(myrx);
}

static void snb_cb(GtkWidget *widget, gpointer data) {
  myrx->snb = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  rx_set_noise(myrx);
}

static void pos_cb(GtkWidget *widget, gpointer data) {
  myrx->nr_agc = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  rx_set_noise(myrx);
}

static void mode_cb(GtkWidget *widget, gpointer data) {
  myrx->nb2_mode = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  rx_set_noise(myrx);
}

static void gain_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_gain_method = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  rx_set_noise(myrx);
}

static void npe_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_npe_method = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  rx_set_noise(myrx);
}

static void trained_thr_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_trained_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void post_taper_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_post_taper = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  rx_set_noise(myrx);
}

static void post_nlevel_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_post_nlevel = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  rx_set_noise(myrx);
}

static void post_rate_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_post_rate = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  rx_set_noise(myrx);
}

static void post_factor_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_post_factor = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  rx_set_noise(myrx);
}

static void trained_t2_cb(GtkWidget *widget, gpointer data) {
  myrx->nr2_trained_t2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void slew_cb(GtkWidget *widget, gpointer data) {
  myrx->nb_tau = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void lead_cb(GtkWidget *widget, gpointer data) {
  myrx->nb_advtime = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void lag_cb(GtkWidget *widget, gpointer data) {
  myrx->nb_hang = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void thresh_cb(GtkWidget *widget, gpointer data) {
  myrx->nb_thresh = 0.165 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void nr_sel_changed(GtkWidget *widget, gpointer data) {
  // show or hide all controls for NR settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(nr_container);
  } else {
    gtk_widget_hide(nr_container);
  }
}

static void nb_sel_changed(GtkWidget *widget, gpointer data) {
  // show or hide all controls for NB settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(nb_container);
  } else {
    gtk_widget_hide(nb_container);
  }
}

static void nr4_sel_changed(GtkWidget *widget, gpointer data) {
  // show or hide all controls for NR4 settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(nr4_container);
  } else {
    gtk_widget_hide(nr4_container);
  }
}

static void nr4_reduction_cb(GtkWidget *widget, gpointer data) {
  myrx->nr4_reduction_amount = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void nr4_smoothing_cb(GtkWidget *widget, gpointer data) {
  myrx->nr4_smoothing_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void nr4_whitening_cb(GtkWidget *widget, gpointer data) {
  myrx->nr4_whitening_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void nr4_rescale_cb(GtkWidget *widget, gpointer data) {
  myrx->nr4_noise_rescale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

static void nr4_threshold_cb(GtkWidget *widget, gpointer data) {
  myrx->nr4_post_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_noise(myrx);
}

void noise_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *btn, *lbl;
  char title[64];
  //
  // This guards against changing the active receiver while the menu is open
  //
  myrx = active_receiver;
  snprintf(title, sizeof(title), "piHPSDR - Noise (RX%d VFO-%s)", myrx->id + 1, myrx->id == 0 ? "A" : "B");
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  //
  // First row: SNB/ANF/NR method
  //
  btn = gtk_check_button_new_with_label("SNB");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->snb);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 1, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(snb_cb), NULL);
  //
  btn = gtk_check_button_new_with_label("ANF");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->anf);
  gtk_widget_show(btn);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, 1, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(anf_cb), NULL);
  //
  lbl = gtk_label_new("Reduction");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, 1, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NONE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NR");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NR2");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NR3");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NR4");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myrx->nr);
  my_combo_attach(GTK_GRID(grid), btn, 3, 1, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(nr_cb), NULL);
  //
  // Second row: Position and NB selection
  //
  lbl = gtk_label_new("NR/NR2/ANF");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 2, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Pre AGC");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Post AGC");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myrx->nr_agc);
  my_combo_attach(GTK_GRID(grid), btn, 1, 2, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(pos_cb), NULL);
  //
  lbl = gtk_label_new("Blanker");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, 2, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NONE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NB2");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myrx->nb);
  my_combo_attach(GTK_GRID(grid), btn, 3, 2, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(nb_cb), NULL);
  GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(line, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), line, 0, 3, 4, 1);
  //
  // Third row: select settings: NR, NB, NR4 settings
  //
  GtkWidget *nr_sel = gtk_radio_button_new_with_label_from_widget(NULL, "NR2 Settings");
  gtk_widget_set_name(nr_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr_sel), 1);
  gtk_widget_show(nr_sel);
  gtk_grid_attach(GTK_GRID(grid), nr_sel, 0, 4, 1, 1);
  g_signal_connect(nr_sel, "toggled", G_CALLBACK(nr_sel_changed), NULL);
  //
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(nr_sel), "NB Settings");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), 0);
  gtk_widget_show(btn);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, 4, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(nb_sel_changed), NULL);
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(nr_sel), "NR4 Settings");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), 0);
  gtk_widget_show(btn);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, 4, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(nr4_sel_changed), NULL);
  //
  // Hiding/Showing ComboBoxes optimized for Touch-Screens does not
  // work. Therefore, we have to group the NR, NB, and NR4 controls
  // in a container, which then can be shown/hidden
  //
  //
  // NR controls
  //
  nr_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), nr_container, 0, 5, 4, 3);
  GtkWidget *nr_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nr_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nr_grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(nr_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(nr_grid), 5);
  //
  lbl = gtk_label_new("Gain Method");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 0, 0, 1, 1);
  //
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Linear");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Log");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Gamma");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Trained");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myrx->nr2_gain_method);
  my_combo_attach(GTK_GRID(nr_grid), btn, 1, 0, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(gain_cb), NULL);
  //
  lbl = gtk_label_new("NPE Method");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 2, 0, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "OSMS");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "MMSE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "NSTAT");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myrx->nr2_npe_method);
  my_combo_attach(GTK_GRID(nr_grid), btn, 3, 0, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(npe_cb), NULL);
  //
  lbl = gtk_label_new("Trained Thresh");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 0, 2, 1, 1);
  btn = gtk_spin_button_new_with_range(-5.0, 5.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->nr2_trained_threshold);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 1, 2, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(trained_thr_cb), NULL);
  //
  lbl = gtk_label_new("Trained T2");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 2, 2, 1, 1);
  btn = gtk_spin_button_new_with_range(0.02, 0.3, 0.01);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->nr2_trained_t2);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 3, 2, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(trained_t2_cb), NULL);
  //
  btn = gtk_check_button_new_with_label("NR2 Post-Processing");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->nr2_post);
  gtk_widget_show(btn);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 0, 3, 2, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(post_cb), NULL);
  //
  lbl = gtk_label_new("Post Level");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 0, 4, 1, 1);
  btn = gtk_spin_button_new_with_range(0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->nr2_post_nlevel);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 1, 4, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(post_nlevel_cb), NULL);
  lbl = gtk_label_new("Post Factor");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 2, 4, 1, 1);
  btn = gtk_spin_button_new_with_range(0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->nr2_post_factor);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 3, 4, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(post_factor_cb), NULL);
  //
  lbl = gtk_label_new("Post Rate");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 0, 5, 1, 1);
  btn = gtk_spin_button_new_with_range(0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->nr2_post_rate);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 1, 5, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(post_rate_cb), NULL);
  lbl = gtk_label_new("Post Taper");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_show(lbl);
  gtk_grid_attach(GTK_GRID(nr_grid), lbl, 2, 5, 1, 1);
  btn = gtk_spin_button_new_with_range(0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->nr2_post_taper);
  gtk_grid_attach(GTK_GRID(nr_grid), btn, 3, 5, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(post_taper_cb), NULL);
  gtk_container_add(GTK_CONTAINER(nr_container), nr_grid);
  //
  // NB controls starting on row 4
  //
  nb_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), nb_container, 0, 5, 4, 3);
  GtkWidget *nb_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nb_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nb_grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(nb_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(nb_grid), 5);
  //
  GtkWidget *mode_title = gtk_label_new("NB2 mode");
  gtk_widget_set_name(mode_title, "boldlabel");
  gtk_widget_set_halign(mode_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), mode_title, 0, 0, 1, 1);
  GtkWidget *mode_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Zero");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Sample&Hold");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Mean Hold");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Hold Sample");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Interpolate");
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), myrx->nb2_mode);
  my_combo_attach(GTK_GRID(nb_grid), mode_combo, 1, 0, 1, 1);
  g_signal_connect(mode_combo, "changed", G_CALLBACK(mode_cb), NULL);
  //
  GtkWidget *slew_title = gtk_label_new("Slew time (ms)");
  gtk_widget_set_name(slew_title, "boldlabel");
  gtk_widget_set_halign(slew_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), slew_title, 0, 1, 1, 1);
  GtkWidget *slew_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(slew_b), myrx->nb_tau * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), slew_b, 1, 1, 1, 1);
  g_signal_connect(slew_b, "changed", G_CALLBACK(slew_cb), NULL);
  //
  GtkWidget *lead_title = gtk_label_new("Lead time (ms)");
  gtk_widget_set_name(lead_title, "boldlabel");
  gtk_widget_set_halign(lead_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), lead_title, 2, 1, 1, 1);
  GtkWidget *lead_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lead_b), myrx->nb_advtime * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), lead_b, 3, 1, 1, 1);
  g_signal_connect(lead_b, "changed", G_CALLBACK(lead_cb), NULL);
  //
  GtkWidget *lag_title = gtk_label_new("Lag time (ms)");
  gtk_widget_set_name(lag_title, "boldlabel");
  gtk_widget_set_halign(lag_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), lag_title, 0, 2, 1, 1);
  GtkWidget *lag_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lag_b), myrx->nb_hang * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), lag_b, 1, 2, 1, 1);
  g_signal_connect(lag_b, "changed", G_CALLBACK(lag_cb), NULL);
  //
  GtkWidget *thresh_title = gtk_label_new("Threshold");
  gtk_widget_set_name(thresh_title, "boldlabel");
  gtk_widget_set_halign(thresh_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), thresh_title, 2, 2, 1, 1);
  GtkWidget *thresh_b = gtk_spin_button_new_with_range(15.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(thresh_b), myrx->nb_thresh * 6.0606060606); // 1.0/0.165
  gtk_grid_attach(GTK_GRID(nb_grid), thresh_b, 3, 2, 1, 1);
  g_signal_connect(thresh_b, "changed", G_CALLBACK(thresh_cb), NULL);
  gtk_container_add(GTK_CONTAINER(nb_container), nb_grid);
  //
  // NR4 controls starting at row 4
  //
  nr4_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), nr4_container, 0, 5, 4, 3);
  GtkWidget *nr4_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nr4_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nr4_grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(nr4_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(nr4_grid), 5);
  //
  GtkWidget *nr4_reduction_title = gtk_label_new("NR4 Reduction (dB)");
  gtk_widget_set_name(nr4_reduction_title, "boldlabel");
  gtk_widget_set_halign(nr4_reduction_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_reduction_title, 0, 0, 1, 1);
  GtkWidget *nr4_reduction_b = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_reduction_b), myrx->nr4_reduction_amount);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_reduction_b, 1, 0, 1, 1);
  g_signal_connect(G_OBJECT(nr4_reduction_b), "changed", G_CALLBACK(nr4_reduction_cb), NULL);
  //
  GtkWidget *nr4_smoothing_title = gtk_label_new("NR4 Smoothing (%)");
  gtk_widget_set_name(nr4_smoothing_title, "boldlabel");
  gtk_widget_set_halign(nr4_smoothing_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_smoothing_title, 2, 0, 1, 1);
  GtkWidget *nr4_smoothing_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_smoothing_b), myrx->nr4_smoothing_factor);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_smoothing_b, 3, 0, 1, 1);
  g_signal_connect(G_OBJECT(nr4_smoothing_b), "changed", G_CALLBACK(nr4_smoothing_cb), NULL);
  //
  GtkWidget *nr4_whitening_title = gtk_label_new("NR4 Whitening (%)");
  gtk_widget_set_name(nr4_whitening_title, "boldlabel");
  gtk_widget_set_halign(nr4_whitening_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_whitening_title, 0, 1, 1, 1);
  GtkWidget *nr4_whitening_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_whitening_b), myrx->nr4_whitening_factor);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_whitening_b, 1, 1, 1, 1);
  g_signal_connect(G_OBJECT(nr4_whitening_b), "changed", G_CALLBACK(nr4_whitening_cb), NULL);
  //
  GtkWidget *nr4_rescale_title = gtk_label_new("NR4 rescale (dB)");
  gtk_widget_set_name(nr4_rescale_title, "boldlabel");
  gtk_widget_set_halign(nr4_rescale_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_rescale_title, 2, 1, 1, 1);
  GtkWidget *nr4_rescale_b = gtk_spin_button_new_with_range(0.0, 12.0, 0.1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_rescale_b), myrx->nr4_noise_rescale);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_rescale_b, 3, 1, 1, 1);
  g_signal_connect(G_OBJECT(nr4_rescale_b), "changed", G_CALLBACK(nr4_rescale_cb), NULL);
  //
  GtkWidget *nr4_threshold_title = gtk_label_new("NR4 post filter threshold (dB)");
  gtk_widget_set_name(nr4_threshold_title, "boldlabel");
  gtk_widget_set_halign(nr4_threshold_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_threshold_title, 1, 2, 2, 1);
  GtkWidget *nr4_threshold_b = gtk_spin_button_new_with_range(-10.0, 10.0, 0.1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_threshold_b), myrx->nr4_post_threshold);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_threshold_b, 3, 2, 1, 1);
  g_signal_connect(G_OBJECT(nr4_threshold_b), "changed", G_CALLBACK(nr4_threshold_cb), NULL);
  //
  gtk_container_add(GTK_CONTAINER(nr4_container), nr4_grid);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  //
  // The width of the main grid is the largest, since it contains all containers.
  // Determine this width and set the width of all containers to that value.
  // This ensures that the column widths of he main grid and the containers
  // line up so the whole menu looks well aligned.
  //
  int width = gtk_widget_get_allocated_width(grid);
  gtk_widget_set_size_request(nr_grid, width, -1);
  gtk_widget_set_size_request(nb_grid, width, -1);
  gtk_widget_set_size_request(nr4_grid, width, -1);
  gtk_widget_hide(nb_container);
  gtk_widget_hide(nr4_container);
}
