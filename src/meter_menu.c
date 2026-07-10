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

#include "client_server.h"
#include "meter.h"
#include "new_menu.h"
#include "radio.h"
#include "receiver.h"

static GtkWidget *dialog = NULL;

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

static void extended_meter_cb(GtkWidget *widget, gpointer data) {
  extended_meter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_reconfigure_screen();
}

static void txmeter_cb (GtkToggleButton *widget, gpointer data) {
  transmitter->metermode = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget));
  send_meter(cl_sock_tcp, active_receiver->smetermode, transmitter->metermode, transmitter->alcmode);
}

static void smeter_cb (GtkToggleButton *widget, gpointer data) {
  int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget));
  if (val) {
    active_receiver->smetermode = SMETER_PEAK;
  } else {
    active_receiver->smetermode = SMETER_AVERAGE;
  }
  if (radio_is_remote) {
    int alcmode = 0;
    int metermode = 0;
    if (can_transmit) {
      alcmode = transmitter->alcmode;
      metermode = transmitter->metermode;
    }
    send_meter(cl_sock_tcp, active_receiver->smetermode, metermode, alcmode);
  }
}

static void meter_type_cb (GtkToggleButton *widget, gpointer        data) {
  meter_type = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
}

static void alc_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget));
  if (val) {
    transmitter->alcmode = ALC_PEAK;
  } else {
    transmitter->alcmode = ALC_AVERAGE;
  }
  if (radio_is_remote) {
    send_meter(cl_sock_tcp, active_receiver->smetermode, transmitter->metermode, transmitter->alcmode);
  }
}

void meter_menu (GtkWidget *parent) {
  GtkWidget *lbl;
  GtkWidget *btn, *mbtn;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Meter");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  //
  btn = gtk_check_button_new_with_label("Enable extended metering");
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 1, 3, 1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), extended_meter);
  g_signal_connect(btn, "toggled", G_CALLBACK(extended_meter_cb), NULL);
  //
  lbl = gtk_label_new("Meter Type");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 2, 1, 1);
  //
  // Combo-box with choices
  //
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Digital");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Analog");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Edgewise");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "DualScale");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), meter_type);
  g_signal_connect(btn, "changed", G_CALLBACK(meter_type_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, 2, 2, 1);
  //
  lbl = gtk_label_new("S-Meter Reading");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 3, 1, 1);
  mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "Peak");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), active_receiver->smetermode == SMETER_PEAK);
  g_signal_connect(mbtn, "toggled", G_CALLBACK(smeter_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), mbtn, 1, 3, 1, 1);
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Average");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), !(active_receiver->smetermode == SMETER_PEAK));
  gtk_grid_attach(GTK_GRID(grid), btn, 2, 3, 1, 1);
  if (can_transmit) {
    lbl = gtk_label_new("TX Pwr Reading");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, 4, 1, 1);
    mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "Peak");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), !transmitter->metermode);
    g_signal_connect(mbtn, "toggled", G_CALLBACK(txmeter_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), mbtn, 1, 4, 1, 1);
    btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Average");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), transmitter->metermode);
    gtk_grid_attach(GTK_GRID(grid), btn, 2, 4, 1, 1);
    lbl = gtk_label_new("TX ALC Reading");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, 5, 1, 1);
    mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "Peak");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), transmitter->alcmode == ALC_PEAK);
    g_signal_connect(mbtn, "toggled", G_CALLBACK(alc_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), mbtn, 1, 5, 1, 1);
    btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Average");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), !(transmitter->alcmode == ALC_PEAK));
    gtk_grid_attach(GTK_GRID(grid), btn, 2, 5, 1, 1);
  }
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
