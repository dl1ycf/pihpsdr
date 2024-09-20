/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "new_menu.h"
#include "equalizer_menu.h"
#include "radio.h"
#include "ext.h"
#include "vfo.h"
#include "transmitter.h"
#include "message.h"

//
// NOTE: resizing the window to the actual width of the menu
//       (ten-band vs. four-band) caused warnings and long
//       "hangs" which could not be resolved.
//       Therefore, the window now always assumes it max size
//       (which may be too large for the current screen) and
//       is aligned with the top window.
//
static GtkWidget *dialog = NULL;

static GtkWidget *scale[11];
static GtkWidget *freqspin[11];
static GtkWidget *enable_b;
static GtkWidget *tenband_b;

static int eqid;          // 0: RX1, 1: RX2, 2: TX
static int have_tenband;  // 0: four-band on display, 1: ten-band on display

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

void update_eq() {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    //
    // insert here any function to inform the client of equalizer setting
    // changes, if this becomes part of the protocol
    //
  } else {
#endif

    if (can_transmit) {
      tx_set_equalizer(transmitter);
    }

    for (int id = 0; id < receivers; id++) {
      rx_set_equalizer(receiver[id]);
    }

#ifdef CLIENT_SERVER
  }

#endif
}

static void tenband_cb (GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (val) {
    for (int i = 5; i < 11; i++) {
      gtk_widget_show(freqspin[i]);
      gtk_widget_show(scale   [i]);
    }
  } else {
    for (int i = 5; i < 11; i++) {
      gtk_widget_hide(freqspin[i]);
      gtk_widget_hide(scale   [i]);
    }
  }

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_tenband = val;
    }

    if (eqid == 0) {
      int m = vfo[eqid].mode;
      mode_settings[m].rx_eq_tenband = val;
    }

    break;

  case 2:
    if (can_transmit) {
      int m = vfo[vfo_get_tx_vfo()].mode;
      transmitter->eq_tenband = val;
      mode_settings[m].tx_eq_tenband = val;
    }

    break;
  }

  have_tenband = val;
  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}

static void enable_cb (GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_enable = val;
    }

    if (eqid == 0) {
      int m = vfo[eqid].mode;
      mode_settings[m].en_rxeq = val;
    }

    break;

  case 2:
    if (can_transmit) {
      int m = vfo[vfo_get_tx_vfo()].mode;
      transmitter->eq_enable = val;
      mode_settings[m].en_txeq = val;
    }

    break;
  }

  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}

static void freq_changed_cb (GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  //
  // The frequency of the current button can be moved in wide limits,
  // but the other frequencies possibly need update in order to avoid
  // crossings.
  //
  double valmin = 10.0*(i+4);
  double valmax = 15900.0 + 10.0*i;

  if (val > valmax) { val = valmax; }
  if (val < valmin) { val = valmin; }
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), val);

  for (int j = i - 1; j > 0; j--) {
    double x1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[j+1]));
    double x2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[j]));
    if (x2 >= x1) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[j]), x1 - 10.0);
    }
  }

  for (int j = i + 1; j < 11; j++) {
    double x1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[j-1]));
    double x2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[j]));
    if (x1 >= x2) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[j]), x1 + 10.0);
    }
  }
      
    
  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      int m = vfo[eqid].mode;
      for (int j = 1; j < 11; j++) {
        receiver[eqid]->eq_freq[j] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[j]));
        if (eqid == 0) {
          mode_settings[m].rx_eq_freq[j] = receiver[eqid]->eq_freq[j];
        }
      }
    }

    break;

  case 2:
    if (can_transmit) {
      int m = vfo[eqid].mode;
      for (int j = 1; j < 11; j++) {
        transmitter->eq_freq[j] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[j]));
        mode_settings[m].tx_eq_freq[j] = transmitter->eq_freq[j];
      }
    }

    break;
  }

  update_eq();
}


static void gain_changed_cb (GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  double val = gtk_range_get_value(GTK_RANGE(widget));

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_gain[i] = val;
    }

    if (eqid == 0) {
      int m = vfo[eqid].mode;
      mode_settings[m].rx_eq_gain[i] = val;
    }

    break;

  case 2:
    if (can_transmit) {
      int m = vfo[vfo_get_tx_vfo()].mode;
      transmitter->eq_gain[i] = val;
      mode_settings[m].tx_eq_gain[i] = val;
    }

    break;
  }

  update_eq();
}

//
// If RX2 is selected while only one RX is running, or if TX
// is selected but there is no transmitter, silently force
// the combo box back to RX1
// At the very end, update the "Enable" button
//
static void eqid_changed_cb(GtkWidget *widget, gpointer data) {
  eqid = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      for (int i = 0; i < 11; i++) {
        gtk_range_set_value(GTK_RANGE(scale[i]), receiver[eqid]->eq_gain[i]);
      }

      for (int i = 1; i < 11; i++) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), receiver[eqid]->eq_freq[i]);
      }
    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
      eqid = 0;
    }

    break;

  case 2:
    if (can_transmit) {
      for (int i = 0; i < 11; i++) {
        gtk_range_set_value(GTK_RANGE(scale[i]), transmitter->eq_gain[i]);
      }

      for (int i = 1; i < 11; i++) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), transmitter->eq_freq[i]);
      }
    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
      eqid = 0;
    }
  }

  switch (eqid) {
  case 0:
    have_tenband = receiver[0]->eq_tenband;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), receiver[0]->eq_enable);
    break;

  case 1:
    have_tenband = receiver[1]->eq_tenband;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), receiver[1]->eq_enable);
    break;

  case 2:
    have_tenband = transmitter->eq_tenband;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), transmitter->eq_enable);
    break;
  }

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tenband_b), have_tenband);

  if (have_tenband) {
    for (int i = 5; i < 11; i++) {
      gtk_widget_show(freqspin[i]);
      gtk_widget_show(scale   [i]);
    }
  } else {
    for (int i = 5; i < 11; i++) {
      gtk_widget_hide(freqspin[i]);
      gtk_widget_hide(scale   [i]);
    }
  }
}

void equalizer_menu(GtkWidget *parent) {
  eqid = 0;
  have_tenband = receiver[0]->eq_tenband;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Equalizer");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *eqid_combo_box = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "RX1 Eq Settings");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "RX2 Eq Settings");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "TX  Eq Settings");
  gtk_combo_box_set_active(GTK_COMBO_BOX(eqid_combo_box), 0);
  my_combo_attach(GTK_GRID(grid), eqid_combo_box, 2, 1, 1, 1);
  g_signal_connect(eqid_combo_box, "changed", G_CALLBACK(eqid_changed_cb), NULL);
  enable_b = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(enable_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), receiver[0]->eq_enable);
  g_signal_connect(enable_b, "toggled", G_CALLBACK(enable_cb), GINT_TO_POINTER(0));
  gtk_grid_attach(GTK_GRID(grid), enable_b, 0, 1, 1, 1);
  tenband_b = gtk_check_button_new_with_label("Ten Bands");
  gtk_widget_set_name(tenband_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(tenband_b), receiver[0]->eq_tenband);
  g_signal_connect(tenband_b, "toggled", G_CALLBACK(tenband_cb), GINT_TO_POINTER(0));
  gtk_grid_attach(GTK_GRID(grid), tenband_b, 1, 1, 1, 1);

  for (int i = 0; i < 11; i++) {
    if (i == 0) {
      GtkWidget *label = gtk_label_new("Preamp  ");
      gtk_widget_set_name(label, "boldlabel");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 2);
    } else {
      freqspin[i] = gtk_spin_button_new_with_range(50.0, 16000.0, 10.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), receiver[0]->eq_freq[i]);
      g_signal_connect(freqspin[i], "value-changed", G_CALLBACK(freq_changed_cb), GINT_TO_POINTER(i));

      if (i < 5) {
        gtk_grid_attach(GTK_GRID(grid), freqspin[i], 0, 2 * i + 2, 1, 1);
      } else {
        gtk_grid_attach(GTK_GRID(grid), freqspin[i], 4, 2 * i - 8, 1, 1);
      }
    }

    scale[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 15.0, 1.0);
    gtk_widget_set_size_request(scale[i], 300, 1);
    gtk_range_set_increments (GTK_RANGE(scale[i]), 1.0, 1.0);
    gtk_range_set_value(GTK_RANGE(scale[i]), receiver[0]->eq_gain[i]);
    g_signal_connect(scale[i], "value-changed", G_CALLBACK(gain_changed_cb), GINT_TO_POINTER(i));

    if (i < 5) {
      gtk_grid_attach(GTK_GRID(grid), scale[i], 1, 2 * i + 2, 2, 2);
    } else {
      gtk_grid_attach(GTK_GRID(grid), scale[i], 5, 2 * i - 8, 2, 2);
    }

    gtk_scale_add_mark(GTK_SCALE(scale[i]), -12.0, GTK_POS_LEFT, "-12dB");
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -9.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -6.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -3.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 0.0, GTK_POS_LEFT, "0dB");
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 3.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 6.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 9.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 12.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 15.0, GTK_POS_LEFT, "15dB");
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  if (!have_tenband) {
    for (int i = 5; i < 11; i++) {
      gtk_widget_hide(freqspin[i]);
      gtk_widget_hide(scale   [i]);
    }
  }

  //
  // Move dialog window such that it aligns with the
  // top window. So, if the dialog is too wide for the
  // screen, at least the four-band sliders are on the screen.
  //
  gint x_pos, y_pos;
  gtk_window_get_position(GTK_WINDOW(top_window), &x_pos, &y_pos);
  gtk_window_move(GTK_WINDOW(dialog), x_pos, y_pos);
}
