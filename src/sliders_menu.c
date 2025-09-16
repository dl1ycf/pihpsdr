/* Copyright (C)
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

#include "actions.h"
#include "new_menu.h"
#include "radio.h"
#include "sliders.h"

//
// List of functions that can be associated with sliders
//
#define NUM_FUNCS 15
static enum ACTION func_list[NUM_FUNCS] = {
  NO_ACTION, AF_GAIN,     AGC_GAIN, ATTENUATION, COMPRESSION,
  CW_SPEED,  LINEIN_GAIN, MIC_GAIN, PAN,         PANADAPTER_LOW,
  RF_GAIN,   SQUELCH,     DRIVE,    VOXLEVEL,    ZOOM
};

static GtkWidget *dialog = NULL;

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

static void combo_cb(GtkWidget *widget, gpointer data) {
  int pos = GPOINTER_TO_INT(data);
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  slider_functions[pos] = func_list[val];
  radio_reconfigure_screen();
}

void sliders_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Sliders configuration");
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

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      int pos = 3 * i + j;
      GtkWidget *w = gtk_combo_box_text_new();
      int val = 0;

      for (int k = 0; k < NUM_FUNCS; k++) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, ActionTable[func_list[k]].str);

        if (slider_functions[pos] == func_list[k]) { val = k; }
      }

      gtk_combo_box_set_active(GTK_COMBO_BOX(w), val);
      my_combo_attach(GTK_GRID(grid), w, j, i + 1, 1, 1);
      g_signal_connect(w, "changed", G_CALLBACK(combo_cb),
                       GINT_TO_POINTER(3 * i + j));
    }
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
