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

#include "ext.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "sliders.h"
#include "transmitter.h"
#include "vfo.h"

static GtkWidget *ledbtn = NULL;
static GtkWidget *dialog = NULL;
static GtkWidget *mic_level_bar;
static guint level_timer_id = 0;

int vox_menu_trigger(gpointer data) {
  int state = GPOINTER_TO_INT(data);
  if (state) {
    gtk_widget_set_name(ledbtn, "redbutton");
  } else {
    gtk_widget_set_name(ledbtn, "greenbutton");
  }
  return G_SOURCE_REMOVE;
}

static int level_update(gpointer arg) {
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(mic_level_bar), vox_get_peak());
  return G_SOURCE_CONTINUE;
}

static void cleanup(void) {
  if (level_timer_id != 0) {
    g_source_remove(level_timer_id);
    level_timer_id = 0;
  }
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
  }
  radio_save_state();
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static gboolean enable_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  vox_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  tx_set_vox(transmitter);
  return TRUE;
}

static void vox_value_changed_cb(GtkWidget *widget, gpointer data) {
  vox_threshold = 0.001 * gtk_range_get_value(GTK_RANGE(widget));
  tx_set_vox(transmitter);
}

static void vox_hang_value_changed_cb(GtkWidget *widget, gpointer data) {
  vox_hang = gtk_range_get_value(GTK_RANGE(widget));
  tx_set_vox(transmitter);
}

void vox_menu(GtkWidget *parent) {
  if (transmitter == NULL) { return; }
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - VOX");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  ledbtn = gtk_button_new();
  gtk_widget_set_name(ledbtn, "greenbutton");
  gtk_grid_attach(GTK_GRID(grid), ledbtn, 2, 0, 1, 1);
  GtkWidget *enable_b = gtk_check_button_new_with_label("VOX Enable");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enable_b), vox_enabled);
  g_signal_connect (enable_b, "toggled", G_CALLBACK(enable_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), enable_b, 3, 0, 1, 1);
  GtkWidget *level_label = gtk_label_new("Mic Level");
  gtk_widget_set_name(level_label, "boldlabel");
  gtk_widget_set_halign(level_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), level_label, 0, 1, 1, 1);
  mic_level_bar = gtk_progress_bar_new();
  gtk_grid_attach(GTK_GRID(grid), mic_level_bar, 1, 1, 3, 1);
  gtk_widget_set_valign(mic_level_bar, GTK_ALIGN_CENTER);
  GtkWidget *threshold_label = gtk_label_new("VOX Threshold");
  gtk_widget_set_name(threshold_label, "boldlabel");
  gtk_widget_set_halign(threshold_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), threshold_label, 0, 2, 1, 1);
  GtkWidget *vox_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1000.0, 1.0);
  gtk_widget_set_valign(vox_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(vox_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(vox_scale), 1000.0*vox_threshold);
  gtk_grid_attach(GTK_GRID(grid), vox_scale, 1, 2, 3, 1);
  g_signal_connect(G_OBJECT(vox_scale), "value_changed", G_CALLBACK(vox_value_changed_cb), NULL);
  GtkWidget *hang_label = gtk_label_new("VOX Hang (ms)");
  gtk_widget_set_name(hang_label, "boldlabel");
  gtk_widget_set_halign(hang_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), hang_label, 0, 4, 1, 1);
  GtkWidget *vox_hang_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1000.0, 1.0);
  gtk_widget_set_valign(vox_hang_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(vox_hang_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(vox_hang_scale), vox_hang);
  gtk_grid_attach(GTK_GRID(grid), vox_hang_scale, 1, 4, 3, 1);
  g_signal_connect(G_OBJECT(vox_hang_scale), "value_changed", G_CALLBACK(vox_hang_value_changed_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  level_timer_id = g_timeout_add(100, level_update, NULL);
}
