/* Copyright (C)
*  2015 - John Melton, G0ORX/N6LYT
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

#include "client_server.h"
#include "main.h"
#include "new_menu.h"
#include "radio.h"

enum _containers {
  RX1_CONTAINER = 0,
  RX2_CONTAINER,
  TX_CONTAINER
};

static int which_container = RX1_CONTAINER;


static GtkWidget *rx1_container;
static GtkWidget *rx2_container;
static GtkWidget *tx_container;

static GtkWidget *dialog = NULL;

//
// This guards against changing the active receiver while the
// menu is open
//
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

static void sel_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle radio button in the top row, this selects
  // which sub-menu is active
  //
  int c = GPOINTER_TO_INT(data);
  GtkWidget *my_container;

  switch (c) {
  case RX1_CONTAINER:
    my_container = rx1_container;
    break;

  case RX2_CONTAINER:
    my_container = rx2_container;
    break;

  case TX_CONTAINER:
    my_container = tx_container;
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

static void detector_cb(GtkToggleButton *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    myrx->display_detector_mode = DET_PEAK;
    break;

  case 1:
    myrx->display_detector_mode = DET_ROSENFELL;
    break;

  case 2:
    myrx->display_detector_mode = DET_AVERAGE;
    break;

  case 3:
    myrx->display_detector_mode = DET_SAMPLEHOLD;
    break;
  }

  if (radio_is_remote) {
    send_display(cl_sock_tcp, myrx->id);
  } else {
    rx_set_detector(myrx);
  }
}

static void average_cb(GtkToggleButton *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    myrx->display_average_mode = AVG_NONE;
    break;

  case 1:
    myrx->display_average_mode = AVG_RECURSIVE;
    break;

  case 2:
    myrx->display_average_mode = AVG_TIMEWINDOW;
    break;

  case 3:
    myrx->display_average_mode = AVG_LOGRECURSIVE;
    break;
  }

  if (radio_is_remote) {
    send_display(cl_sock_tcp, myrx->id);
  } else {
    rx_set_average(myrx);
  }
}

static void panadapter_peaks_on_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->panadapter_peaks_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void time_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->display_average_time = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_display(cl_sock_tcp, myrx->id);
  } else {
    rx_set_average(myrx);
  }
}

static void filled_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->display_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_filled_cb(GtkWidget *widget, gpointer data) {
  transmitter->display_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void panadapter_hide_noise_filled_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->panadapter_hide_noise_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void panadapter_peaks_in_passband_filled_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->panadapter_peaks_in_passband_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void gradient_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->display_gradient = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void frames_per_second_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  int old = myrx->display_panadapter;
  myrx->display_panadapter = (myrx->fps > 1);

  if (old != myrx->display_panadapter) { radio_reconfigure(); }

  if (radio_is_remote) {
    send_rxfps(cl_sock_tcp, myrx->id, myrx->fps);
  } else {
    rx_set_framerate(myrx);
  }
}

static void tx_frames_per_second_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_txfps(cl_sock_tcp, transmitter->fps);
  } else {
    tx_set_framerate(transmitter);
  }
}

static void panadapter_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  const RECEIVER *myrx = (RECEIVER *)data;
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_panhigh(myrx->id, value);
}

static void tx_panadapter_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  transmitter->panadapter_high = value;
}

static void panadapter_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  const RECEIVER *myrx = (RECEIVER *)data;
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_panlow(myrx->id, value);
}

static void tx_panadapter_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  transmitter->panadapter_low = value;
}

static void panadapter_step_value_changed_cb(GtkWidget *widget, gpointer data) {
  const RECEIVER *myrx = (RECEIVER *)data;
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_panstep(myrx->id, value);
}

static void tx_panadapter_step_value_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  transmitter->panadapter_step = value;
}

static void panadapter_num_peaks_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->panadapter_num_peaks = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_ignore_range_divider_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->panadapter_ignore_range_divider = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_ignore_noise_percentile_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->panadapter_ignore_noise_percentile = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->waterfall_high = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->waterfall_low = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_automatic_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  myrx->waterfall_automatic = val;
}

static void display_waterfall_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->display_waterfall = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_reconfigure();
}

static void waterfall_percent_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *myrx = (RECEIVER *)data;
  myrx->waterfall_percent = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_reconfigure();
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

static void display_warnings_cb(GtkWidget *widget, gpointer data) {
  display_warnings = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_display_pacurr_cb(GtkWidget *widget, gpointer data) {
  display_pacurr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

void display_menu(GtkWidget *parent) {
  GtkWidget *label;
  GtkWidget *btn;
  GtkWidget *mbtn; // main button for radio buttons
  char title[64];
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  snprintf(title, sizeof(title), "piHPSDR - Display");
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_container_add(GTK_CONTAINER(content), grid);
  int row = 0;
  int col = 0;
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect(btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  //
  // Must init the containers here since setting the buttons emits
  // a signal leading to show/hide commands
  //
  rx1_container = gtk_fixed_new();
  rx2_container = gtk_fixed_new();
  tx_container = gtk_fixed_new();
  GtkWidget *rx1_grid = gtk_grid_new();
  GtkWidget *rx2_grid = gtk_grid_new();
  GtkWidget *tx_grid = gtk_grid_new();
  mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "RX1 Settings");
  col = 3;

  if (can_transmit) {
    btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "TX settings");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), 0);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(TX_CONTAINER));
    gtk_grid_attach(GTK_GRID(grid), tx_container, 0, 1, 4, 1);
    gtk_grid_set_column_spacing(GTK_GRID(tx_grid), 10);
    gtk_grid_set_row_homogeneous(GTK_GRID(tx_grid), TRUE);
    gtk_container_add(GTK_CONTAINER(tx_container), tx_grid);
    col--;
  }

  if (receivers > 1) {
    btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "RX2 settings");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), 0);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(RX2_CONTAINER));
    gtk_grid_attach(GTK_GRID(grid), rx2_container, 0, 1, 4, 1);
    gtk_grid_set_column_spacing(GTK_GRID(rx2_grid), 10);
    gtk_grid_set_row_homogeneous(GTK_GRID(rx2_grid), TRUE);
    gtk_container_add(GTK_CONTAINER(rx2_container), rx2_grid);
    col--;
  }

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), 1);
  gtk_grid_attach(GTK_GRID(grid), mbtn, col, row, 1, 1);
  g_signal_connect(mbtn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(RX1_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), rx1_container, 0, 1, 4, 1);
  gtk_grid_set_column_spacing(GTK_GRID(rx1_grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(rx1_grid), TRUE);
  gtk_container_add(GTK_CONTAINER(rx1_container), rx1_grid);

  for (int id = 0; id < receivers; id++) {
    RECEIVER *myrx = receiver[id];
    GtkWidget *mygrid = (id == 0) ? rx1_grid : rx2_grid;
    row = 0;
    col = 0;
    label = gtk_label_new("Frames/sec");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 64.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->fps);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(frames_per_second_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("Panadapter High");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->panadapter_high);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(panadapter_high_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("Panadapter Low");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->panadapter_low);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(panadapter_low_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("Panadapter Step");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->panadapter_step);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(panadapter_step_value_changed_cb), myrx);
    row++;
    btn = gtk_check_button_new_with_label("Pan Filled");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->display_filled);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(filled_cb), myrx);
    btn = gtk_check_button_new_with_label("Gradient");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->display_gradient);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(gradient_cb), myrx);
    row++;
    label = gtk_label_new("Waterfall High");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->waterfall_high);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(waterfall_high_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("Waterfall Low");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->waterfall_low);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(waterfall_low_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("WF Height (%)");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    GtkWidget *waterfall_percent = gtk_spin_button_new_with_range(20.0, 80.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(waterfall_percent), (double) myrx->waterfall_percent);
    gtk_grid_attach(GTK_GRID(mygrid), waterfall_percent, col + 1, row, 1, 1);
    g_signal_connect(waterfall_percent, "value-changed", G_CALLBACK(waterfall_percent_cb), myrx);
    row++;
    btn = gtk_check_button_new_with_label("Display WF");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->display_waterfall);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(display_waterfall_cb), myrx);
    btn = gtk_check_button_new_with_label("WF automatic");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->waterfall_automatic);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    row++;
    g_signal_connect(btn, "toggled", G_CALLBACK(waterfall_automatic_cb), myrx);

    if (!radio_is_remote && id == 0) {
      btn = gtk_check_button_new_with_label("Display Warnings");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), display_warnings);
      gtk_grid_attach(GTK_GRID(mygrid), btn, col, row, 2, 1);
      g_signal_connect(btn, "toggled", G_CALLBACK(display_warnings_cb), NULL);
    }

    col = 2;
    row = 0;
    label = gtk_label_new("Detector");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    GtkWidget *detector_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Peak");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Rosenfell");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Average");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Sample");

    switch (myrx->display_detector_mode) {
    case DET_PEAK:
      gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 0);
      break;

    case DET_ROSENFELL:
      gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 1);
      break;

    case DET_AVERAGE:
      gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 2);
      break;

    case DET_SAMPLEHOLD:
      gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 3);
      break;
    }

    my_combo_attach(GTK_GRID(mygrid), detector_combo, col + 1, row, 1, 1);
    g_signal_connect(detector_combo, "changed", G_CALLBACK(detector_cb), myrx);
    row++;
    label = gtk_label_new("Averaging: ");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    GtkWidget *average_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "None");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "Recursive");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "Time Window");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "Log Recursive");

    switch (myrx->display_average_mode) {
    case AVG_NONE:
      gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 0);
      break;

    case AVG_RECURSIVE:
      gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 1);
      break;

    case AVG_TIMEWINDOW:
      gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 2);
      break;

    case AVG_LOGRECURSIVE:
      gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 3);
      break;
    }

    my_combo_attach(GTK_GRID(mygrid), average_combo, col + 1, row, 1, 1);
    g_signal_connect(average_combo, "changed", G_CALLBACK(average_cb), myrx);
    row++;
    label = gtk_label_new("Av. Time (ms)");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    GtkWidget *time_r = gtk_spin_button_new_with_range(1.0, 9999.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_r), (double)myrx->display_average_time);
    gtk_grid_attach(GTK_GRID(mygrid), time_r, col + 1, row, 1, 1);
    g_signal_connect(time_r, "value_changed", G_CALLBACK(time_value_changed_cb), myrx);
    row++;
    row++;
    btn = gtk_check_button_new_with_label("Label Strongest Peaks");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), myrx->panadapter_peaks_on);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(panadapter_peaks_on_cb), myrx);
    row++;
    btn = gtk_check_button_new_with_label("Label in Passband Only");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), myrx->panadapter_peaks_in_passband_filled);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(panadapter_peaks_in_passband_filled_cb), myrx);
    row++;
    btn = gtk_check_button_new_with_label("No Labels Below Noise Floor");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), myrx->panadapter_hide_noise_filled);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(panadapter_hide_noise_filled_cb), myrx);
    row++;
    label = gtk_label_new("Number of Peaks");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 10.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->panadapter_num_peaks);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(panadapter_num_peaks_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("Ignore Adjacent");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 150.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->panadapter_ignore_range_divider);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(panadapter_ignore_range_divider_value_changed_cb), myrx);
    row++;
    label = gtk_label_new("Floor Percentile");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)myrx->panadapter_ignore_noise_percentile);
    gtk_grid_attach(GTK_GRID(mygrid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(panadapter_ignore_noise_percentile_value_changed_cb), myrx);
  }

  //
  // Since the RECEIVER and TRANSMITTER data structures are different, we have to repeat code
  // since the presets and callbacks are different
  //
    if (can_transmit) {
    row = 0;
    col = 0;
    label = gtk_label_new("Frames/sec");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 64.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->fps);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_frames_per_second_value_changed_cb), NULL);
    row++;
    label = gtk_label_new("Panadapter High");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(-50.0, 10.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_high);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_panadapter_high_value_changed_cb), NULL);
    row++;
    label = gtk_label_new("Panadapter Low");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(-120.0, -40.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_low);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_panadapter_low_value_changed_cb), NULL);
    row++;
    label = gtk_label_new("Panadapter Step");
    gtk_widget_set_name (label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(5.0, 20.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_step);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_panadapter_step_value_changed_cb), NULL);
    row++;
    btn = gtk_check_button_new_with_label("Pan Filled");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->display_filled);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(tx_filled_cb), NULL);

    if (!radio_is_remote) {
      row += 5;
      btn = gtk_check_button_new_with_label("Display PA current");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), display_pacurr);
      gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 2, 1);
      g_signal_connect(btn, "toggled", G_CALLBACK(tx_display_pacurr_cb), NULL);
    }

    row = 4;
    col = 2;
    btn = gtk_check_button_new_with_label("Label Strongest Peaks");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), transmitter->panadapter_peaks_on);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(tx_panadapter_peaks_on_cb), NULL);
    row++;
    btn = gtk_check_button_new_with_label("Label in Passband Only");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), transmitter->panadapter_peaks_in_passband_filled);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(tx_panadapter_peaks_in_passband_filled_cb), NULL);
    row++;
    btn = gtk_check_button_new_with_label("No Labels Below Noise Floor");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), transmitter->panadapter_hide_noise_filled);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(tx_panadapter_hide_noise_filled_cb), NULL);
    row++;
    label = gtk_label_new("Number of Peaks");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 10.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_num_peaks);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_panadapter_num_peaks_value_changed_cb), NULL);
    row++;
    label = gtk_label_new("Ignore Adjacent");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 150.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_ignore_range_divider);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_panadapter_ignore_range_divider_value_changed_cb), NULL);
    row++;
    label = gtk_label_new("Floor Percentile");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    btn = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_ignore_noise_percentile);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col + 1, row, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(tx_panadapter_ignore_noise_percentile_value_changed_cb), NULL);
  }
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  gtk_widget_hide(rx2_container);
  gtk_widget_hide(tx_container);
}

