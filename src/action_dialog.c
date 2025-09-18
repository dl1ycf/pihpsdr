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
static enum ACTION action;

static void action_select_cb(GtkWidget *widget, gpointer data) {
  const CHOICE *choice = (CHOICE *)data;
  g_signal_handler_block(G_OBJECT(previous_button), previous_signal_id);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(previous_button), widget == previous_button);
  g_signal_handler_unblock(G_OBJECT(previous_button), previous_signal_id);
  previous_button = widget;
  previous_signal_id = choice->signal_id;
  action = choice->action;
}

int action_dialog(GtkWidget *parent, int filter, enum ACTION currentAction) {
  GtkRequisition min;
  GtkRequisition nat;
  int width, height;
  CHOICE *previous = NULL;
  CHOICE *choice = NULL;
  action = currentAction;
  previous_button = NULL;
  dialog = gtk_dialog_new_with_buttons("Action", GTK_WINDOW(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       ("_OK"), GTK_RESPONSE_ACCEPT, ("_Cancel"), GTK_RESPONSE_REJECT, NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
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
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);

  int col = 0;
  int row = 0;

  for (int i = 0; i < ACTIONS; i++) {
    if ((ActionTable[i].type & filter) || (ActionTable[i].type == AT_NONE)) {
      GtkWidget *button = gtk_toggle_button_new_with_label(ActionTable[i].str);
      gtk_widget_set_name(button, "small_toggle_button");
      gtk_grid_attach(GTK_GRID(grid), button, col, row, 1, 1);

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

  gtk_container_add(GTK_CONTAINER(sw), grid);
  gtk_container_add(GTK_CONTAINER(content), sw);
  gtk_widget_show_all(content);
  //
  // Determine the size without scrolling. To avoid this looking
  // over-crowded, increase horizontal width bxy 20 percent.
  //
  gtk_widget_get_preferred_size(sw, &min, &nat);
  width  = (nat.width * 120) / 100;
  height = nat.height;

  //
  // Limit the window to 750*430
  //
  if (width  > 750) { width = 750; }

  if (height > 430) { height = 430; }

  //
  // For some reason, the set_size_request below doew not work until
  // setting propagation of natural widths to FALSE
  //
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), FALSE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), FALSE);
  gtk_widget_set_size_request(sw, width, height);
  //
  // Block the GUI  while this dialog is running, if it has completed
  // (Either OK or Cancel button pressed), destroy it.
  //
  int result = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy(dialog);

  if (result != GTK_RESPONSE_ACCEPT) {
    action = currentAction;
  }

  // free up choice structures
  while (previous != NULL) {
    choice = previous;
    previous = choice->previous;
    g_free(choice);
  }

  return action;
}

