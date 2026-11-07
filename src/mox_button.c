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

//
// A menu only for debugging purposes. Once opened it is kept  open.
// Any piHPSDR push-button or encoder command can be triggered.
//

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "actions.h"
#include "ext.h"
#include "main.h"
#include "message.h"
#include "radio.h"

int open_mox_button = 0;

//
// Test menu to give all sorts of commands
//
static GtkWidget *dialog = NULL;

static gboolean delete_cb(void) {
  //
  // The test menu can be destroyd by standard GTK methods
  // (e.g. closing the menu window).
  // It can never be re-opened.
  //
  if (dialog != NULL) {
    gtk_widget_destroy(dialog);
    dialog = NULL;
    open_mox_button = 0;
  }
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean mox_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (event->type != GDK_BUTTON_PRESS) { return TRUE; }
  if (mox) {
    g_timeout_add(ptt_delay, ext_radio_set_mox, GINT_TO_POINTER(0));
  } else {
    radio_set_mox(1);
  }
  return FALSE;
}

void mox_button(int state) {
  GtkWidget *btn;
  if (state == 0) {
    if (dialog != NULL) {
      (void) delete_cb();
    }
    return;
  }
  if (state == 1 && dialog != NULL) {
    return;
  }
  dialog = gtk_dialog_new();
  open_mox_button = 1;
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(top_window));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (delete_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  btn = gtk_button_new_with_label("MOX");
  gtk_widget_set_size_request(btn, 125, 125);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
  g_signal_connect(btn, "button-press-event", G_CALLBACK(mox_press_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  gtk_widget_show_all(dialog);
  gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
}
