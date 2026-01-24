/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "client_server.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "server_menu.h"

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

static void server_enable_cb(GtkWidget *widget, gpointer data) {
  hpsdr_server = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (hpsdr_server) {
    create_hpsdr_server();
  } else {
    destroy_hpsdr_server();
  }
}

static void server_stop_cb(GtkWidget *widget, gpointer data) {
  server_stops_protocol = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void port_value_changed_cb(GtkWidget *widget, gpointer data) {
  listen_port = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void pwd_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  snprintf(hpsdr_pwd, sizeof(hpsdr_pwd), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
}

void server_menu(GtkWidget *parent) {
  GtkWidget *lbl;
  GtkWidget *btn;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Server");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  //
  btn = gtk_button_new_with_label("Close");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
  //
  btn = gtk_check_button_new_with_label("Server Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), hpsdr_server);
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 1, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(server_enable_cb), NULL);
  //
  btn = gtk_check_button_new_with_label("Server runs protocol only if client is connected");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), server_stops_protocol);
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 2, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(server_stop_cb), NULL);
  //
  lbl = gtk_label_new("Server Port");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 3, 1, 1);
  //
  btn = gtk_spin_button_new_with_range(45000, 55000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)listen_port);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, 3, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(port_value_changed_cb), NULL);
  //
  lbl = gtk_label_new("Server Password");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 4, 1, 1);
  //
  btn = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(btn), hpsdr_pwd);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, 4, 2, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(pwd_cb), NULL);
  //
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}

