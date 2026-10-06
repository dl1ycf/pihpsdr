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
#include "new_menu.h"
#include "radio.h"

static GtkWidget *dialog = NULL;

static double gain_coarse, gain_fine;
static double phase_coarse, phase_fine;

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
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

static void enable_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_diversity(state);
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
  int row = 0;
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 3, 1);
  btn = gtk_check_button_new_with_label("Diversity Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), diversity_enabled);
  gtk_grid_attach(GTK_GRID(grid), btn, 6, row, 4, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(enable_cb), NULL);
  row++;


  if (have_rx_att) {
    int rxadc = receiver[0]->adc;   // ADC of "primary" Antenna
    int otheradc = 1 - rxadc;       // ADC of "noisy" Antenna
    lbl = gtk_label_new("RX1 ATT:");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
    btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_range_set_value(GTK_RANGE(btn), adc[rxadc].attenuation);
    g_signal_connect(btn, "value_changed", G_CALLBACK(att_cb), GINT_TO_POINTER(rxadc));
    gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 4, 1);
    lbl = gtk_label_new("Other:");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 6, row, 1, 1);
    btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_range_set_value(GTK_RANGE(btn), adc[otheradc].attenuation);
    g_signal_connect(btn, "value_changed", G_CALLBACK(att_cb), GINT_TO_POINTER(otheradc));
    gtk_grid_attach(GTK_GRID(grid), btn, 7, row, 4, 1);
    row++;
  }

  sanitize_man_values();
  lbl = gtk_label_new("Gain (dB, coarse)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -25.0, +25.0, 0.5);
  gtk_range_set_value(GTK_RANGE(btn), gain_coarse);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 9, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(gain_coarse_changed_cb), NULL);
  row++;
  lbl = gtk_label_new("Gain (dB, fine)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -2.0, +2.0, 0.05);
  gtk_range_set_value(GTK_RANGE(btn), gain_fine);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 9, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(gain_fine_changed_cb), NULL);
  row++;
  lbl = gtk_label_new("Phase (coarse)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -180.0, 180.0, 2.0);
  gtk_range_set_value(GTK_RANGE(btn), phase_coarse);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 9, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(phase_coarse_changed_cb), NULL);
  row++;
  lbl = gtk_label_new("Phase (fine)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 2, 1);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -5.0, 5.0, 0.1);
  gtk_range_set_value(GTK_RANGE(btn), phase_fine);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 9, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(phase_fine_changed_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
