/* Copyright (C)
*  2026 - Christoph van Wüllen, DL1YCF
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

#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "profiles.h"
#include "radio.h"

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


// cppcheck-suppress constParameterCallback
static gboolean load_rx_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  int m = GPOINTER_TO_INT(data);
  profiles_load_rx_profile(active_receiver, m);
  return FALSE;
}

// cppcheck-suppress constParameterCallback
static gboolean save_rx_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  int m = GPOINTER_TO_INT(data);
  profiles_save_rx_profile(active_receiver, m);
  return FALSE;
}

// cppcheck-suppress constParameterCallback
static gboolean load_tx_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (can_transmit) {
    int m = GPOINTER_TO_INT(data);
    profiles_load_tx_profile(transmitter, m);
  }

  return FALSE;
}

// cppcheck-suppress constParameterCallback
static gboolean save_tx_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (can_transmit) {
    int m = GPOINTER_TO_INT(data);
    profiles_save_tx_profile(transmitter, m);
  }

  return FALSE;
}

const char *names[NUMPROFILES] = {"Audiophile", "Rag Chew", "Contest", "DX", "Digi", "User1", "User2", "User3" };

void profile_menu(GtkWidget *parent) {
  GtkWidget *lbl, *btn, *sep;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - RX/TX Profiles");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  lbl = gtk_label_new("RX profiles");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, 1, 2, 1);

  if (can_transmit) {
    lbl = gtk_label_new("     ");
    gtk_grid_attach(GTK_GRID(grid), lbl, 4, 1, 1, 1);
    lbl = gtk_label_new("TX profiles");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), lbl, 5, 1, 2, 1);
  }

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(sep, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), sep, 0, 2, 7, 1);
  sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request(sep, 3, -1);
  gtk_grid_attach(GTK_GRID(grid), sep, 1, 2, 1, NUMPROFILES + 1);

  for (int p = 0; p < NUMPROFILES; p++) {
    lbl = gtk_label_new(names[p]);
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, p + 3, 1, 1);
    btn = gtk_button_new_with_label("Load RX");
    gtk_grid_attach(GTK_GRID(grid), btn, 2, p + 3, 1, 1);
    g_signal_connect(btn, "button-press-event", G_CALLBACK(load_rx_cb), GINT_TO_POINTER(p + MODES));
    btn = gtk_button_new_with_label("Save RX");
    gtk_grid_attach(GTK_GRID(grid), btn, 3, p + 3, 1, 1);
    g_signal_connect(btn, "button-press-event", G_CALLBACK(save_rx_cb), GINT_TO_POINTER(p + MODES));

    if (can_transmit) {
      lbl = gtk_label_new("     ");
      gtk_grid_attach(GTK_GRID(grid), lbl, 4, p + 3, 1, 1);
      btn = gtk_button_new_with_label("Load TX");
      gtk_grid_attach(GTK_GRID(grid), btn, 5, p + 3, 1, 1);
      g_signal_connect(btn, "button-press-event", G_CALLBACK(load_tx_cb), GINT_TO_POINTER(p + MODES));
      btn = gtk_button_new_with_label("Save TX");
      gtk_grid_attach(GTK_GRID(grid), btn, 6, p + 3, 1, 1);
      g_signal_connect(btn, "button-press-event", G_CALLBACK(save_tx_cb), GINT_TO_POINTER(p + MODES));
    }
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
