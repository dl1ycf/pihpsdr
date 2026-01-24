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

#include "new_menu.h"
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
static void exit_cb (GtkWidget *widget, gpointer data) {
  radio_exit_program();
}

// cppcheck-suppress constParameterCallback
static void reboot_cb (GtkWidget *widget, gpointer data) {
  radio_reboot();
}

// cppcheck-suppress constParameterCallback
static void shutdown_cb (GtkWidget *widget, gpointer data) {
  radio_shutdown();
}

void exit_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Exit");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  int row = 0;
  int col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Cancel");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  row++;
  col = 0;
  GtkWidget *exit_b = gtk_button_new_with_label("Exit");
  g_signal_connect (exit_b, "button-press-event", G_CALLBACK(exit_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), exit_b, col, row, 1, 1);
  col++;
  GtkWidget *reboot_b = gtk_button_new_with_label("Reboot");
  g_signal_connect (reboot_b, "button-press-event", G_CALLBACK(reboot_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), reboot_b, col, row, 1, 1);
  col++;
  GtkWidget *shutdown_b = gtk_button_new_with_label("Shutdown");
  g_signal_connect (shutdown_b, "button-press-event", G_CALLBACK(shutdown_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), shutdown_b, col, row, 1, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
