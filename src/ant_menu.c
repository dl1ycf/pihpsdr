/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include "band.h"
#include "client_server.h"
#include "message.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "radio.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif

static GtkWidget *dialog = NULL;
static GtkWidget *grid = NULL;
static GtkWidget *hf_container = NULL;
static GtkWidget *xvtr_container = NULL;

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

static void rx_ant_cb(GtkToggleButton *widget, gpointer data) {
  int b = GPOINTER_TO_INT(data);
  int ant = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  BAND *band = band_get_band(b);
  band->RxAntenna = ant;

  if (radio_is_remote) {
    send_band_data(cl_sock_tcp, b);
  } else {
    radio_apply_band_settings(0, 0);
  }
}

static void tx_ant_cb(GtkToggleButton *widget, gpointer data) {
  int b = GPOINTER_TO_INT(data);
  int ant = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  BAND *band = band_get_band(b);
  band->TxAntenna = ant;

  if (radio_is_remote) {
    send_band_data(cl_sock_tcp, b);
  } else {
    radio_apply_band_settings(0, 0);
  }
}

//
// Currently the SoapySDR antenna setting is global
// (for all bands)
//
static void adc_antenna_cb(GtkComboBox *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  int ant = gtk_combo_box_get_active(widget);
  adc[id].antenna = ant;

  if (device == SOAPYSDR_USB_DEVICE) {
    if (radio_is_remote) {
      send_soapy_rxant(cl_sock_tcp, id);
    } else {
#ifdef SOAPYSDR
      soapy_protocol_set_rx_antenna(id, ant);
#endif
    }
  }
}

static void dac_antenna_cb(GtkComboBox *widget, gpointer data) {
  if (!can_transmit) { return; }

  if (radio_is_transmitting()) {
    //
    // Suppress TX antenna changes while transmitting
    // or if there is no transceiver
    //
    gtk_combo_box_set_active(widget, transmitter->antenna);
    return;
  }

  transmitter->antenna = gtk_combo_box_get_active(widget);

  if (device == SOAPYSDR_USB_DEVICE) {
    if (radio_is_remote) {
      send_soapy_txant(cl_sock_tcp);
    } else {
#ifdef SOAPYSDR
      soapy_protocol_set_tx_antenna(transmitter->antenna);
#endif
    }
  }
}

static void show_hf(void) {
  GtkWidget *label;
  int bands = radio_max_band();
  hf_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), hf_container, 0, 1, 6, 1);
  GtkWidget *mygrid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(mygrid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(mygrid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(mygrid), 5);
  label = gtk_label_new("Band");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 0, 0, 1, 1);
  label = gtk_label_new("RX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 1, 0, 1, 1);
  label = gtk_label_new("TX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 2, 0, 1, 1);
  label = gtk_label_new("   ");
  gtk_grid_attach(GTK_GRID(mygrid), label, 3, 0, 1, 1);
  label = gtk_label_new("Band");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 4, 0, 1, 1);
  label = gtk_label_new("RX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 5, 0, 1, 1);
  label = gtk_label_new("TX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 6, 0, 1, 1);
  int col = 0;
  int row = 1;

  for (int i = 0; i <= bands + 2; i++) {
    int b = i;

    if (i == bands + 1) {
      b = bandWWV;
    } else if (i == bands + 2) {
      b = bandGen;
    }

    const BAND *band = band_get_band(b);

    if (strlen(band->title) > 0) {
      if (col > 6) {
        row++;
        col = 0;
      }

      label = gtk_label_new(band->title);
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
      col++;
      GtkWidget *rxcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant3");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Xvtr");
      gtk_combo_box_set_active(GTK_COMBO_BOX(rxcombo), band->RxAntenna);
      my_combo_attach(GTK_GRID(mygrid), rxcombo, col, row, 1, 1);
      g_signal_connect(rxcombo, "changed", G_CALLBACK(rx_ant_cb), GINT_TO_POINTER(b));
      col++;
      GtkWidget *txcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant3");
      gtk_combo_box_set_active(GTK_COMBO_BOX(txcombo), band->TxAntenna);
      my_combo_attach(GTK_GRID(mygrid), txcombo, col, row, 1, 1);
      g_signal_connect(txcombo, "changed", G_CALLBACK(tx_ant_cb), GINT_TO_POINTER(b));
      col++;
      col++;
    }
  }

  gtk_container_add(GTK_CONTAINER(hf_container), mygrid);
}

static void show_xvtr(void) {
  GtkWidget *label; // re-used for all labels
  int num = 0;

  for (int i = BANDS; i < BANDS + XVTRS; i++) {
    const BAND *band = band_get_band(i);

    if (strlen(band->title) > 0) { num++; }
  }

  // If there are no transverters, there is nothing to do
  if (num == 0) {
    xvtr_container = NULL;
    return;
  }

  xvtr_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), xvtr_container, 0, 1, 6, 1);
  GtkWidget *mygrid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(mygrid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(mygrid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(mygrid), 5);
  label = gtk_label_new("Band");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 0, 0, 1, 1);
  label = gtk_label_new("RX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 1, 0, 1, 1);
  label = gtk_label_new("TX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 2, 0, 1, 1);

  if (num > 1) {
    // Only if there is more than one xvtr band, the
    // second column is used
    label = gtk_label_new("   ");
    gtk_grid_attach(GTK_GRID(mygrid), label, 3, 0, 1, 1);
    label = gtk_label_new("Band");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 4, 0, 1, 1);
    label = gtk_label_new("RX Ant");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 5, 0, 1, 1);
    label = gtk_label_new("TX Ant");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 6, 0, 1, 1);
  }

  int col = 0;
  int row = 1;

  for (int i = BANDS; i < BANDS + XVTRS; i++) {
    const BAND *band = band_get_band(i);

    if (strlen(band->title) > 0) {
      if (col > 6) {
        row++;
        col = 0;
      }

      label = gtk_label_new(band->title);
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
      col++;
      GtkWidget *rxcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant3");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Xvtr");
      gtk_combo_box_set_active(GTK_COMBO_BOX(rxcombo), band->RxAntenna);
      my_combo_attach(GTK_GRID(mygrid), rxcombo, col, row, 1, 1);
      g_signal_connect(rxcombo, "changed", G_CALLBACK(rx_ant_cb), GINT_TO_POINTER(i));
      col++;
      GtkWidget *txcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant3");
      gtk_combo_box_set_active(GTK_COMBO_BOX(txcombo), band->TxAntenna);
      my_combo_attach(GTK_GRID(mygrid), txcombo, col, row, 1, 1);
      g_signal_connect(txcombo, "changed", G_CALLBACK(tx_ant_cb), GINT_TO_POINTER(i));
      col++;
      col++;
    }
  }

  gtk_container_add(GTK_CONTAINER(xvtr_container), mygrid);
}

static void hf_rb_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (xvtr_container != NULL) { gtk_widget_hide(xvtr_container); }

  if (hf_container   != NULL) { gtk_widget_show(hf_container  ); }
}

static void xvtr_rb_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (xvtr_container != NULL) { gtk_widget_show(xvtr_container); }

  if (hf_container   != NULL) { gtk_widget_hide(hf_container  ); }
}

void ant_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - ANT");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);

  if (device != SOAPYSDR_USB_DEVICE) {
    GtkWidget *hf_rb = gtk_radio_button_new_with_label(NULL, "HF");
    gtk_widget_set_name(hf_rb, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hf_rb), TRUE);
    g_signal_connect(hf_rb, "toggled", G_CALLBACK(hf_rb_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), hf_rb, 1, 0, 1, 1);
    GtkWidget *xvtr_rb = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(hf_rb), "XVTR");
    gtk_widget_set_name(xvtr_rb, "boldlabel");
    g_signal_connect(xvtr_rb, "toggled", G_CALLBACK(xvtr_rb_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), xvtr_rb, 2, 0, 1, 1);
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    show_hf();
    show_xvtr();
  }

  if (device == SOAPYSDR_USB_DEVICE) {
    int row = 1;

    for (int id = 0; id < RECEIVERS; id++) {
      if (radio->soapy.rx[id].antennas > 0) {
        char text[64];
        snprintf(text, sizeof(text), "RX%d Antenna", id + 1);
        GtkWidget *label = gtk_label_new(text);
        gtk_widget_set_name(label, "boldlabel");
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        GtkWidget *box = gtk_combo_box_text_new();

        for (int i = 0; i < radio->soapy.rx[id].antennas; i++) {
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(box), NULL, radio->soapy.rx[id].antenna[i]);
        }

        gtk_combo_box_set_active(GTK_COMBO_BOX(box), adc[id].antenna);
        g_signal_connect(box, "changed", G_CALLBACK(adc_antenna_cb), GINT_TO_POINTER(id));
        my_combo_attach(GTK_GRID(grid), box, 1, row, 1, 1);
        row++;
      }
    }

    if (can_transmit && radio->soapy.tx.antennas > 0) {
      GtkWidget *label = gtk_label_new("TX Antenna");
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
      GtkWidget *box = gtk_combo_box_text_new();

      for (int i = 0; i < radio->soapy.tx.antennas; i++) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(box), NULL, radio->soapy.tx.antenna[i]);
      }

      gtk_combo_box_set_active(GTK_COMBO_BOX(box), transmitter->antenna);
      g_signal_connect(box, "changed", G_CALLBACK(dac_antenna_cb), NULL);
      my_combo_attach(GTK_GRID(grid), box, 1, row, 1, 1);
    }
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  if (xvtr_container != NULL) { gtk_widget_hide(xvtr_container); }
}
