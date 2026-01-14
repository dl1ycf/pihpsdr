/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include <ctype.h>

#include "client_server.h"
#include "ext.h"
#include "iambic.h"
#include "message.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "radio.h"
#include "rigctl.h"

static GtkWidget *dialog = NULL;
static GtkWidget *options_container;
static GtkWidget *cwtxt_container;

static void cw_changed() {
  // inform the local keyer about CW parameter changes
  // NewProtocol: rely on periodically sent HighPrio packets
  keyer_update();

  if (!radio_is_remote) { schedule_transmit_specific(); }

  //
  // speed and side tone frequency are displayed in the VFO bar
  //
  g_idle_add(ext_vfo_update, NULL);
}

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

static void sel_cb(GtkWidget *widget, gpointer data) {
  GtkWidget *cnt = (GtkWidget *) data;

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(cnt);
    gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
  } else {
    gtk_widget_hide(cnt);
  }
}

static void text_cb(GtkWidget *widget, gpointer data) {
  char *cwtxt = (char *) data;
  const char *text = gtk_entry_get_text(GTK_ENTRY(widget));
  snprintf(cwtxt, 256, "%s", text);
  char *cp = cwtxt;
  while (*cp) {
    *cp = toupper(*cp);
    cp++;
  }
  gtk_entry_set_text(GTK_ENTRY(widget), cwtxt);
}

static void cw_keyer_internal_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_internal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_spacing_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_spacing = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_speed_value_changed_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_cw_speed(val);
}

static void cw_breakin_cb(GtkWidget *widget, gpointer data) {
  cw_breakin = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_hang_time_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_hang_time = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_weight_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_weight = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_keys_reversed_cb(GtkWidget *widget, gpointer data) {
  cw_keys_reversed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_mode_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  cw_keyer_mode = val;
  cw_changed();
}

static void cw_keyer_sidetone_level_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_sidetone_volume = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_sidetone_frequency_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_sidetone_frequency = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();

  if (radio_is_remote) {
    send_sidetone_freq(cl_sock_tcp, cw_keyer_sidetone_frequency);
  } else {
    rx_filter_changed(active_receiver);
    rx_set_offset(active_receiver);
    schedule_transmit_specific();
  }
}

#if 0
static void cw_ramp_width_changed_cb(GtkWidget *widget, gpointer data) {
  cw_ramp_width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  tx_set_ramps(transmitter);
  schedule_transmit_specific();
}

#endif

void cw_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  int row;
  GtkWidget *lbl, *btn, *selbtn;
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - CW");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(content), grid);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), TRUE);
  options_container = gtk_fixed_new();
  cwtxt_container = gtk_fixed_new();
  //
  // Top row in original grid: Close Button and Option/CWtext selection
  //
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
  selbtn = gtk_radio_button_new_with_label_from_widget(NULL,"CW Options");
  gtk_widget_set_name(selbtn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(selbtn), 1);
  gtk_grid_attach(GTK_GRID(grid), selbtn, 1, 0, 1, 1);
  g_signal_connect(selbtn, "toggled", G_CALLBACK(sel_cb), options_container);
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(selbtn),"CW Texts");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), 0);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, 0, 1,  1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), cwtxt_container);
  //
  // Options Container
  //
  gtk_grid_attach(GTK_GRID(grid), options_container, 0, 1, 3, 1);
  GtkWidget *op_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(op_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(op_grid), 5);
  gtk_grid_set_column_homogeneous (GTK_GRID(op_grid), TRUE);
  gtk_container_add(GTK_CONTAINER(options_container), op_grid);
  row=0;
  lbl = gtk_label_new("CW Speed (WPM)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(op_grid), lbl, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 60.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)cw_keyer_speed);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(cw_keyer_speed_value_changed_cb), NULL);

  if (!radio_is_remote) {
    btn = gtk_check_button_new_with_label("CW handled in Radio");
    gtk_widget_set_name(btn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), cw_keyer_internal);
    gtk_grid_attach(GTK_GRID(op_grid), btn, 2, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(cw_keyer_internal_cb), NULL);
  }

  row++;
  lbl = gtk_label_new("Hang time (ms)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(btn, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(op_grid), lbl, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.0, 1000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)cw_keyer_hang_time);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(cw_keyer_hang_time_value_changed_cb), NULL);
  btn = gtk_check_button_new_with_label("CW Break-In");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), cw_breakin);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(cw_breakin_cb), NULL);

  row++;
  lbl = gtk_label_new("Sidetone Level");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(op_grid), lbl, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.0, 127.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)cw_keyer_sidetone_volume);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(cw_keyer_sidetone_level_value_changed_cb), NULL);
  btn = gtk_check_button_new_with_label("Letter spacing");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), cw_keyer_spacing);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(cw_keyer_spacing_cb), NULL);

  row++;
  lbl = gtk_label_new("Sidetone Freq");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(op_grid), lbl, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(100.0, 1200.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)cw_keyer_sidetone_frequency);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(cw_keyer_sidetone_frequency_value_changed_cb), NULL);
  btn = gtk_check_button_new_with_label("Keys reversed");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), cw_keys_reversed);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(cw_keys_reversed_cb), NULL);

  row++;
  lbl = gtk_label_new("Weight");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(op_grid), lbl, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)cw_keyer_weight);
  gtk_grid_attach(GTK_GRID(op_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(cw_keyer_weight_value_changed_cb), NULL);

  row++;
  lbl = gtk_label_new("Paddle Mode");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(op_grid), lbl, 0, row, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Straight Key");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Iambic Mode A");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Iambic Mode B");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), cw_keyer_mode);
  my_combo_attach(GTK_GRID(op_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(cw_keyer_mode_cb), NULL);

  //
  // CW text container, with close button etc.
  //
  gtk_grid_attach(GTK_GRID(grid), cwtxt_container, 0, 1, 3, 1);
  GtkWidget *cw_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous (GTK_GRID(cw_grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(cw_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(cw_grid), 5);
  gtk_container_add(GTK_CONTAINER(cwtxt_container), cw_grid);
  row=0;
  lbl = gtk_label_new("Callsign (# token)");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(cw_grid), lbl, 0, 0, 2, 1);
  btn = gtk_entry_new();
  gtk_entry_set_width_chars(GTK_ENTRY(btn), 64);
  gtk_entry_set_text(GTK_ENTRY(btn), predef_call);
  gtk_grid_attach(GTK_GRID(cw_grid), btn, 2, 0, 8, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(text_cb), predef_call);
  for (int i = 0; i < 5; i++) {
    char text[256];
    lbl = gtk_label_new("");
    snprintf(text, sizeof(text), "CWTxt%d", i + 1);
    gtk_label_set_text(GTK_LABEL(lbl), text);
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(cw_grid), lbl, 0, i + 1, 1, 1);
    btn = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(btn), 64);
    snprintf(text, sizeof(text), "%s", predef_cwtxt[i]);
    gtk_entry_set_text(GTK_ENTRY(btn), text);
    gtk_grid_attach(GTK_GRID(cw_grid), btn, 1, i + 1, 9, 1);
    g_signal_connect(btn, "changed", G_CALLBACK(text_cb), predef_cwtxt[i]);
  }
    
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  gtk_widget_hide(cwtxt_container);
  gtk_widget_show(options_container);
  gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
}
