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

#include "agc.h"
#include "band.h"
#include "ext.h"
#include "new_menu.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;
//
// These widgets are static since they need to be
// sensitve only if the AGC mode is "Custom"
//
static GtkWidget *cust_attack = NULL;
static GtkWidget *cust_decay = NULL;
static GtkWidget *cust_slope = NULL;
static GtkWidget *cust_hang = NULL;

static RECEIVER *myrx;

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

static void agc_hang_threshold_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->agc_hang_threshold = (int)gtk_range_get_value(GTK_RANGE(widget));
  rx_set_agc(myrx);
}

static void agc_cb (GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  gtk_widget_set_sensitive(cust_attack, (val == AGC_CUSTOM));
  gtk_widget_set_sensitive(cust_decay, (val == AGC_CUSTOM));
  gtk_widget_set_sensitive(cust_hang, (val == AGC_CUSTOM));
  gtk_widget_set_sensitive(cust_slope, (val == AGC_CUSTOM));
  //
  // When switching from some other AGC mode to FIXED, then set the AGC gain slider
  // to 60 dB, otherwise this may blow off your ears
  //
  if (val == AGC_FIXED && myrx->agc_gain > 60.0) {
    suppress_popup_sliders++;
    radio_set_agc_gain(myrx->id, 60.0);
    suppress_popup_sliders--;
  }
  myrx->agc = val;
  rx_set_agc(myrx);
  g_idle_add(ext_vfo_update, NULL);
}

static void agc_cust_attack_cb(GtkWidget *w, gpointer d) {
  myrx->agc_custom_attack = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
  rx_set_agc(myrx);
}

static void agc_cust_decay_cb(GtkWidget *w, gpointer d) {
  myrx->agc_custom_decay = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
  rx_set_agc(myrx);
}

static void agc_cust_hang_cb(GtkWidget *w, gpointer d) {
  myrx->agc_custom_hang = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
  rx_set_agc(myrx);
}

static void agc_cust_slope_cb(GtkWidget *w, gpointer d) {
  // Slope stored *10 (WDSP units), slider shows dB
  myrx->agc_custom_slope = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)) * 10.0;
  rx_set_agc(myrx);
}

void agc_menu(GtkWidget *parent) {
  GtkWidget *lbl;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  //
  // This guards against changing the active receiver while the menu is open
  //
  myrx = active_receiver;
  snprintf(title, sizeof(title), "piHPSDR - AGC (RX%d)", myrx->id + 1);
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
  GtkWidget *agc_title = gtk_label_new("AGC");
  gtk_widget_set_name(agc_title, "boldlabel");
  gtk_widget_set_halign(agc_title, GTK_ALIGN_END);
  gtk_widget_show(agc_title);
  gtk_grid_attach(GTK_GRID(grid), agc_title, 0, 1, 1, 1);
  GtkWidget *agc_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Off");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Long");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Slow");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Medium");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Fast");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Custom");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Fixed");
  gtk_combo_box_set_active(GTK_COMBO_BOX(agc_combo), myrx->agc);
  my_combo_attach(GTK_GRID(grid), agc_combo, 1, 1, 1, 1);
  g_signal_connect(agc_combo, "changed", G_CALLBACK(agc_cb), NULL);
  GtkWidget *agc_hang_threshold_label = gtk_label_new("Hang Threshold");
  gtk_widget_set_name(agc_hang_threshold_label, "boldlabel");
  gtk_widget_set_halign(agc_hang_threshold_label, GTK_ALIGN_END);
  gtk_widget_show(agc_hang_threshold_label);
  gtk_grid_attach(GTK_GRID(grid), agc_hang_threshold_label, 0, 2, 1, 1);
  GtkWidget *agc_hang_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_range_set_increments (GTK_RANGE(agc_hang_threshold_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(agc_hang_threshold_scale), myrx->agc_hang_threshold);
  gtk_widget_show(agc_hang_threshold_scale);
  gtk_grid_attach(GTK_GRID(grid), agc_hang_threshold_scale, 1, 2, 3, 1);
  g_signal_connect(G_OBJECT(agc_hang_threshold_scale), "value_changed", G_CALLBACK(agc_hang_threshold_value_changed_cb),
                   NULL);
  // AGC Custom mode section
  GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_attach(GTK_GRID(grid), sep2, 0, 3, 4, 1);
  lbl = gtk_label_new("AGC parameters for 'Custom' mode:");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 4, 4, 1);
  lbl = gtk_label_new("Attack (ms):");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 5, 1, 1);
  cust_attack = gtk_spin_button_new_with_range(1, 20, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cust_attack), myrx->agc_custom_attack);
  gtk_grid_attach(GTK_GRID(grid), cust_attack, 1, 5, 1, 1);
  g_signal_connect(cust_attack, "value-changed", G_CALLBACK(agc_cust_attack_cb), NULL);
  lbl = gtk_label_new("Decay (ms):");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, 5, 1, 1);
  cust_decay = gtk_spin_button_new_with_range(10, 5000, 10);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cust_decay), myrx->agc_custom_decay);
  gtk_grid_attach(GTK_GRID(grid), cust_decay, 3, 5, 1, 1);
  g_signal_connect(cust_decay, "value-changed", G_CALLBACK(agc_cust_decay_cb), NULL);
  lbl = gtk_label_new("Hang (ms):");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 6, 1, 1);
  cust_hang = gtk_spin_button_new_with_range(0, 5000, 10);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cust_hang), myrx->agc_custom_hang);
  gtk_grid_attach(GTK_GRID(grid), cust_hang, 1, 6, 1, 1);
  g_signal_connect(cust_hang, "value-changed", G_CALLBACK(agc_cust_hang_cb), NULL);
  lbl = gtk_label_new("Slope (dB):");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, 6, 1, 1);
  cust_slope = gtk_spin_button_new_with_range(0, 20, 0.5);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cust_slope), myrx->agc_custom_slope / 10.0);
  gtk_grid_attach(GTK_GRID(grid), cust_slope, 3, 6, 1, 1);
  g_signal_connect(cust_slope, "value-changed", G_CALLBACK(agc_cust_slope_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  gtk_widget_set_sensitive(cust_attack, (myrx->agc == AGC_CUSTOM));
  gtk_widget_set_sensitive(cust_decay, (myrx->agc == AGC_CUSTOM));
  gtk_widget_set_sensitive(cust_hang, (myrx->agc == AGC_CUSTOM));
  gtk_widget_set_sensitive(cust_slope, (myrx->agc == AGC_CUSTOM));
}
