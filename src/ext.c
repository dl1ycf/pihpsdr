/* Copyright (C)
*  2017 - John Melton, G0ORX/N6LYT
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

#include "main.h"
#include "new_menu.h"
#include "radio.h"
#include "vfo.h"

//
// The following calls functions can be called usig g_idle_add
// Their return value is G_SOURCE_REMOVE so they will be called only
// once.
//

// cppcheck-suppress constParameterPointer
int ext_start_radio(gpointer data) {
  radio_start_radio();
  return G_SOURCE_REMOVE;
}

//
// ALL calls to vfo_update should go through g_idle_add(ext_vfo_update)
// The first call starts a periodic GTK task that updates the VFO bar.
// (so it will be redrawn even if one forgets the vfo_update()
//
static guint vfo_timeout = 0;

// cppcheck-suppress constParameterCallback
static int vfo_timeout_cb(gpointer data) {
  //if (vfo_timeout > 0) {
  //  g_source_remove(vfo_timeout);
  //  vfo_timeout = 0;
  // }
  vfo_update();
  return G_SOURCE_CONTINUE;
}

int ext_vfo_update(gpointer data) {
  //
  // If no timeout is pending, then a vfo_update() is to
  // be scheduled soon.
  //
  if (vfo_timeout == 0) {
    vfo_timeout = g_timeout_add(100, vfo_timeout_cb, NULL);
  }
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_radio_set_tune(gpointer data) {
  radio_set_tune(GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_radio_toggle_mox(gpointer data) {
  radio_toggle_mox();
  return G_SOURCE_REMOVE;
}

int ext_radio_set_mox(gpointer data) {
  radio_set_mox(GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_radio_set_vox(gpointer data) {
  radio_set_vox(GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_band_menu(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  start_band_menu(v);
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_mode_menu(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  start_mode_menu(v);
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_filter_menu(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  start_filter_menu(v);
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_rx_menu(gpointer data) {
  start_rx_menu();
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_tx_menu(gpointer data) {
  start_tx_menu();
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_radio_set_duplex(gpointer data) {
  int state = GPOINTER_TO_INT(data);
  radio_set_duplex(state);
  return G_SOURCE_REMOVE;
}

int ext_set_title(gpointer data) {
  gtk_window_set_title(GTK_WINDOW(top_window), (char *)data);
  return G_SOURCE_REMOVE;
}

int radio_reconfigure_screen_done = 0;

// cppcheck-suppress constParameterPointer
int ext_radio_reconfigure_screen(gpointer data) {
  radio_reconfigure_screen();
  radio_reconfigure_screen_done = 1;
  return G_SOURCE_REMOVE;
}
