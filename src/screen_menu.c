/* Copyright (C)
*  2023 - Christoph van Wüllen, DL1YCF
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
#include <stdio.h>

#include "appearance.h"
#include "css.h"
#include "ext.h"
#include "main.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"

static GtkWidget *dialog = NULL;
static GtkWidget *wide_b = NULL;
static GtkWidget *height_b = NULL;
static GtkWidget *size_b = NULL;
static gulong font_signal_id;
static guint apply_timeout = 0;

//
// local copies of global variables
//
static int my_display_width;
static int my_display_height;
static int my_display_size;
static int my_rx_stack_horizontal;

//
// It has been reported (and I could reproduce)
// that hitting the width or heigth
// button in fast succession leads to internal GTK crashes
// Therefore, we delegate the GTK screen change operations to
// a timeout handler that is at most called every 500 msec
//
static int apply(gpointer data) {
  apply_timeout = 0;
  //
  display_width[1]             = my_display_width;
  display_height[1]            = my_display_height;
  display_size                 = my_display_size;
  rx_stack_horizontal          = my_rx_stack_horizontal;
  //
  radio_reconfigure_screen();
  if (radio_is_remote) {
    send_screen(cl_sock_tcp, rx_stack_horizontal, display_width[my_display_size]);
  }
  return G_SOURCE_REMOVE;
}

static void schedule_apply(void) {
  if (apply_timeout > 0) {
    g_source_remove(apply_timeout);
  }
  apply_timeout = g_timeout_add(500, apply, NULL);
}

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

static void font_cb(GtkWidget *widget, gpointer data) {
  int choice = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  load_font(choice);
  g_signal_handler_block(G_OBJECT(widget), font_signal_id);
  gtk_combo_box_set_active(GTK_COMBO_BOX(widget), which_css_font);
  g_signal_handler_unblock(G_OBJECT(widget), font_signal_id);
  g_idle_add(ext_vfo_update, NULL);
}

static void size_cb(GtkWidget *widget, gpointer data) {
  my_display_size = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  gtk_widget_set_sensitive(wide_b, my_display_size == 1);
  gtk_widget_set_sensitive(height_b, my_display_size == 1);
  schedule_apply();
}

static void width_cb(GtkWidget *widget, gpointer data) {
  my_display_width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

static void height_cb(GtkWidget *widget, gpointer data) {
  my_display_height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

static void horizontal_cb(GtkWidget *widget, gpointer data) {
  my_rx_stack_horizontal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_apply();
}

static void slider_rows_cb(GtkWidget *widget, gpointer data) {
  slider_rows = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

static void toolbar_rows_cb(GtkWidget *widget, gpointer data) {
  toolbar_rows = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

void screen_menu(GtkWidget *parent) {
  GtkWidget *label;
  GtkWidget *button;
  my_display_width       = display_width[1];
  my_display_height      = display_height[1];
  my_display_size        = display_size;
  my_rx_stack_horizontal = rx_stack_horizontal;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Screen Layout");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  int row = 0;
  int col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  row++;
  col = 0;
  label = gtk_label_new("Font used");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  button = gtk_combo_box_text_new();
  for (int i = 0; i < num_css_fonts; i++) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(button), NULL, cssfont[i]);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(button), which_css_font);
  my_combo_attach(GTK_GRID(grid), button, col, row, 2, 1);
  font_signal_id = g_signal_connect(button, "changed", G_CALLBACK(font_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Window size");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  size_b = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(size_b), NULL, "Full Screen");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(size_b), NULL, "Custom");
  for (int i = 2; i < 6; i++) {
    char txt[64];
    snprintf(txt, sizeof(txt), "%d * %d", display_width[i], display_height[i]);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(size_b), NULL, txt);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(size_b), my_display_size);
  my_combo_attach(GTK_GRID(grid), size_b, col, row, 2, 1);
  g_signal_connect(size_b, "changed", G_CALLBACK(size_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Custom Width/Height");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  wide_b = gtk_spin_button_new_with_range(640.0, (double) display_width[0], 32.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(wide_b), (double) my_display_width);
  gtk_grid_attach(GTK_GRID(grid), wide_b, col, row, 1, 1);
  g_signal_connect(wide_b, "value-changed", G_CALLBACK(width_cb), NULL);
  col++;
  height_b = gtk_spin_button_new_with_range(400.0, (double) display_height[0], 16.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_b), (double) my_display_height);
  gtk_grid_attach(GTK_GRID(grid), height_b, col, row, 1, 1);
  g_signal_connect(height_b, "value-changed", G_CALLBACK(height_cb), NULL);
  row++;
  label = gtk_label_new("Slider Rows");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  button = gtk_spin_button_new_with_range(0.0, 3.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(button), slider_rows);
  gtk_grid_attach(GTK_GRID(grid), button, 1, row, 1, 1);
  g_signal_connect(button, "value-changed", G_CALLBACK(slider_rows_cb), NULL);
  row++;
  label = gtk_label_new("Toolbar Rows");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  button = gtk_spin_button_new_with_range(0.0, 3.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(button), toolbar_rows);
  gtk_grid_attach(GTK_GRID(grid), button, 1, row, 1, 1);
  g_signal_connect(button, "value-changed", G_CALLBACK(toolbar_rows_cb), NULL);
  row++;
  col = 1;
  button = gtk_check_button_new_with_label("Stack RX horizontally");
  gtk_widget_set_name(button, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), my_rx_stack_horizontal);
  gtk_grid_attach(GTK_GRID(grid), button, col, row, 2, 1);
  g_signal_connect(button, "toggled", G_CALLBACK(horizontal_cb), NULL);
  gtk_widget_set_sensitive(wide_b, my_display_size == 1);
  gtk_widget_set_sensitive(height_b, my_display_size == 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
