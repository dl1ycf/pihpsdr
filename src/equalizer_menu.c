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

#include <wdsp.h>

#include "new_menu.h"
#include "equalizer_menu.h"
#include "radio.h"
#include "ext.h"
#include "vfo.h"
#include "transmitter.h"
#include "message.h"

static GtkWidget *dialog = NULL;

static GtkWidget *scale[5];
static GtkWidget *freqlabel[5];
static GtkWidget *enable_b;

static int eqid;  // 0: RX0, 1: RX1, 2: TX

static int enable_sem = 0;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
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
      receiver_set_equalizer(receiver[id]);
    }

#ifdef CLIENT_SERVER
  }

#endif
}

static void enable_cb (GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  int m;

  if (! enable_sem) { 
    switch (eqid) {
    case 0:
    case 1:
      if (eqid < receivers) {
        receiver[eqid]->eq_enable = val;
      }
      if (eqid == 0) {
        m = vfo[eqid].mode;
        mode_settings[m].en_rxeq = val;
      }

      break;

    case 2:
      if (can_transmit) {
        transmitter->eq_enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
        m = vfo[get_tx_vfo()].mode;
        mode_settings[m].en_txeq = transmitter->eq_enable;
      }

      break;
    }

    update_eq();
    g_idle_add(ext_vfo_update, NULL);
  
  } else {
    enable_sem = 0;
  }
}

static void scale_changed_cb (GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  double val = gtk_range_get_value(GTK_RANGE(widget));
  int m;

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_gain[i] = val;
    }
    if (eqid == 0) {
      m = vfo[eqid].mode;
      mode_settings[m].rxeq[i] = val;
    }

    break;

  case 2:
    if (can_transmit) {
      transmitter->eq_gain[i] = val;
      m = vfo[get_tx_vfo()].mode;
      mode_settings[m].txeq[i] = val;
    }

    break;
  }

  update_eq();
}

//
// If RX1 is selected while only one RX is running, or if TX
// is selected but there is no transmitter, silently force
// the combo box back to RX0
// Make 'Enable' show the status of the chosen combo box option 
//
static void eqid_changed_cb(GtkWidget *widget, gpointer data) {
  char text[16];
  eqid = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      for (int i = 0; i < 5; i++) {
        gtk_range_set_value(GTK_RANGE(scale[i]), receiver[eqid]->eq_gain[i]);
      }

      for (int i = 1; i < 5; i++) {
        snprintf(text, 16, "%4d Hz  ", (int)(receiver[eqid]->eq_freq[i] + 0.5));
        gtk_label_set_text(GTK_LABEL(freqlabel[i]), text);
      }
      
      enable_sem = 1; // signal that this is not caused by toggling the enable button
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_b), receiver[eqid]->eq_enable);

    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    }

    break;

  case 2:
    if (can_transmit) {
      for (int i = 0; i < 5; i++) {
        gtk_range_set_value(GTK_RANGE(scale[i]), transmitter->eq_gain[i]);
      }

      for (int i = 1; i < 5; i++) {
        snprintf(text, 16, "%4d Hz  ", (int)(transmitter->eq_freq[i] + 0.5));
        gtk_label_set_text(GTK_LABEL(freqlabel[i]), text);
      }
      
      enable_sem = 1; // signal that this is not caused by toggling the enable button
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), transmitter->eq_enable);
      
    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    }

    break;
  }
}


void equalizer_menu(GtkWidget *parent) {
  char text[16];
  eqid = 0;
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
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *eqid_combo_box = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "RX0 Equalizer Settings");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "RX1 Equalizer Settings");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "TX  Equalizer Settings");
  gtk_combo_box_set_active(GTK_COMBO_BOX(eqid_combo_box), 0);
  my_combo_attach(GTK_GRID(grid), eqid_combo_box, 1, 1, 3, 1);
  g_signal_connect(eqid_combo_box, "changed", G_CALLBACK(eqid_changed_cb), NULL);
  enable_b = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(enable_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), receiver[0]->eq_enable);
  g_signal_connect(enable_b, "toggled", G_CALLBACK(enable_cb), GINT_TO_POINTER(0));
  gtk_grid_attach(GTK_GRID(grid), enable_b, 0, 1, 1, 1);

  for (int i = 0; i < 5; i++) {
    if (i == 0) {
      freqlabel[i] = gtk_label_new("Preamp  ");
      gtk_widget_set_name(freqlabel[i], "boldlabel");
      gtk_widget_set_halign(freqlabel[i], GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), freqlabel[i], 0, 2, 1, 2);
    } else {
      snprintf(text, 16, "%4d Hz  ", (int) receiver[0]->eq_freq[i]);
      freqlabel[i] = gtk_label_new(text);
      gtk_widget_set_name(freqlabel[i], "boldlabel");
      gtk_widget_set_halign(freqlabel[i], GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), freqlabel[i], 0, 2 * i + 2, 1, 2);
    }

    scale[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 15.0, 1.0);
    gtk_range_set_increments (GTK_RANGE(scale[i]), 1.0, 1.0);
    gtk_range_set_value(GTK_RANGE(scale[i]), receiver[0]->eq_gain[i]);
    g_signal_connect(scale[i], "value-changed", G_CALLBACK(scale_changed_cb), GINT_TO_POINTER(i));
    gtk_grid_attach(GTK_GRID(grid), scale[i], 1, 2 * i + 2, 3, 2);
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
}
