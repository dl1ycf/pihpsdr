/* Copyright (C)
* 2024 - Christoph van Wüllen, DL1YCF
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

//
// A menu only for debugging purposes. Once opened it is kept  open.
// Any piHPSDR push-button or encoder command can be triggered.
//

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "actions.h"
#include "message.h"

int open_test_menu = 0;

//
// Test menu to give all sorts of commands
//
static GtkWidget *dialog = NULL;
static GtkWidget *test_name = NULL;
static GtkWidget *test_press = NULL;
static GtkWidget *test_ccw = NULL;
static GtkWidget *test_cw = NULL;
static int test_action = NO_ACTION;
static guint repeat_timer = 0;
static int repeat_state = 0;

static gboolean delete_cb() {
  //
  // The test menu can be destroyd by standard GTK methods
  // (e.g. closing the menu window).
  // It can never be re-opened.
  //
  gtk_widget_destroy(dialog);
  return TRUE;
}

static gboolean repeat_cb(gpointer data) {
  int retval = TRUE;

  switch (repeat_state) {
  case 0:
  default:
    repeat_timer = 0;
    retval = G_SOURCE_REMOVE;
    break;

  case 1:
    schedule_action(test_action, PRESSED, 1);
    break;

  case 2:
    schedule_action(test_action, RELATIVE, -1);
    break;

  case 3:
    schedule_action(test_action, RELATIVE, 1);
    break;
  }

  return retval;
}

static void test_action_changed_cb(GtkWidget *widget, gpointer data) {
  test_action = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  gtk_label_set_label(GTK_LABEL(test_name), ActionTable[test_action].str);
  gtk_widget_set_sensitive(test_press, FALSE);
  gtk_widget_set_sensitive(test_ccw, FALSE);
  gtk_widget_set_sensitive(test_cw, FALSE);

  if (ActionTable[test_action].type & AT_BTN) {
    gtk_widget_set_sensitive(test_press, TRUE);
  } else {
    gtk_widget_set_sensitive(test_ccw, TRUE);
    gtk_widget_set_sensitive(test_cw, TRUE);
  }

  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
  }

  repeat_timer = 0;
  repeat_state = 0;
}

// cppcheck-suppress constParameterCallback
static void test_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (event->type != GDK_BUTTON_PRESS) { return; }

  repeat_state = 1;
  schedule_action(test_action, PRESSED, 1);

  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
  }

  repeat_timer = g_timeout_add(500, repeat_cb, NULL);
}

// cppcheck-suppress constParameterCallback
static void test_release_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  repeat_state = 0;

  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
  }

  repeat_timer = 0;
  schedule_action(test_action, RELEASED, 1);
}

// cppcheck-suppress constParameterCallback
static void test_ccw_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (event->type != GDK_BUTTON_PRESS) { return; }

  repeat_state = 2;
  schedule_action(test_action, RELATIVE, -1);

  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
  }

  repeat_timer = g_timeout_add(250, repeat_cb, NULL);
}

// cppcheck-suppress constParameterCallback
static void test_cw_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (event->type != GDK_BUTTON_PRESS) { return; }

  repeat_state = 3;
  schedule_action(test_action, RELATIVE, 1);

  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
  }

  repeat_timer = g_timeout_add(250, repeat_cb, NULL);
}

// cppcheck-suppress constParameterCallback
static void cancel_repeat_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
  }

  repeat_timer = 0;
  repeat_state = 0;
}

void test_menu(GtkWidget *parent) {
  GtkWidget *lbl;
  GtkWidget *btn;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Test Commands");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (delete_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  lbl = gtk_label_new("Choose Action: ");
  gtk_widget_set_name(lbl, "medium_button");
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, 0, 1, 2);
  gtk_widget_set_size_request(lbl, 150, 50);
  btn = gtk_spin_button_new_with_range(0, ACTIONS, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), test_action);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, 0, 1, 2);
  g_signal_connect(btn, "value_changed", G_CALLBACK(test_action_changed_cb), NULL);
  test_name = gtk_label_new(ActionTable[test_action].str);
  gtk_widget_set_name(test_name, "medium_button");
  gtk_widget_set_valign(test_name, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), test_name, 2, 0, 1, 2);
  test_press = gtk_button_new_with_label("PushButton\nPress/Release");
  gtk_widget_set_name(test_press, "medium_button");
  gtk_widget_set_size_request(test_press, 150, 50);
  gtk_widget_set_halign(test_press, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), test_press, 0, 2, 1, 2);
  g_signal_connect(G_OBJECT(test_press), "button-press-event", G_CALLBACK(test_press_cb), NULL);
  g_signal_connect(G_OBJECT(test_press), "button-release-event", G_CALLBACK(test_release_cb), NULL);
  test_ccw = gtk_button_new_with_label("Encoder\nCounterClockWise");
  gtk_widget_set_name(test_ccw, "medium_button");
  gtk_widget_set_halign(test_ccw, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), test_ccw, 1, 2, 1, 2);
  g_signal_connect(G_OBJECT(test_ccw), "button-press-event", G_CALLBACK(test_ccw_cb), NULL);
  g_signal_connect(G_OBJECT(test_ccw), "button-release-event", G_CALLBACK(cancel_repeat_cb), NULL);
  test_cw = gtk_button_new_with_label("Encoder\nClockWise");
  gtk_widget_set_name(test_cw, "medium_button");
  gtk_widget_set_halign(test_cw, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), test_cw, 2, 2, 1, 2);
  g_signal_connect(G_OBJECT(test_cw), "button-press-event", G_CALLBACK(test_cw_cb), NULL);
  g_signal_connect(G_OBJECT(test_cw), "button-release-event", G_CALLBACK(cancel_repeat_cb), NULL);
  gtk_widget_set_sensitive(test_press, FALSE);
  gtk_widget_set_sensitive(test_ccw, FALSE);
  gtk_widget_set_sensitive(test_cw, FALSE);
  gtk_container_add(GTK_CONTAINER(content), grid);
  gtk_widget_show_all(dialog);
}
