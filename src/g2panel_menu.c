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

#include "action_dialog.h"
#include "actions.h"
#include "g2panel.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"


int g2panel_menu_is_open = 0;
static int *last_buttonvec = NULL;
static int *last_encodervec = NULL;
static int last_andromeda_type = 0;  // 4 or 5
static int last_type = 0;            // AT_BTN or AT_ENC
static int action_chosen = NO_ACTION;
static int last_pos = 0;

static GtkWidget *dialog = NULL;
static GtkWidget *last_label = NULL;
static GtkWidget *newAction = NULL;
static GtkWidget *restore = NULL;

static void cleanup() {
  g2panel_menu_is_open = 0;

  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }

  last_label = NULL;
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static gboolean action_cb(GtkWidget *widget, gpointer data) {
  switch (last_type) {
  case AT_BTN:
    action_chosen = action_dialog(dialog, AT_BTN, last_buttonvec[last_pos]);
    last_buttonvec[last_pos] = action_chosen;
    break;

  case AT_ENC:
    action_chosen = action_dialog(dialog, AT_ENC, last_encodervec[last_pos]);
    last_encodervec[last_pos] = action_chosen;
    break;
  }

  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[action_chosen].str);
  return TRUE;
}

static gboolean restore_cb(GtkWidget *widget, gpointer data) {
  int *vec;
  vec = g2panel_default_buttons(last_andromeda_type);

  if (vec != NULL) {
    for (int i = 0; i <= 99; i++) { last_buttonvec[i] = vec[i]; }

    g_free(vec);
  }

  vec = g2panel_default_encoders(last_andromeda_type);

  if (vec != NULL) {
    for (int i = 0; i <= 49; i++) { last_encodervec[i] = vec[i]; }

    g_free(vec);
  }

  //
  // Update command button to reflect the new setting
  //
  if (last_type == AT_BTN) {
    gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[last_buttonvec[last_pos]].str);
  } else {
    gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[last_encodervec[last_pos]].str);
  }

  return TRUE;
}

void g2panel_menu(GtkWidget *parent) {
  GtkWidget *w;
  GtkWidget *grid;
  dialog = gtk_dialog_new();
  int row = 0;
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - G2 panel Switch/Encoder Assignments");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 1, 1);
  row++;
  w = gtk_label_new("Last panel element touched:");
  gtk_widget_set_name(w, "medium_button");
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 5, 1);
  last_label = gtk_label_new("  ");
  gtk_widget_set_name(last_label, "medium_button");
  gtk_widget_set_halign(last_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), last_label, 5, row, 3, 1);
  row++;
  w = gtk_label_new("piHPSDR command assigned:");
  gtk_widget_set_name(w, "medium_button");
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 5, 2);
  newAction = gtk_button_new_with_label("    ");
  gtk_widget_set_name(newAction, "medium_button");
  g_signal_connect(newAction, "button-press-event", G_CALLBACK(action_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), newAction, 5, row, 3, 2);
  row++;
  row++;
  restore = gtk_button_new_with_label("Restore factory settings for encoders and buttons");
  gtk_widget_set_name(restore, "medium_button");
  g_signal_connect(restore, "button-press-event", G_CALLBACK(restore_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), restore, 0, row, 8, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  g2panel_menu_is_open = 1;
  gtk_widget_show_all(dialog);
  gtk_widget_hide(newAction);
  gtk_widget_hide(restore);
}

void g2panel_change_command(int andromeda_type, int type, int *buttonvec, int *encodervec, int pos) {
  char str[128];
  last_buttonvec = buttonvec;
  last_encodervec = encodervec;
  last_andromeda_type = andromeda_type;
  last_pos = pos;
  last_type = type; // AT_BTN or AT_ENC

  if (last_pos < 0) { last_pos = 0; }

  switch (type) {
  case AT_BTN:
    snprintf(str, sizeof(str), "Button #%d", last_pos);

    if (last_pos > 99) { last_pos = 0; }

    gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[buttonvec[last_pos]].str);
    break;

  case AT_ENC:
    snprintf(str, sizeof(str), "Encoder #%d", last_pos);

    if (last_pos > 49) { last_pos = 0; }

    gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[encodervec[last_pos]].str);
    break;

  default:
    snprintf(str, sizeof(str), "UNKNOWN #%d", last_pos);
    break;
  }

  gtk_label_set_text(GTK_LABEL(last_label), str);
  gtk_widget_show(newAction);
  gtk_widget_show(restore);
}
