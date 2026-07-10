/* Copyright (C)
*  2026 - piHPSDR Modernisation, contribution from AL
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

#include "appearance.h"
#include "ext.h"
#include "main.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "theme.h"

static GtkWidget *dialog       = NULL;

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog       = NULL;
    gtk_widget_destroy(tmp);
    sub_menu    = NULL;
    active_menu = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void dark_cb(GtkWidget *widget, gpointer data) {
  gtk_dark_theme = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  theme_set();
}

static void theme_combo_cb(GtkWidget *widget, gpointer data) {
  active_theme_index = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  theme_set();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point — called from new_menu.c (see INTEGRATION_GUIDE.md)
// ─────────────────────────────────────────────────────────────────────────────

void theme_menu(GtkWidget *parent) {
  GtkWidget *w, *label;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Colour Theme");
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy",      G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid    = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
  int row = 0;
  // ── Close button ──────────────────────────────────────────────────────────
  w = gtk_button_new_with_label("Close");
  gtk_widget_set_name(w, "close_button");
  g_signal_connect(w, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);
  row++;
  // ── Separator ─────────────────────────────────────────────────────────────
  gtk_grid_attach(GTK_GRID(grid),
                  gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                  0, row, 3, 1);
  GtkSettings *settings = gtk_settings_get_default();
  gchar *theme_name = NULL;
  if (settings) {
    g_object_get(settings, "gtk-theme-name", &theme_name, NULL);
  }
  if (theme_name) {
    row++;
    label = gtk_label_new("GTK Theme Name");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    label = gtk_label_new(theme_name);
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
    g_free(theme_name);
  }
  row++;
  label = gtk_label_new("GTK Dark Theme");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  w = gtk_check_button_new();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), gtk_dark_theme);
  gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(dark_cb), NULL);
  // ── Theme selector row ────────────────────────────────────────────────────
  row++;
  // ── Theme selector row ────────────────────────────────────────────────────
  label = gtk_label_new("piHPSDR Colour Theme");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  GtkWidget *combo = gtk_combo_box_text_new();
  for (int i = 0; i < num_themes; i++) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, themes[i].name);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active_theme_index);
  gtk_widget_set_hexpand(combo, TRUE);
  my_combo_attach(GTK_GRID(grid), combo, 1, row, 2, 1);
  g_signal_connect(combo, "changed", G_CALLBACK(theme_combo_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
