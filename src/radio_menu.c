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
#include "discovered.h"
#include "ext.h"
#include "main.h"
#include "message.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "radio.h"
#include "sliders.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "vfo.h"

static GtkWidget *dialog = NULL;

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

static void rx_gain_element_changed_cb(GtkWidget *widget, gpointer data) {
  if (device == SOAPYSDR_USB_DEVICE) {
#ifdef SOAPYSDR
    int id = GPOINTER_TO_INT(data);
    double gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

    if (radio_is_remote) {
      // TODO
      //  Note that if the gain element is changed on the remote radio,
      //  the RF-gain is re-calculated so we then need to update the
      //  RF-gain (and the slider) on the client side.
      return;
    }

    soapy_protocol_set_rx_gain_element(id, (char *)gtk_widget_get_name(widget), gain);
    g_idle_add(sliders_rf_gain, GINT_TO_POINTER(100 * suppress_popup_sliders + id));
#endif
  }
}

static void tx_gain_element_changed_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit && device == SOAPYSDR_USB_DEVICE) {
#ifdef SOAPYSDR
    double gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

    if (radio_is_remote) {
      // TODO
      //  Note that if the gain element is changed on the remote radio,
      //  the RF-gain is re-calculated so we then need to update the
      //  RF-gain (and the slider) on the client side.
      return;
    }

    soapy_protocol_set_tx_gain_element((char *)gtk_widget_get_name(widget), (int) gain);
    g_idle_add(sliders_drive, GINT_TO_POINTER(100 * suppress_popup_sliders));
#endif
  }
}

static void agc_changed_cb(GtkWidget *widget, gpointer data) {
  if (device == SOAPYSDR_USB_DEVICE) {
    int id = GPOINTER_TO_INT(data);
    int agc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    adc[id].agc = agc;

    if (radio_is_remote) {
      send_soapy_agc(client_socket, id);
      return;
    }

#ifdef SOAPYSDR
    soapy_protocol_set_automatic_gain(id, agc);

    if (!agc) { soapy_protocol_set_rx_gain(id); }

#endif
  }
}


static void calibration_value_changed_cb(GtkWidget *widget, gpointer data) {
  double f = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  if (f >= 0) {
    frequency_calibration = (int) (10.0 * f + 0.5);
  } else {
    frequency_calibration = (int) (10.0 * f - 0.5);
  }
  t_print("F=%f C=%d\n", f, frequency_calibration);

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }
}

static void rx_gain_calibration_value_changed_cb(GtkWidget *widget, gpointer data) {
  rx_gain_calibration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }
}

static void vfo_divisor_value_changed_cb(GtkWidget *widget, gpointer data) {
  vfo_encoder_divisor = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void ptt_ring_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    mic_ptt_tip_bias_ring = 0;
  }

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  } else {
    schedule_transmit_specific();
  }
}

static void ptt_tip_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    mic_ptt_tip_bias_ring = 1;
  }

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  } else {
    schedule_transmit_specific();
  }
}

static void toggle_cb(GtkWidget *widget, gpointer data) {
  int *value = (int *) data;
  *value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  } else {
    schedule_general();
    schedule_transmit_specific();
    schedule_high_priority();
  }
}

static void anan10e_cb(GtkWidget *widget, gpointer data) {
  int new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (radio_is_remote) {
    send_anan10E(client_socket, new);
  } else {
    radio_set_anan10E(new);
  }
}

static void split_cb(GtkWidget *widget, gpointer data) {
  int new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (radio_is_remote) {
    send_split(client_socket, new);
  } else {
    radio_set_split(new);
  }
}

static void duplex_cb(GtkWidget *widget, gpointer data) {
  if (radio_is_transmitting()) {
    //
    // While transmitting, ignore the click
    //
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), duplex);
    return;
  }

  int val  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_duplex(val);
}

static void sat_cb(GtkWidget *widget, gpointer data) {
  sat_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void filter_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  int fb;

  switch (val) {
  case 0:
  default:
    fb = NO_FILTER_BOARD;
    break;

  case 1:
    fb = ALEX;
    break;

  case 2:
    fb = APOLLO;
    break;

  case 3:
    fb = CHARLY25;
    break;

  case 4:
    fb = N2ADR;
    break;
  }

  radio_load_filters(fb);
}

static void mic_input_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
  default:
    mic_input_xlr = MIC3P55MM;
    break;

  case 1:
    mic_input_xlr = MICXLR;
    break;
  }

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  } else {
    schedule_transmit_specific();
  }
}

static void sample_rate_cb(GtkToggleButton *widget, gpointer data) {
  const char *p = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  int samplerate;

  //
  // There are so many different possibilities for sample rates, so
  // we just "scanf" from the combobox text entry
  //
  if (sscanf(p, "%d", &samplerate) != 1) { return; }

  if (radio_is_remote) {
    //
    // We arrive here only for P1 and SOAPY and change the sample rate
    // of *all* receivers
    //
    for (int id = 0; id < RECEIVERS; id++) {
      send_sample_rate(client_socket, id, samplerate);
    }
  } else {
    radio_change_sample_rate(samplerate);
  }
}

static void receivers_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget)) + 1;

  //
  // reconfigure_radio requires that the RX panels are active
  // (segfault otherwise), therefore ignore this while TXing
  //
  if (radio_is_transmitting()) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), receivers - 1);
    return;
  }

  if (radio_is_remote) {
    send_receivers(client_socket, val);
  } else {
    radio_change_receivers(val);
  }
}

static void region_cb(GtkWidget *widget, gpointer data) {
  int r = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_region(client_socket, r);
  } else {
    radio_change_region(r);
  }
}

static void rit_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
  default:
    vfo_set_rit_step(1);
    break;

  case 1:
    vfo_set_rit_step(10);
    break;

  case 2:
    vfo_set_rit_step(100);
    break;
  }
}

static void ck10mhz_cb(GtkWidget *widget, gpointer data) {
  atlas_clock_source_10mhz = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }
}

static void ck128mhz_cb(GtkWidget *widget, gpointer data) {
  atlas_clock_source_128mhz = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }
}

static void micsource_cb(GtkWidget *widget, gpointer data) {
  atlas_mic_source = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }
}

static void tx_cb(GtkWidget *widget, gpointer data) {
  atlas_penelope = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_radiomenu(client_socket);
  }
}

void radio_menu(GtkWidget *parent) {
  int col;
  GtkWidget *label;
  GtkWidget *ChkBtn;
  GtkWidget *Separator;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Radio");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
  int row;
  int max_row;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button_press_event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  row = 1;
  label = gtk_label_new("Receivers:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  row++;
  GtkWidget *receivers_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(receivers_combo), NULL, "1");

  if (radio->supported_receivers > 1) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(receivers_combo), NULL, "2");
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(receivers_combo), receivers - 1);
  my_combo_attach(GTK_GRID(grid), receivers_combo, 0, row, 1, 1);
  g_signal_connect(receivers_combo, "changed", G_CALLBACK(receivers_cb), NULL);
  row++;

  switch (protocol) {
  case NEW_PROTOCOL:
    // Sample rate changes handled in the RX menu
    break;

  case ORIGINAL_PROTOCOL: {
    label = gtk_label_new("Sample Rate:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    row++;
    GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "48000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "96000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "192000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "384000");

    switch (active_receiver->sample_rate) {
    case 48000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 0);
      break;

    case 96000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 1);
      break;

    case 192000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 2);
      break;

    case 384000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 3);
      break;
    }

    my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 0, row, 1, 1);
    g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), NULL);
    row++;
  }
  break;

  case SOAPYSDR_PROTOCOL: {
    label = gtk_label_new("Sample Rate:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    row++;
    char rate_string[16];
    GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
    int rate = radio->soapy.sample_rate;
    int pos = 0;

    while (rate >= 48000) {
      snprintf(rate_string, sizeof(rate_string), "%d", rate);
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, rate_string);

      if (rate == active_receiver->sample_rate) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), pos);
      }

      rate = rate / 2;
      pos++;
    }

    my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 0, row, 1, 1);
    g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), NULL);
  }

  row++;
  break;
  }

  max_row = row;
  row = 1;
  label = gtk_label_new("RIT/XIT step (Hz):");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
  row++;
  GtkWidget *rit_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_combo), NULL, "1");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_combo), NULL, "10");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_combo), NULL, "100");

  switch (vfo[active_receiver->id].rit_step) {
  default:
    // we should not arrive here, but just in case ...
    vfo_set_rit_step(1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 0);
    break;

  case 1:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 0);
    break;

  case 10:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 1);
    break;

  case 100:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 2);
    break;
  }

  my_combo_attach(GTK_GRID(grid), rit_combo, 1, row, 1, 1);
  g_signal_connect(rit_combo, "changed", G_CALLBACK(rit_cb), NULL);
  row++;
  label = gtk_label_new("SAT mode:");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
  row++;
  GtkWidget *sat_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sat_combo), NULL, "SAT Off");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sat_combo), NULL, "SAT");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sat_combo), NULL, "RSAT");
  gtk_combo_box_set_active(GTK_COMBO_BOX(sat_combo), sat_mode);
  my_combo_attach(GTK_GRID(grid), sat_combo, 1, row, 1, 1);
  g_signal_connect(sat_combo, "changed", G_CALLBACK(sat_cb), NULL);
  row++;

  if (row > max_row) { max_row = row; }

  row = 1;
  label = gtk_label_new("Region:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
  row++;
  GtkWidget *region_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "Other");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "UK");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "WRC15");
  gtk_combo_box_set_active(GTK_COMBO_BOX(region_combo), region);
  my_combo_attach(GTK_GRID(grid), region_combo, 2, row, 1, 1);
  g_signal_connect(region_combo, "changed", G_CALLBACK(region_cb), NULL);
  row++;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    label = gtk_label_new("Filter Board:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
    row++;
    GtkWidget *filter_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "NONE");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "ALEX");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "APOLLO");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "CHARLY25");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "N2ADR");

    switch (filter_board) {
    case NO_FILTER_BOARD:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 0);
      break;

    case ALEX:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 1);
      break;

    case APOLLO:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 2);
      break;

    case CHARLY25:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 3);
      break;

    case N2ADR:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 4);
      break;
    }

    my_combo_attach(GTK_GRID(grid), filter_combo, 2, row, 1, 1);
    g_signal_connect(filter_combo, "changed", G_CALLBACK(filter_cb), NULL);
    row++;
  }

  if (row > max_row) { max_row = row; }

  row = max_row;
  label = gtk_label_new("VFO Enc Div:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  GtkWidget *vfo_divisor = gtk_spin_button_new_with_range(1.0, 60.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(vfo_divisor), (double)vfo_encoder_divisor);
  gtk_grid_attach(GTK_GRID(grid), vfo_divisor, 1, row, 1, 1);
  g_signal_connect(vfo_divisor, "value_changed", G_CALLBACK(vfo_divisor_value_changed_cb), NULL);
  ChkBtn = gtk_check_button_new_with_label("VFO snap");
  gtk_widget_set_name(ChkBtn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), vfo_snap);
  gtk_grid_attach(GTK_GRID(grid), ChkBtn, 2, row, 1, 1);
  g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &vfo_snap);
  row++;

  // cppcheck-suppress knownConditionTrueFalse
  if (row > max_row) { max_row = row; }

  //
  // The HPSDR machine-specific stuff is now put in columns 3+4
  // either the ATLAS bits (METIS) or the ORION microphone settings
  //
  if (device == DEVICE_OZY || device == DEVICE_METIS) {
    row = 1;
    label = gtk_label_new("ATLAS bus settings:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 2, 1);
    row++;
    label = gtk_label_new("10 MHz source:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *ck10mhz_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck10mhz_combo), NULL, "Atlas");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck10mhz_combo), NULL, "Penelope");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck10mhz_combo), NULL, "Mercury");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ck10mhz_combo), atlas_clock_source_10mhz);
    my_combo_attach(GTK_GRID(grid), ck10mhz_combo, 4, row, 1, 1);
    g_signal_connect(ck10mhz_combo, "changed", G_CALLBACK(ck10mhz_cb), NULL);
    row++;
    label = gtk_label_new("122.88 MHz source:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *ck128mhz_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck128mhz_combo), NULL, "Penelope");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck128mhz_combo), NULL, "Mercury");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ck128mhz_combo), SET(atlas_clock_source_128mhz));
    my_combo_attach(GTK_GRID(grid), ck128mhz_combo, 4, row, 1, 1);
    g_signal_connect(ck128mhz_combo, "changed", G_CALLBACK(ck128mhz_cb), NULL);
    row++;
    label = gtk_label_new("Mic source:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *micsource_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(micsource_combo), NULL, "Janus");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(micsource_combo), NULL, "Penelope");
    gtk_combo_box_set_active(GTK_COMBO_BOX(micsource_combo), SET(atlas_mic_source));
    my_combo_attach(GTK_GRID(grid), micsource_combo, 4, row, 1, 1);
    g_signal_connect(micsource_combo, "changed", G_CALLBACK(micsource_cb), NULL);
    row++;
    label = gtk_label_new("TX config:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *tx_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tx_combo), NULL, "No TX");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tx_combo), NULL, "Penelope");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tx_combo), NULL, "Pennylane");
    gtk_combo_box_set_active(GTK_COMBO_BOX(tx_combo), atlas_penelope);
    my_combo_attach(GTK_GRID(grid), tx_combo, 4, row, 1, 1);
    g_signal_connect(tx_combo, "changed", G_CALLBACK(tx_cb), NULL);
    row++;

    //
    // This option is for ATLAS systems which *only* have an OZY
    // and a JANUS board (the RF front end then is either SDR-1000 or SoftRock)
    //
    // It is assumed that the SDR-1000 is controlled outside piHPSDR
    //
    if (device == DEVICE_OZY) {
      ChkBtn = gtk_check_button_new_with_label("Janus Only");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), atlas_janus);
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, 4, row, 1, 1);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &atlas_janus);
      row++;
    }

    if (row > max_row) { max_row = row; }
  }

  if (device == NEW_DEVICE_ORION || device == NEW_DEVICE_ORION2 || device == NEW_DEVICE_SATURN ||
      device == DEVICE_ORION || device == DEVICE_ORION2) {
    row = 1;
    label = gtk_label_new("ORION/SATURN Mic jack:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 2, 1);
    row++;
    GtkWidget *ptt_ring_b = gtk_radio_button_new_with_label(NULL, "PTT On Ring, Mic and Bias on Tip");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ptt_ring_b), mic_ptt_tip_bias_ring == 0);
    gtk_grid_attach(GTK_GRID(grid), ptt_ring_b, 3, row, 2, 1);
    g_signal_connect(ptt_ring_b, "toggled", G_CALLBACK(ptt_ring_cb), NULL);
    row++;
    GtkWidget *ptt_tip_b = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(ptt_ring_b),
                           "PTT On Tip, Mic and Bias on Ring");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ptt_tip_b), mic_ptt_tip_bias_ring == 1);
    gtk_grid_attach(GTK_GRID(grid), ptt_tip_b, 3, row, 2, 1);
    g_signal_connect(ptt_tip_b, "toggled", G_CALLBACK(ptt_tip_cb), NULL);
    row++;

    if (device == NEW_DEVICE_SATURN) {
      label = gtk_label_new("Mic Input:");
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), label, 4, row, 1, 1);
      GtkWidget *mic_input_combo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_input_combo), NULL, "3.5mm");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_input_combo), NULL, "XLR");

      switch (mic_input_xlr) {
      case MIC3P55MM:
        gtk_combo_box_set_active(GTK_COMBO_BOX(mic_input_combo), 0);
        break;

      case MICXLR:
        gtk_combo_box_set_active(GTK_COMBO_BOX(mic_input_combo), 1);
        break;
      }

      my_combo_attach(GTK_GRID(grid), mic_input_combo, 4, row + 1, 1, 1);
      g_signal_connect(mic_input_combo, "changed", G_CALLBACK(mic_input_cb), NULL);
    }

    ChkBtn = gtk_check_button_new_with_label("PTT Enabled");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mic_ptt_enabled);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 3, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mic_ptt_enabled);
    row++;
    ChkBtn = gtk_check_button_new_with_label("BIAS Enabled");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mic_bias_enabled);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 3, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mic_bias_enabled);
    row++;

    if (row > max_row) { max_row = row; }
  }

  row = max_row;
  col = 0;
  //
  // Insert small separation between top columns and bottom rows
  //
  Separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(Separator, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), Separator, col, row, 6, 1);
  row++;

  if (can_transmit) {
    ChkBtn = gtk_check_button_new_with_label("Split");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), split);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(split_cb), NULL);
    col++;
    ChkBtn = gtk_check_button_new_with_label("Duplex");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), duplex);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(duplex_cb), NULL);
    col++;
    ChkBtn = gtk_check_button_new_with_label("Mute RX when TX");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mute_rx_while_transmitting);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mute_rx_while_transmitting);

    if (protocol ==  ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      col++;
      ChkBtn = gtk_check_button_new_with_label("PA enable");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), pa_enabled);
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &pa_enabled);
    }

    row++;
  }

  col = 0;
  ChkBtn = gtk_check_button_new_with_label("Optimize for TouchScreen");
  gtk_widget_set_name(ChkBtn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), optimize_for_touchscreen);
  gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 2, 1);
  g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &optimize_for_touchscreen);
  col += 2;

  switch (device) {
  case NEW_DEVICE_ORION2:
  case NEW_DEVICE_SATURN: {
    ChkBtn = gtk_check_button_new_with_label("Mute Spkr Amp");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mute_spkr_amp);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mute_spkr_amp);
    col++;
    ChkBtn = gtk_check_button_new_with_label("Mute Spkr when TX");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mute_spkr_xmit);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mute_spkr_xmit);
    col++;
  }
  break;

  case DEVICE_HERMES_LITE2: {
    ChkBtn = gtk_check_button_new_with_label("HL2 audio codec");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), hl2_audio_codec);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &hl2_audio_codec);
    col++;
    ChkBtn = gtk_check_button_new_with_label("HL2 CL1/2");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), hl2_cl1_input);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &hl2_cl1_input);
    col++;
  }
  break;

  case DEVICE_HERMES:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2: {
    ChkBtn = gtk_check_button_new_with_label("Anan-10E/100B");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), anan10E);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(anan10e_cb), NULL);
    col++;
  }
  break;

  case SOAPYSDR_USB_DEVICE: {
    ChkBtn = gtk_check_button_new_with_label("Swap IQ");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), soapy_iqswap);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &soapy_iqswap);
    col++;

    for (int id = 0; id < RECEIVERS; id++) {
      if (radio->soapy.rx[id].has_automatic_gain) {
        char text[64];
        snprintf(text, sizeof(text), "HW AGC RX%d", id + 1);
        ChkBtn = gtk_check_button_new_with_label(text);
        gtk_widget_set_name(ChkBtn, "boldlabel");
        gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), adc[id].agc);
        g_signal_connect(ChkBtn, "toggled", G_CALLBACK(agc_changed_cb), GINT_TO_POINTER(id));
        col++;
      }
    }
  }
  break;
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    row++;
    ChkBtn = gtk_check_button_new_with_label("Enable TxInhibit Input");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), enable_tx_inhibit);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 0, row, 2, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &enable_tx_inhibit);
    ChkBtn = gtk_check_button_new_with_label("Enable AutoTune Input");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), enable_auto_tune);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 2, row, 2, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &enable_auto_tune);
  }

  row++;
  // cppcheck-suppress redundantAssignment
  col = 0;
  label = gtk_label_new("Frequency\nCalibration (ppm):");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *calibration_b = gtk_spin_button_new_with_range(-2500.0, 2500.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(calibration_b), 0.1*(double)frequency_calibration);
  gtk_grid_attach(GTK_GRID(grid), calibration_b, col, row, 1, 1);
  g_signal_connect(calibration_b, "value_changed", G_CALLBACK(calibration_value_changed_cb), NULL);
  //
  // Calibration of the RF front end
  //
  col++;
  label = gtk_label_new("RX Gain\nCalibration (dB):");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *rx_gain_calibration_b = gtk_spin_button_new_with_range(-50.0, 50.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_gain_calibration_b), (double)rx_gain_calibration);
  gtk_grid_attach(GTK_GRID(grid), rx_gain_calibration_b, col, row, 1, 1);
  g_signal_connect(rx_gain_calibration_b, "value_changed", G_CALLBACK(rx_gain_calibration_value_changed_cb), NULL);
  //
  // If we are running a SoapySDR radio where at least one channel
  // has more than one gain element, draw a separator line and
  // produce controls that allow manipulation of these gain elements.
  //
  // A channel with only a singe gain element is not shown (use the
  // RF-gain or TX-drive slider instead).
  //
  // Currently, this is not supported on the client side since we must
  // then asynchronously query the gain elements (the change with the
  // RF-gain and TX-drive slider in an unpredictable way).
  //
  int soapy_display_gains = 0;

  for (int id = 0; id < RECEIVERS; id++) {
    if (radio->soapy.rx[id].gains > 1) { soapy_display_gains = 1; }
  }

  if (radio->soapy.tx.gains > 1) { soapy_display_gains = 1; }

  if ((device != SOAPYSDR_USB_DEVICE) || radio_is_remote) {
    soapy_display_gains = 0;
  }

  if (soapy_display_gains) {
    row++;
    Separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(Separator, -1, 3);
    gtk_grid_attach(GTK_GRID(grid), Separator, 0, row, 6, 1);
    row++;
    max_row = row;
    //
    // Display spin buttons for gain elements, but only if there are more than one
    //
    col = 0;

    for (int id = 0; id < RECEIVERS; id++) {
      if (radio->soapy.rx[id].gains > 0) {
        char text[64];
        row = max_row;
        snprintf(text, sizeof(text), "RX%d Gains:", id + 1);
        label = gtk_label_new(text);
        gtk_widget_set_name(label, "boldlabel");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, col + 1, row, 1, 1);
        row++;

        for (int i = 0; i < radio->soapy.rx[id].gains; i++) {
          label = gtk_label_new(radio->soapy.rx[id].gain_elem_name[i]);
          gtk_widget_set_name(label, "boldlabel");
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
          double range_step = radio->soapy.rx[id].gain_elem_step[i];
          double range_min  = radio->soapy.rx[id].gain_elem_min[i];
          double range_max  = radio->soapy.rx[id].gain_elem_max[i];

          if (range_step == 0.0) {
            range_step = 1.0;
          }

          GtkWidget *wgain = gtk_spin_button_new_with_range(range_min, range_max, range_step);
          gtk_widget_set_name (wgain, radio->soapy.rx[id].gain_elem_name[i]);
#ifdef SOAPYSDR
          double value = soapy_protocol_get_rx_gain_element(id, radio->soapy.rx[id].gain_elem_name[i]);
          gtk_spin_button_set_value(GTK_SPIN_BUTTON(wgain), value);
#endif
          gtk_grid_attach(GTK_GRID(grid), wgain, col + 1, row, 1, 1);
          g_signal_connect(wgain, "value_changed", G_CALLBACK(rx_gain_element_changed_cb), GINT_TO_POINTER(id));
          row++;
        }

        col += 2;
      }
    }

    if (can_transmit && radio->soapy.tx.gains > 0) {
      row = max_row;
      label = gtk_label_new("TX Gains:");
      gtk_widget_set_name(label, "boldlabel");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), label, col + 1, row, 1, 1);
      row++;

      for (int i = 0; i < radio->soapy.tx.gains; i++) {
        label = gtk_label_new(radio->soapy.tx.gain_elem_name[i]);
        gtk_widget_set_name(label, "boldlabel");
        gtk_widget_set_halign(label, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
        double range_step = radio->soapy.tx.gain_elem_step[i];
        double range_min  = radio->soapy.tx.gain_elem_min[i];
        double range_max  = radio->soapy.tx.gain_elem_max[i];

        if (range_step == 0.0) {
          range_step = 1.0;
        }

        GtkWidget *wgain = gtk_spin_button_new_with_range(range_min, range_max, range_step);
        gtk_widget_set_name (wgain, radio->soapy.tx.gain_elem_name[i]);
#ifdef SOAPYSDR
        double value = soapy_protocol_get_tx_gain_element(radio->soapy.tx.gain_elem_name[i]);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(wgain), value);
#endif
        gtk_grid_attach(GTK_GRID(grid), wgain, col + 1, row, 1, 1);
        g_signal_connect(wgain, "value_changed", G_CALLBACK(tx_gain_element_changed_cb), NULL);
        row++;
      }
    }
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
