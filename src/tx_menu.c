/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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
#include "new_menu.h"
#include "radio.h"
#include "sliders.h"
#include "transmitter.h"
#include "ext.h"
#include "filter.h"
#include "mode.h"
#include "vfo.h"
#include "new_protocol.h"
#include "message.h"
#include "mystring.h"

static GtkWidget *dialog = NULL;
static GtkWidget *input;
static GtkWidget *tx_spin_low;
static GtkWidget *tx_spin_high;

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

static void linein_value_changed(GtkWidget *widget, gpointer data) {
  linein_gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void frames_per_second_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  tx_set_displaying(transmitter, transmitter->displaying);
}

static void filled_cb(GtkWidget *widget, gpointer data) {
  transmitter->display_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void comp_enable_cb(GtkWidget *widget, gpointer data) {
  transmitter_set_compressor(transmitter, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
  mode_settings[get_tx_mode()].compressor = transmitter->compressor;
  g_idle_add(ext_vfo_update, NULL);
}

static void comp_cb(GtkWidget *widget, gpointer data) {
  transmitter_set_compressor_level(transmitter, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget)));
  mode_settings[get_tx_mode()].compressor_level = transmitter->compressor_level;
  g_idle_add(ext_vfo_update, NULL);
}

static void tx_spin_low_cb (GtkWidget *widget, gpointer data) {
  tx_filter_low = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  tx_set_filter(transmitter);
}

static void tx_spin_high_cb (GtkWidget *widget, gpointer data) {
  tx_filter_high = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  tx_set_filter(transmitter);
}

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

  schedule_transmit_specific();
}

static void panadapter_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_high = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_low = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_step_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_step = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void am_carrier_level_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->am_carrier_level = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  transmitter_set_am_carrier_level(transmitter);
}

static void ctcss_cb (GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  transmitter_set_ctcss(transmitter, state, transmitter->ctcss);
  g_idle_add(ext_vfo_update, NULL);
}

static void ctcss_frequency_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  transmitter_set_ctcss(transmitter, transmitter->ctcss_enabled, i);
  g_idle_add(ext_vfo_update, NULL);
}

static void tune_use_drive_cb (GtkWidget *widget, gpointer data) {
  transmitter->tune_use_drive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void tune_drive_cb (GtkWidget *widget, gpointer data) {
  transmitter->tune_drive = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void digi_drive_cb (GtkWidget *widget, gpointer data) {
  drive_digi_max = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (transmitter->drive > drive_digi_max + 0.5) {
    set_drive(drive_digi_max);
  }
}

static void swr_protection_cb (GtkWidget *widget, gpointer data) {
  transmitter->swr_protection = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void swr_alarm_cb (GtkWidget *widget, gpointer data) {
  transmitter->swr_alarm = (double)gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void use_rx_filter_cb(GtkWidget *widget, gpointer data) {
  transmitter->use_rx_filter = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  tx_set_filter(transmitter);

  if (transmitter->use_rx_filter) {
    gtk_widget_set_sensitive (tx_spin_low, FALSE);
    gtk_widget_set_sensitive (tx_spin_high, FALSE);
  } else {
    gtk_widget_set_sensitive (tx_spin_low, TRUE);
    gtk_widget_set_sensitive (tx_spin_high, TRUE);
  }
}

static void local_microphone_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
    if (audio_open_input() == 0) {
      transmitter->local_microphone = 1;
    } else {
      transmitter->local_microphone = 0;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
    }
  } else {
    if (transmitter->local_microphone) {
      transmitter->local_microphone = 0;
      audio_close_input();
    }
  }
}

static void local_input_changed_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  t_print("local_input_changed_cb: %d %s\n", i, input_devices[i].name);

  if (transmitter->local_microphone) {
    audio_close_input();
  }

  STRLCPY(transmitter->microphone_name, input_devices[i].name, sizeof(transmitter->microphone_name));

  if (transmitter->local_microphone) {
    if (audio_open_input() < 0) {
      transmitter->local_microphone = 0;
    }
  }
}

static gboolean emp_cb (GtkWidget *widget, gpointer data) {
  pre_emphasize = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  tx_set_pre_emphasize(transmitter, pre_emphasize);
  return FALSE;
}

void tx_menu(GtkWidget *parent) {
  int i;
  char temp[32];
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
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  int row = 0;
  int col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);

  if (n_input_devices > 0) {
    row++;
    col = 0;
    GtkWidget *local_microphone_b = gtk_check_button_new_with_label("Local Microphone");
    gtk_widget_set_halign(local_microphone_b, GTK_ALIGN_END);
    gtk_widget_set_name(local_microphone_b, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (local_microphone_b), transmitter->local_microphone);
    gtk_grid_attach(GTK_GRID(grid), local_microphone_b, col++, row, 1, 1);
    g_signal_connect(local_microphone_b, "toggled", G_CALLBACK(local_microphone_cb), NULL);
    input = gtk_combo_box_text_new();

    for (i = 0; i < n_input_devices; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(input), NULL, input_devices[i].description);

      if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(input), i);
      }
    }

    // If the combo box shows no device, take the first one
    // AND set the mic.name to that device name.
    // This situation occurs if the local microphone device in the props
    // file is no longer present
    i = gtk_combo_box_get_active(GTK_COMBO_BOX(input));

    if (i < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(input), 0);
      STRLCPY(transmitter->microphone_name, input_devices[0].name, sizeof(transmitter->microphone_name));
    }

    my_combo_attach(GTK_GRID(grid), input, col, row, 4, 1);
    g_signal_connect(input, "changed", G_CALLBACK(local_input_changed_cb), NULL);
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    row++;
    col = 0;
    GtkWidget *lbl = gtk_label_new("Radio Mic");
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, col++, row, 1, 1);
    //
    // Mic Boost, Mic In, and Line In can the handled mutually exclusive
    //
    GtkWidget *mic_in_b = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_in_b), NULL, "Mic In");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_in_b), NULL, "Mic Boost");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_in_b), NULL, "Line In");
    int pos = 0;

    if (mic_linein) {
      pos = 2;
    } else if (mic_boost) {
      pos = 1;
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(mic_in_b), pos);
    my_combo_attach(GTK_GRID(grid), mic_in_b, col++, row, 1, 1);
    g_signal_connect(mic_in_b, "changed", G_CALLBACK(mic_in_cb), NULL);
    col++;
    label = gtk_label_new("LineIn Lvl (dB)");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
    col++;
    GtkWidget *linein_spin = gtk_spin_button_new_with_range(-34.0, 12.5, 1.5);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(linein_spin), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(linein_spin), linein_gain);
    gtk_grid_attach(GTK_GRID(grid), linein_spin, col, row, 1, 1);
    g_signal_connect(G_OBJECT(linein_spin), "value_changed", G_CALLBACK(linein_value_changed), NULL);
  }

  row++;
  col = 0;
  label = gtk_label_new("TX Filter Low");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  tx_spin_low = gtk_spin_button_new_with_range(0.0, 8000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_spin_low), (double)tx_filter_low);
  gtk_grid_attach(GTK_GRID(grid), tx_spin_low, col, row, 1, 1);
  g_signal_connect(tx_spin_low, "value-changed", G_CALLBACK(tx_spin_low_cb), NULL);
  col++;
  GtkWidget *use_rx_filter_b = gtk_check_button_new_with_label("TX uses RX Filter");
  gtk_widget_set_name(use_rx_filter_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (use_rx_filter_b), transmitter->use_rx_filter);
  gtk_grid_attach(GTK_GRID(grid), use_rx_filter_b, col, row, 2, 1);
  g_signal_connect(use_rx_filter_b, "toggled", G_CALLBACK(use_rx_filter_cb), NULL);
  col++;
  GtkWidget *comp_enable = gtk_check_button_new_with_label("Compression");
  gtk_widget_set_name(comp_enable, "boldlabel");
  gtk_widget_set_halign(comp_enable, GTK_ALIGN_END);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (comp_enable), transmitter->compressor);
  gtk_grid_attach(GTK_GRID(grid), comp_enable, col, row, 1, 1);
  g_signal_connect(comp_enable, "toggled", G_CALLBACK(comp_enable_cb), NULL);
  col++;
  GtkWidget *comp = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(comp), (double)transmitter->compressor_level);
  gtk_grid_attach(GTK_GRID(grid), comp, col++, row, 1, 1);
  g_signal_connect(comp, "value-changed", G_CALLBACK(comp_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("TX Filter High");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  tx_spin_high = gtk_spin_button_new_with_range(0.0, 8000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_spin_high), (double)tx_filter_high);
  gtk_grid_attach(GTK_GRID(grid), tx_spin_high, col, row, 1, 1);
  g_signal_connect(tx_spin_high, "value-changed", G_CALLBACK(tx_spin_high_cb), NULL);
  col++;

  if (transmitter->use_rx_filter) {
    gtk_widget_set_sensitive (tx_spin_low, FALSE);
    gtk_widget_set_sensitive (tx_spin_high, FALSE);
  }

  GtkWidget *tune_use_drive_b = gtk_check_button_new_with_label("Tune use drive");
  gtk_widget_set_name(tune_use_drive_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tune_use_drive_b), transmitter->tune_use_drive);
  gtk_grid_attach(GTK_GRID(grid), tune_use_drive_b, col, row, 1, 1);
  g_signal_connect(tune_use_drive_b, "toggled", G_CALLBACK(tune_use_drive_cb), NULL);
  col++;
  label = gtk_label_new("Tune Drive level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *tune_drive = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tune_drive), (double)transmitter->tune_drive);
  gtk_grid_attach(GTK_GRID(grid), tune_drive, col, row, 1, 1);
  g_signal_connect(tune_drive, "value-changed", G_CALLBACK(tune_drive_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter High");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_high_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_high_r), (double)transmitter->panadapter_high);
  gtk_grid_attach(GTK_GRID(grid), panadapter_high_r, col, row, 1, 1);
  g_signal_connect(panadapter_high_r, "value_changed", G_CALLBACK(panadapter_high_value_changed_cb), NULL);
  col++;
  GtkWidget *swr_protection_b = gtk_check_button_new_with_label("SWR Protection");
  gtk_widget_set_name(swr_protection_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (swr_protection_b), transmitter->swr_protection);
  gtk_grid_attach(GTK_GRID(grid), swr_protection_b, col, row, 1, 1);
  g_signal_connect(swr_protection_b, "toggled", G_CALLBACK(swr_protection_cb), NULL);
  col++;
  GtkWidget *swr_alarm_label = gtk_label_new("SWR alarm at");
  gtk_widget_set_name(swr_alarm_label, "boldlabel");
  gtk_widget_set_halign(swr_alarm_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), swr_alarm_label, col, row, 1, 1);
  col++;
  GtkWidget *swr_alarm = gtk_spin_button_new_with_range(1.0, 10.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(swr_alarm), (double)transmitter->swr_alarm);
  gtk_grid_attach(GTK_GRID(grid), swr_alarm, col, row, 1, 1);
  g_signal_connect(swr_alarm, "value-changed", G_CALLBACK(swr_alarm_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Low");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_low_r = gtk_spin_button_new_with_range(-400.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_low_r), (double)transmitter->panadapter_low);
  gtk_grid_attach(GTK_GRID(grid), panadapter_low_r, col, row, 1, 1);
  g_signal_connect(panadapter_low_r, "value_changed", G_CALLBACK(panadapter_low_value_changed_cb), NULL);
  col++;
  GtkWidget *ctcss_b = gtk_check_button_new_with_label("CTCSS Enable");
  gtk_widget_set_name(ctcss_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ctcss_b), transmitter->ctcss_enabled);
  gtk_grid_attach(GTK_GRID(grid), ctcss_b, col, row, 1, 1);
  g_signal_connect(ctcss_b, "toggled", G_CALLBACK(ctcss_cb), NULL);
  col++;
  label = gtk_label_new("CTCSS Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *ctcss_frequency_b = gtk_combo_box_text_new();

  for (i = 0; i < CTCSS_FREQUENCIES; i++) {
    snprintf(temp, 32, "%0.1f", ctcss_frequencies[i]);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ctcss_frequency_b), NULL, temp);
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(ctcss_frequency_b), transmitter->ctcss);
  my_combo_attach(GTK_GRID(grid), ctcss_frequency_b, col, row, 1, 1);
  g_signal_connect(ctcss_frequency_b, "changed", G_CALLBACK(ctcss_frequency_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Step");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_step_r = gtk_spin_button_new_with_range(-400.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_step_r), (double)transmitter->panadapter_step);
  gtk_grid_attach(GTK_GRID(grid), panadapter_step_r, col, row, 1, 1);
  g_signal_connect(panadapter_step_r, "value_changed", G_CALLBACK(panadapter_step_value_changed_cb), NULL);
  col++;
  GtkWidget *emp_b = gtk_check_button_new_with_label("FM PreEmp/ALC");
  gtk_widget_set_name(emp_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (emp_b), !pre_emphasize);
  gtk_grid_attach(GTK_GRID(grid), emp_b, col, row, 1, 1);
  g_signal_connect(emp_b, "toggled", G_CALLBACK(emp_cb), NULL);
  col++;
  label = gtk_label_new("AM Carrier Level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *am_carrier_level = gtk_spin_button_new_with_range(0.0, 1.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(am_carrier_level), (double)transmitter->am_carrier_level);
  gtk_grid_attach(GTK_GRID(grid), am_carrier_level, col, row, 1, 1);
  g_signal_connect(am_carrier_level, "value_changed", G_CALLBACK(am_carrier_level_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Frames Per Second");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *fps_spin = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(fps_spin), (double)transmitter->fps);
  gtk_grid_attach(GTK_GRID(grid), fps_spin, col, row, 1, 1);
  g_signal_connect(fps_spin, "value-changed", G_CALLBACK(frames_per_second_value_changed_cb), NULL);
  col++;
  GtkWidget *filled_b = gtk_check_button_new_with_label("Fill Panadapter");
  gtk_widget_set_name(filled_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (filled_b), transmitter->display_filled);
  gtk_grid_attach(GTK_GRID(grid), filled_b, col, row, 1, 1);
  g_signal_connect(filled_b, "toggled", G_CALLBACK(filled_cb), NULL);
  col++;
  label = gtk_label_new("Max Digi Drv");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *digi_drive_b = gtk_spin_button_new_with_range(1.0, drive_max, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(digi_drive_b), drive_digi_max);
  gtk_grid_attach(GTK_GRID(grid), digi_drive_b, col, row, 1, 1);
  g_signal_connect(digi_drive_b, "value-changed", G_CALLBACK(digi_drive_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
