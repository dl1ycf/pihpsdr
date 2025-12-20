/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "ext.h"
#include "filter.h"
#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "radio.h"
#include "sliders.h"
#include "transmitter.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;
static GtkWidget *input;
static GtkWidget *tx_spin_low;
static GtkWidget *tx_spin_high;
static GtkWidget *swrtune_volume_spin;
static GtkWidget *tunedrive_spin;

static GtkWidget *tx_container;
static GtkWidget *cfc_container;
static GtkWidget *dexp_container;
static GtkWidget *peaks_container;
static GtkWidget *tune_container;

//
// Some symbolic constants used in callbacks
//

enum _containers {
  TX_CONTAINER = 1,
  CFC_CONTAINER,
  DEXP_CONTAINER,
  PEAKS_CONTAINER,
  TUNE_CONTAINER
};
static int which_container = TX_CONTAINER;

#define TX         0
#define DEXP     256
#define CFC      512
#define CFCFREQ  768
#define CFCLVL  1024
#define CFCPOST 1280

enum _tx_choices {
  TX_FPS = TX,
  TX_LINEIN,
  TX_COMP,
  TX_FILTER_LOW,
  TX_FILTER_HIGH,
  TX_PAN_LOW,
  TX_PAN_HIGH,
  TX_PAN_STEP,
  TX_AM_CARRIER,
  TX_TUNE_DRIVE,
  TX_DIGI_DRIVE,
  TX_SWR_ALARM,
  TX_DISPLAY_FILLED,
  TX_COMP_ENABLE,
  TX_CTCSS_ENABLE,
  TX_TUNE_USE_DRIVE,
  TX_SWR_PROTECTION,
  TX_USE_RX_FILTER,
  TX_FM_EMP,
  TX_AUDIO_MON,
  TX_SWRTUNE,
  TX_SWRTUNE_VOLUME
};

enum _dexp_choices {
  DEXP_ONOFF = DEXP,
  DEXP_FILTER,
  DEXP_FILTER_LOW,
  DEXP_FILTER_HIGH,
  DEXP_ATTACK,
  DEXP_RELEASE,
  DEXP_HOLD,
  DEXP_TAU,
  DEXP_HYST,
  DEXP_TRIGGER,
  DEXP_EXP
};

enum _cfc_choices {
  CFC_ONOFF = CFC,
  CFC_EQ
};

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

static void tx_panadapter_peaks_in_passband_filled_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_peaks_in_passband_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_panadapter_hide_noise_filled_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_hide_noise_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_panadapter_peaks_on_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_peaks_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_panadapter_num_peaks_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_num_peaks = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void tx_panadapter_ignore_range_divider_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_ignore_range_divider = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void tx_panadapter_ignore_noise_percentile_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_ignore_noise_percentile = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void sel_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle radio button in the top row, this selects
  // which sub-menu is active
  //
  int c = GPOINTER_TO_INT(data);
  GtkWidget *my_container;

  switch (c) {
  case TX_CONTAINER:
    my_container = tx_container;
    break;

  case CFC_CONTAINER:
    my_container = cfc_container;
    break;

  case DEXP_CONTAINER:
    my_container = dexp_container;
    break;

  case PEAKS_CONTAINER:
    my_container = peaks_container;
    break;

  case TUNE_CONTAINER:
    my_container = tune_container;
    break;

  default:
    // We should never come here
    my_container = NULL;
    break;
  }

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(my_container);
    which_container = c;
  } else {
    gtk_widget_hide(my_container);
  }
}

static void spinbtn_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle ALL spin-buttons in this menu
  //
  int mode = vfo_get_tx_mode();
  int c = GPOINTER_TO_INT(data);
  double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  int    vi = (v >= 0.0) ? (int) (v + 0.5) : (int) (v - 0.5);
  int d = c & 0xFF00;   // The command class, TX/CFCFREQ/CFCLVL/CFCPOST/DEXP
  int e = c & 0x00FF;   // channel No. for CRCFREQ/CFCLVL/CFCPOST

  if (d == TX) {
    switch (c) {
    case TX_LINEIN:
      linein_gain = v;

      if (radio_is_remote) {
        send_txmenu(cl_sock_tcp);
      } else {
        schedule_transmit_specific();
      }

      break;

    case TX_FPS:
      transmitter->fps = vi;

      if (radio_is_remote) {
        send_txfps(cl_sock_tcp, transmitter->fps);
      } else {
        tx_set_framerate(transmitter);
      }

      break;

    case TX_SWRTUNE_VOLUME:
      transmitter->swrtune_volume = v;
      break;

    case TX_COMP:
      transmitter->compressor_level = vi;
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_FILTER_LOW:
      transmitter->default_filter_low = vi;
      tx_set_filter(transmitter);
      break;

    case TX_FILTER_HIGH:
      transmitter->default_filter_high = vi;
      tx_set_filter(transmitter);
      break;

    case TX_PAN_LOW:
      transmitter->panadapter_low = vi;
      break;

    case TX_PAN_HIGH:
      transmitter->panadapter_high = vi;
      break;

    case TX_PAN_STEP:
      transmitter->panadapter_step = vi;
      break;

    case TX_AM_CARRIER:
      transmitter->am_carrier_level = v;

      if (radio_is_remote) {
        send_am_carrier(cl_sock_tcp);
      } else {
        tx_set_am_carrier_level(transmitter);
      }

      break;

    case TX_TUNE_DRIVE:
      transmitter->tune_drive = vi;

      if (radio_is_remote) {
        send_txmenu(cl_sock_tcp);
      }

      break;

    case TX_DIGI_DRIVE:
      drive_digi_max = v;

      if (radio_is_remote) {
        send_digidrivemax(cl_sock_tcp);
      } else if ((mode == modeDIGL || mode == modeDIGU) && transmitter->drive > v + 0.5) {
        radio_set_drive(v);
      }

      break;

    case TX_SWR_ALARM:
      transmitter->swr_alarm = v;

      if (radio_is_remote) {
        send_txmenu(cl_sock_tcp);
      }

      break;
    }
  } else if (d == CFCFREQ) {
    // The CFC frequency spin buttons
    transmitter->cfc_freq[e] = v;
    tx_set_compressor(transmitter);
  } else if (d == CFCLVL) {
    // The CFC compression-level spin buttons
    transmitter->cfc_lvl[e] = v;
    tx_set_compressor(transmitter);
  } else if (d == CFCPOST) {
    // The CFC Post-equaliser gain spin buttons
    transmitter->cfc_post[e] = v;
    tx_set_compressor(transmitter);
  } else if (d == DEXP) {
    // The DEXP spin buttons. Note that the spin buttons for the
    // "time" values are in milli-seconds
    switch (c) {
    case DEXP_TAU:
      transmitter->dexp_tau = 0.001 * v;
      break;

    case DEXP_ATTACK:
      transmitter->dexp_attack = 0.001 * v;
      break;

    case DEXP_RELEASE:
      transmitter->dexp_release = 0.001 * v;
      break;

    case DEXP_HOLD:
      transmitter->dexp_hold = 0.001 * v;
      break;

    case DEXP_HYST:
      transmitter->dexp_hyst = v;
      break;

    case DEXP_TRIGGER:
      transmitter->dexp_trigger = vi;
      break;

    case DEXP_FILTER_LOW:
      transmitter->dexp_filter_low = vi;
      break;

    case DEXP_FILTER_HIGH:
      transmitter->dexp_filter_high = vi;
      break;

    case DEXP_EXP:
      // Note this is in dB
      transmitter->dexp_exp = vi;
      break;
    }

    tx_set_dexp(transmitter);
  }
}

static void chkbtn_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle ALL the check buttons in this menu
  //
  int c = GPOINTER_TO_INT(data);
  int v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  int d = c & 0xFF00;

  if (d == TX) {
    // TX menu check buttons
    switch (c) {
    case TX_DISPLAY_FILLED:
      transmitter->display_filled = v;
      break;

    case TX_COMP_ENABLE:
      transmitter->compressor = v;
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_CTCSS_ENABLE:
      transmitter->ctcss_enabled = v;

      if (radio_is_remote) {
        send_ctcss(cl_sock_tcp);
      } else {
        tx_set_ctcss(transmitter);
      }

      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_TUNE_USE_DRIVE:
      transmitter->tune_use_drive = v;
      gtk_widget_set_sensitive (tunedrive_spin, NOT(v));

      if (radio_is_remote) {
        send_txmenu(cl_sock_tcp);
      }

      break;

    case TX_SWRTUNE:
      transmitter->swrtune = v;
      gtk_widget_set_sensitive (swrtune_volume_spin, SET(v));
      break;

    case TX_SWR_PROTECTION:
      transmitter->swr_protection = v;

      if (radio_is_remote) {
        send_txmenu(cl_sock_tcp);
      }

      break;

    case TX_USE_RX_FILTER:
      transmitter->use_rx_filter = v;
      tx_set_filter(transmitter);
      gtk_widget_set_sensitive (tx_spin_low, NOT(v));
      gtk_widget_set_sensitive (tx_spin_high, NOT(v));
      break;

    case TX_AUDIO_MON:
      transmitter->audiomonitor = v;
      break;

    case TX_FM_EMP:
      transmitter->pre_emphasize = !v;

      if (radio_is_remote) {
        send_preemp(cl_sock_tcp);
      } else {
        tx_set_pre_emphasize(transmitter);
      }

      break;
    }
  } else if (d == CFC) {
    // CFC menu check buttons
    switch (c) {
    case CFC_ONOFF:
      transmitter->cfc = v;
      break;

    case CFC_EQ:
      transmitter->cfc_eq = v;
      break;
    }

    tx_set_compressor(transmitter);
    g_idle_add(ext_vfo_update, NULL);
  } else if (d == DEXP) {
    // DEXP menu check buttons
    switch (c) {
    case DEXP_ONOFF:
      transmitter->dexp = v;
      break;

    case DEXP_FILTER:
      transmitter->dexp_filter = v;
      break;
    }

    tx_set_dexp(transmitter);
    g_idle_add(ext_vfo_update, NULL);
  }
}

//
// For the combo-boxes we do not "fuse" the callbacks
//
static void mic_in_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  switch (i) {
  case 0: // Mic In, but no boost
    mic_boost = 0;
    mic_linein = 0;
    break;

  case 1: // Mic In with boost
    mic_boost = 1;
    mic_linein = 0;
    break;

  case 2: // Line in
    mic_boost = 0;
    mic_linein = 1;
    break;
  }

  if (radio_is_remote) {
    send_txmenu(cl_sock_tcp);
  } else {
    schedule_transmit_specific();
  }
}

static void ctcss_frequency_cb(GtkWidget *widget, gpointer data) {
  transmitter->ctcss = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_ctcss(cl_sock_tcp);
  } else {
    tx_set_ctcss(transmitter);
  }

  g_idle_add(ext_vfo_update, NULL);
}


static void local_input_changed_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (i < 0) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    i = 0;
  }

  if (transmitter->local_audio) {
    transmitter->local_audio = 0;
    audio_close_input(transmitter);
  }

  if (i > 0) {
    transmitter->local_audio = 1;
    snprintf(transmitter->audio_name, sizeof(transmitter->audio_name), "%s", input_devices[i - 1].name);

    if (audio_open_input(transmitter) < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
      transmitter->local_audio = 0;
    }
  }

  int txmode = vfo_get_tx_mode();
  mode_settings[txmode].tx_local_audio = transmitter->local_audio;

  if (transmitter->local_audio) {
    snprintf(mode_settings[txmode].tx_audio_name, sizeof(mode_settings[txmode].tx_audio_name), "%s",
             transmitter->audio_name);
  }

  copy_mode_settings(txmode);
}

void tx_menu(GtkWidget *parent) {
  GtkWidget *btn;
  GtkWidget *mbtn;  // main button for radio buttons
  GtkWidget *label;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Transmit");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 0);
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), TRUE);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_container_add(GTK_CONTAINER(content), grid);
  int row = 0;
  int col = 0;
  int have_swr = 0;
  int have_mic = 0;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    have_swr = 1;
    have_mic = 1;
  }

  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  //
  // Must init the containers here since setting the buttons emits
  // a signal leading to show/hide commands
  //
  tx_container = gtk_fixed_new();
  cfc_container = gtk_fixed_new();
  dexp_container = gtk_fixed_new();
  peaks_container = gtk_fixed_new();
  tune_container = gtk_fixed_new();
  col++;
  mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "TX Settings");
  gtk_widget_set_name(mbtn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), (which_container == TX_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), mbtn, col, row, 1, 1);
  g_signal_connect(mbtn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(TX_CONTAINER));
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "CFC Settings");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == CFC_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(CFC_CONTAINER));
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "DExp Settings");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == DEXP_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(DEXP_CONTAINER));
  row++;
  col = 1;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Peak Labels");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == PEAKS_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(PEAKS_CONTAINER));
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "TUNE and SWR");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == TUNE_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(TUNE_CONTAINER));
  //
  // TX container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), tx_container, 0, 2, 4, 1);
  GtkWidget *tx_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(tx_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(tx_grid), 5);
  gtk_container_add(GTK_CONTAINER(tx_container), tx_grid);
  row = 0;
  row++;
  col = 0;
  label = gtk_label_new("TX Audio In");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(tx_grid), label, col++, row, 1, 1);
  input = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(input), NULL, "From Radio");
  gtk_combo_box_set_active(GTK_COMBO_BOX(input), 0);

  for (int i = 0; i < n_input_devices; i++) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(input), NULL, input_devices[i].description);

    if (transmitter->local_audio && strcmp(transmitter->audio_name, input_devices[i].name) == 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(input), i + 1);
    }
  }

  if (!transmitter->local_audio) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(input), 0);
  }

  my_combo_attach(GTK_GRID(tx_grid), input, col, row, 4, 1);
  g_signal_connect(input, "changed", G_CALLBACK(local_input_changed_cb), NULL);

  if (have_mic) {
    row++;
    col = 0;
    label = gtk_label_new("Radio Mic");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col++, row, 1, 1);
    //
    // Mic Boost, Mic In, and Line In can the handled mutually exclusive
    //
    btn = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Mic In");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Mic Boost");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Line In");
    int pos = 0;

    if (mic_linein) {
      pos = 2;
    } else if (mic_boost) {
      pos = 1;
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), pos);
    my_combo_attach(GTK_GRID(tx_grid), btn, col++, row, 1, 1);
    g_signal_connect(btn, "changed", G_CALLBACK(mic_in_cb), NULL);
    col++;
    label = gtk_label_new("LineIn Lvl (dB)");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    col++;
    btn = gtk_spin_button_new_with_range(-34.5, 12.0, 1.5);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(btn), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), linein_gain);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
    g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_LINEIN));
  }

  row++;
  col = 0;
  label = gtk_label_new("TX Filter Low");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  tx_spin_low = gtk_spin_button_new_with_range(0.0, 8000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_spin_low), (double)transmitter->default_filter_low);
  gtk_grid_attach(GTK_GRID(tx_grid), tx_spin_low, col, row, 1, 1);
  g_signal_connect(tx_spin_low, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_FILTER_LOW));
  gtk_widget_set_sensitive (tx_spin_low, NOT(transmitter->use_rx_filter));
  col++;
  btn = gtk_check_button_new_with_label("TX uses RX Filter");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->use_rx_filter);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_USE_RX_FILTER));
  col++;
  btn = gtk_check_button_new_with_label("Compression");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_widget_set_halign(btn, GTK_ALIGN_END);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->compressor);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_COMP_ENABLE));
  col++;
  btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->compressor_level);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col++, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_COMP));
  row++;
  col = 0;
  label = gtk_label_new("TX Filter High");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  tx_spin_high = gtk_spin_button_new_with_range(0.0, 8000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_spin_high), (double)transmitter->default_filter_high);
  gtk_grid_attach(GTK_GRID(tx_grid), tx_spin_high, col, row, 1, 1);
  g_signal_connect(tx_spin_high, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_FILTER_HIGH));
  gtk_widget_set_sensitive (tx_spin_high, NOT(transmitter->use_rx_filter));
  col++;
  btn = gtk_check_button_new_with_label("CTCSS Enable");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->ctcss_enabled);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_CTCSS_ENABLE));
  col++;
  label = gtk_label_new("CTCSS Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_combo_box_text_new();

  for (int i = 0; i < CTCSS_FREQUENCIES; i++) {
    char temp[32];
    snprintf(temp, sizeof(temp), "%0.1f", ctcss_frequencies[i]);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, temp);
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), transmitter->ctcss);
  my_combo_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(ctcss_frequency_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter High");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_high);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PAN_HIGH));
  col++;
  btn = gtk_check_button_new_with_label("FM PreEmp/ALC");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), !transmitter->pre_emphasize);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_FM_EMP));
  col++;
  label = gtk_label_new("AM Carrier Level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(0.0, 1.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->am_carrier_level);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_AM_CARRIER));
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Low");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(-400.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_low);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PAN_LOW));
  col++;
  btn = gtk_check_button_new_with_label("TX Audio Monitor");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->audiomonitor);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_AUDIO_MON));
  col++;
  label = gtk_label_new("Max Digi Drv");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(drive_min, drive_max, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), drive_digi_max);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_DIGI_DRIVE));
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Step");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(5.0, 25.0, 5.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_step);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PAN_STEP));
  col++;
  btn = gtk_check_button_new_with_label("Fill Panadapter");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->display_filled);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_DISPLAY_FILLED));
  col++;
  label = gtk_label_new("Frames Per Second");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->fps);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_FPS));
  //
  // CFC container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), cfc_container, 0, 2, 4, 1);
  GtkWidget *cfc_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(cfc_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(cfc_grid), 5);
  gtk_container_add(GTK_CONTAINER(cfc_container), cfc_grid);
  row = 0;
  btn = gtk_check_button_new_with_label("Use Continuous Frequency Compressor");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->cfc);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 0, row, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(CFC_ONOFF));
  btn = gtk_check_button_new_with_label("Use Post-Compression Equaliser");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->cfc_eq);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(CFC_EQ));
  row++;
  label = gtk_label_new("Add Freq-Indep. Compression");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 0, row, 2, 1);
  btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_lvl[0]);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCLVL));
  label = gtk_label_new("Add Freq-Indep. Gain");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 3, row, 2, 1);
  btn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_post[0]);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 5, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCPOST));
  row++;
  GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(line, -1, 3);
  gtk_grid_attach(GTK_GRID(cfc_grid), line, 0, row, 6, 1);
  // Frequency, Level, Post-Gain
  row++;
  label = gtk_label_new("Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 0, row, 1, 1);
  label = gtk_label_new("Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 3, row, 1, 1);
  label = gtk_label_new("CmprLevel");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 1, row, 1, 1);
  label = gtk_label_new("CmprLevel");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 4, row, 1, 1);
  label = gtk_label_new("PostGain");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 2, row, 1, 1);
  label = gtk_label_new("PostGain");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 5, row, 1, 1);

  for (int i = 1; i < 6; i++) {
    row++;
    btn = gtk_spin_button_new_with_range(10.0, 9990.0, 10.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_freq[i]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 0, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCFREQ + i));
    btn = gtk_spin_button_new_with_range(10.0, 9990.0, 10.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_freq[i + 5]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCFREQ + i + 5));
    btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_lvl[i]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 1, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCLVL + i));
    btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_lvl[i + 5]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 4, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCLVL + i + 5));
    btn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_post[i]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 2, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCPOST + i));
    btn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_post[i + 5]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 5, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCPOST + i + 5));
  }

  //
  // DEXP container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), dexp_container, 0, 2, 4, 1);
  GtkWidget *dexp_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(dexp_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(dexp_grid), 5);
  gtk_container_add(GTK_CONTAINER(dexp_container), dexp_grid);
  row = 0;
  btn = gtk_check_button_new_with_label("Use Downward Expander");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->dexp);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 0, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(DEXP_ONOFF));
  btn = gtk_check_button_new_with_label("Use Side Channel Filter");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->dexp_filter);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(DEXP_FILTER));
  row++;
  label = gtk_label_new("Expansion Factor (dB)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.00, 30.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_exp);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_EXP));
  label = gtk_label_new("Filter Low-Cut (Hz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 2, row, 1, 1);
  btn = gtk_spin_button_new_with_range(10.00, 1500.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_filter_low);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_FILTER_LOW));
  row++;
  label = gtk_label_new("Hysteresis Ratio");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.05, 0.95, 0.01);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_hyst);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_HYST));
  label = gtk_label_new("Filter High-Cut (Hz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 2, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1510.00, 5000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_filter_high);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_FILTER_HIGH));
  row++;
  label = gtk_label_new("Trigger Level (dB)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(-40.0, -10.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_trigger);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_TRIGGER));
  row++;
  label = gtk_label_new("Trigger Attack tau (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 250.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_tau);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_TAU));
  row++;
  label = gtk_label_new("Trigger Attack Time (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 250.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_attack);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_ATTACK));
  row++;
  label = gtk_label_new("Trigger Release Time (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_release);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_RELEASE));
  row++;
  label = gtk_label_new("Trigger Hold Time (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(10.0, 1500.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_hold);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_HOLD));
  //
  // Peaks container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), peaks_container, 0, 2, 4, 1);
  GtkWidget *peaks_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(peaks_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(peaks_grid), 5);
  gtk_container_add(GTK_CONTAINER(peaks_container), peaks_grid);
  col = 0;
  row = 0;
  GtkWidget *b_panadapter_peaks_on = gtk_check_button_new_with_label("Label Strongest Peaks");
  gtk_widget_set_name(b_panadapter_peaks_on, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_panadapter_peaks_on), transmitter->panadapter_peaks_on);
  gtk_widget_show(b_panadapter_peaks_on);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_panadapter_peaks_on, col, row, 1, 1);
  g_signal_connect(b_panadapter_peaks_on, "toggled", G_CALLBACK(tx_panadapter_peaks_on_cb), NULL);
  row++;
  GtkWidget *b_pan_peaks_in_passband = gtk_check_button_new_with_label("Label in Passband Only");
  gtk_widget_set_name(b_pan_peaks_in_passband, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_pan_peaks_in_passband),
                               transmitter->panadapter_peaks_in_passband_filled);
  gtk_widget_show(b_pan_peaks_in_passband);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_pan_peaks_in_passband, col, row, 1, 1);
  g_signal_connect(b_pan_peaks_in_passband, "toggled", G_CALLBACK(tx_panadapter_peaks_in_passband_filled_cb), NULL);
  GtkWidget *b_pan_hide_noise = gtk_check_button_new_with_label("No Labels Below Noise Floor");
  gtk_widget_set_name(b_pan_hide_noise, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_pan_hide_noise), transmitter->panadapter_hide_noise_filled);
  gtk_widget_show(b_pan_hide_noise);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_pan_hide_noise, col, ++row, 1, 1);
  g_signal_connect(b_pan_hide_noise, "toggled", G_CALLBACK(tx_panadapter_hide_noise_filled_cb), NULL);
  label = gtk_label_new("Number of Peaks to Label");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, ++row, 1, 1);
  col++;
  GtkWidget *panadapter_num_peaks_r = gtk_spin_button_new_with_range(1.0, 10.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_num_peaks_r), (double)transmitter->panadapter_num_peaks);
  gtk_widget_show(panadapter_num_peaks_r);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_num_peaks_r, col, row, 1, 1);
  g_signal_connect(panadapter_num_peaks_r, "value_changed", G_CALLBACK(tx_panadapter_num_peaks_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Ignore Adjacent Peaks");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_ignore_range_divider_r = gtk_spin_button_new_with_range(1.0, 150.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_ignore_range_divider_r),
                            (double)transmitter->panadapter_ignore_range_divider);
  gtk_widget_show(panadapter_ignore_range_divider_r);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_ignore_range_divider_r, col, row, 1, 1);
  g_signal_connect(panadapter_ignore_range_divider_r, "value_changed",
                   G_CALLBACK(tx_panadapter_ignore_range_divider_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Noise Floor Percentile");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_ignore_noise_percentile_r = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_ignore_noise_percentile_r),
                            (double)transmitter->panadapter_ignore_noise_percentile);
  gtk_widget_show(panadapter_ignore_noise_percentile_r);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_ignore_noise_percentile_r, col, row, 1, 1);
  g_signal_connect(panadapter_ignore_noise_percentile_r, "value_changed",
                   G_CALLBACK(tx_panadapter_ignore_noise_percentile_value_changed_cb), NULL);
  //
  // TUNE/SWR container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), tune_container, 0, 2, 4, 1);
  GtkWidget *swr_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(swr_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(swr_grid), 5);
  gtk_container_add(GTK_CONTAINER(tune_container), swr_grid);
  row = 0;
  col = 0;
  label = gtk_label_new("Tune Drive level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(swr_grid), label, col, row, 1, 1);
  col++;
  tunedrive_spin = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tunedrive_spin), (double)transmitter->tune_drive);
  gtk_grid_attach(GTK_GRID(swr_grid), tunedrive_spin, col, row, 1, 1);
  g_signal_connect(tunedrive_spin, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_TUNE_DRIVE));
  gtk_widget_set_sensitive (tunedrive_spin, NOT(transmitter->tune_use_drive));
  col++;
  btn = gtk_check_button_new_with_label("Tune use drive");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->tune_use_drive);
  gtk_grid_attach(GTK_GRID(swr_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_TUNE_USE_DRIVE));

  if (have_swr) {
    row++;
    col = 0;
    label = gtk_label_new("SWR alarm at");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(swr_grid), label, col, row, 1, 1);
    col++;
    btn = gtk_spin_button_new_with_range(1.0, 10.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->swr_alarm);
    gtk_grid_attach(GTK_GRID(swr_grid), btn, col, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_SWR_ALARM));
    col++;
    btn = gtk_check_button_new_with_label("SWR Protection");
    gtk_widget_set_name(btn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->swr_protection);
    gtk_grid_attach(GTK_GRID(swr_grid), btn, col, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_SWR_PROTECTION));
    row++;
    col = 0;
    label = gtk_label_new("Tune Sidetone Volume");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(swr_grid), label, col, row, 1, 1);
    col++;
    swrtune_volume_spin = gtk_spin_button_new_with_range(0.0, 0.5, 0.01);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(swrtune_volume_spin), transmitter->swrtune_volume);
    gtk_grid_attach(GTK_GRID(swr_grid), swrtune_volume_spin, col, row, 1, 1);
    g_signal_connect(swrtune_volume_spin, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_SWRTUNE_VOLUME));
    gtk_widget_set_sensitive (swrtune_volume_spin, SET(transmitter->swrtune));
    col++;
    btn = gtk_check_button_new_with_label("SWR-dependend Sidetone with TUNE");
    gtk_widget_set_name(btn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->swrtune);
    gtk_grid_attach(GTK_GRID(swr_grid), btn, col, row, 3, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_SWRTUNE));
  }

  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  //
  // Only show one of the TX, CFC, DEXP containers
  // This is the TX container upon first invocation of the TX menu,
  // but subsequent TX menu openings will show the container that
  // was active when leaving this menu before.
  //
  switch (which_container) {
  case TX_CONTAINER:
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(dexp_container);
    gtk_widget_hide(peaks_container);
    gtk_widget_hide(tune_container);
    break;

  case CFC_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(dexp_container);
    gtk_widget_hide(peaks_container);
    gtk_widget_hide(tune_container);
    break;

  case DEXP_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(peaks_container);
    gtk_widget_hide(tune_container);
    break;

  case PEAKS_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(dexp_container);
    gtk_widget_hide(tune_container);
    break;

  case TUNE_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(dexp_container);
    gtk_widget_hide(peaks_container);
    break;
  }
}
