/* Copyright (C)
* 2021 - John Melton, G0ORX/N6LYT
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
#include "main.h"
#include "message.h"

#define GRID_WIDTH 6

typedef struct _choice {
  int action;
  GtkWidget *button;
  gulong signal_id;
  struct _choice *previous;
} CHOICE;

static GtkWidget *dialog;
static GtkWidget *previous_button;
static gulong previous_signal_id;
static enum ACTION new_action;   // action currently chosen
static enum ACTION ret_action;   // action to be returned

static void destroy() {
  if (dialog) {
    gtk_widget_destroy(dialog);
  }
}

static gboolean cancel_cb() {
  destroy();
  return TRUE;
}

static gboolean choose_cb() {
  //
  // Here we set ret_action
  //
  ret_action = new_action;
  destroy();
  return TRUE;
}

static void action_select_cb(GtkWidget *widget, gpointer data) {
  const CHOICE *choice = (CHOICE *)data;
  g_signal_handler_block(G_OBJECT(previous_button), previous_signal_id);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(previous_button), widget == previous_button);
  g_signal_handler_unblock(G_OBJECT(previous_button), previous_signal_id);
  previous_button = widget;
  previous_signal_id = choice->signal_id;
  new_action = choice->action;
}

int action_dialog(GtkWidget *parent, int filter, enum ACTION currentAction) {
  GtkRequisition min;
  GtkRequisition nat;
  int width, height;
  GtkWidget *w;
  CHOICE *previous = NULL;
  CHOICE *choice = NULL;
  new_action = currentAction;
  ret_action = currentAction;
  previous_button = NULL;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "Choose Function");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (cancel_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (cancel_cb), NULL);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  w = gtk_button_new_with_label("Choose");
  gtk_widget_set_name(w, "close_button");
  g_signal_connect (w, "button-press-event", G_CALLBACK(choose_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 0, 0, 1, 1);
  w = gtk_button_new_with_label("Cancel");
  gtk_widget_set_name(w, "close_button");
  g_signal_connect (w, "button-press-event", G_CALLBACK(cancel_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 5, 0, 1, 1);
  //
  // The rest goes into a scrollable subgrid
  //
  GtkWidget *scrgrd = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(scrgrd), 2);
  gtk_grid_set_row_spacing (GTK_GRID(scrgrd), 2);
  gtk_grid_set_column_homogeneous(GTK_GRID(scrgrd), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(scrgrd), TRUE);
  GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  //Set scrollbar to ALWAYS be displayed and not as temporary overlay
  g_object_set(sw , "overlay-scrolling", FALSE , NULL);
  //
  // For some reason, the get_preferred_size below does not work until
  // setting propagation of natural widths to FALSE
  //
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);

  int col = 0;
  int row = 0;

  for (int i = 0; i < ACTIONS; i++) {
    if ((ActionTable[i].type & filter) || (ActionTable[i].type == AT_NONE)) {
      GtkWidget *button = gtk_toggle_button_new_with_label(ActionTable[i].str);
      gtk_widget_set_name(button, "small_toggle_button");
      gtk_grid_attach(GTK_GRID(scrgrd), button, col, row, 1, 1);

      if (ActionTable[i].action == currentAction) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
      }

      choice = g_new0(CHOICE, 1);
      choice->action = i;
      choice->button = button;
      choice->signal_id = g_signal_connect(button, "toggled", G_CALLBACK(action_select_cb), choice);
      choice->previous = previous;
      previous = choice;

      if (ActionTable[i].action == currentAction) {
        previous_button = button;
        previous_signal_id = choice->signal_id;
      }

      col++;

      if (col == GRID_WIDTH) {
        col = 0;
        row++;
      }
    }
  }

  gtk_container_add(GTK_CONTAINER(sw), scrgrd);
  gtk_widget_show_all(sw);
  //
  // Determine the size without scrolling.
  //
  gtk_widget_get_preferred_size(sw, &min, &nat);
  width  = nat.width;
  height = nat.height;
  //
  // Limit the window to display size
  //
  if (width > display_width[0] - 50) {
    width  = display_width[0] - 50;
  }

  //
  // This dialog can become very tall, so restrict its height
  // even if display size permits
  //
  if (height > 500) {
    height = 500;
  }

  if (height > display_height[0] - 100) {
    height = display_height[0] - 100;
  }

  //
  // For some reason, the set_size_request below doew not work until
  // setting propagation of natural widths to FALSE
  //
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), FALSE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), FALSE);
  gtk_widget_set_size_request(sw, width, height);

  gtk_grid_attach(GTK_GRID(grid), sw, 0, 1, 6, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);

  //
  // Block the GUI  while this dialog is running, if it has completed
  // (Either OK or Cancel button pressed), destroy it.
  //
  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));

  // free up choice structures
  while (previous != NULL) {
    choice = previous;
    previous = choice->previous;
    g_free(choice);
  }

  return ret_action;
}

