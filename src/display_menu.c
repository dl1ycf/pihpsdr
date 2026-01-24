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

#include "client_server.h"
#include "main.h"
#include "new_menu.h"
#include "radio.h"

enum _containers {
  GENERAL_CONTAINER = 1,
  PEAKS_CONTAINER
};

static int which_container = GENERAL_CONTAINER;


static GtkWidget *general_container;
static GtkWidget *peaks_container;

static GtkWidget *dialog = NULL;
static GtkWidget *general_container;
static GtkWidget *peaks_container;

//
// This guards against changing the active receiver while the
// menu is open
//
static RECEIVER *myrx;

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
  case GENERAL_CONTAINER:
    my_container = general_container;
    break;

  case PEAKS_CONTAINER:
    my_container = peaks_container;
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
  myrx->panadapter_peaks_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void time_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->display_average_time = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_display(cl_sock_tcp, myrx->id);
  } else {
    rx_set_average(myrx);
  }
}

static void filled_cb(GtkWidget *widget, gpointer data) {
  myrx->display_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void panadapter_hide_noise_filled_cb(GtkWidget *widget, gpointer data) {
  myrx->panadapter_hide_noise_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void panadapter_peaks_in_passband_filled_cb(GtkWidget *widget, gpointer data) {
  myrx->panadapter_peaks_in_passband_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void gradient_cb(GtkWidget *widget, gpointer data) {
  myrx->display_gradient = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void frames_per_second_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_rxfps(cl_sock_tcp, myrx->id, myrx->fps);
  } else {
    rx_set_framerate(myrx);
  }
}

static void panadapter_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_panhigh(myrx->id, value);
}

static void panadapter_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_panlow(myrx->id, value);
}

static void panadapter_step_value_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_set_panstep(myrx->id, value);
}

static void panadapter_num_peaks_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->panadapter_num_peaks = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_ignore_range_divider_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->panadapter_ignore_range_divider = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_ignore_noise_percentile_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->panadapter_ignore_noise_percentile = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->waterfall_high = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  myrx->waterfall_low = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_automatic_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  myrx->waterfall_automatic = val;
}

static void display_waterfall_cb(GtkWidget *widget, gpointer data) {
  myrx->display_waterfall = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_reconfigure();
}

static void display_panadapter_cb(GtkWidget *widget, gpointer data) {
  myrx->display_panadapter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_reconfigure();
}

static void waterfall_percent_cb(GtkWidget *widget, gpointer data) {
  myrx->waterfall_percent = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  radio_reconfigure();
}

//
// Some symbolic constants used in callbacks
//


void display_menu(GtkWidget *parent) {
  GtkWidget *label;
  char title[64];
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  //
  // Store the active receiver in myrx, so it is kept constant while the
  // menu is open, even if the active receiver changes later
  //
  myrx = active_receiver;
  snprintf(title, sizeof(title), "piHPSDR - Display RX%d", myrx->id + 1);
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
  GtkWidget *btn;
  GtkWidget *mbtn; // main button for radio buttons
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect(btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  //
  // Must init the containers here since setting the buttons emits
  // a signal leading to show/hide commands
  //
  general_container = gtk_fixed_new();
  peaks_container = gtk_fixed_new();
  col++;
  mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "General Settings");
  gtk_widget_set_name(mbtn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), (which_container == GENERAL_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), mbtn, col, row, 2, 1);
  g_signal_connect(mbtn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(GENERAL_CONTAINER));
  col++;
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Peak Labels");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == PEAKS_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(PEAKS_CONTAINER));
  //
  // General container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), general_container, 0, 1, 4, 1);
  GtkWidget *general_grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(general_grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(general_grid), TRUE);
  gtk_container_add(GTK_CONTAINER(general_container), general_grid);
  row = 0;
  col = 0;
  label = gtk_label_new("Frames Per Second");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *frames_per_second_r = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(frames_per_second_r), (double)myrx->fps);
  gtk_grid_attach(GTK_GRID(general_grid), frames_per_second_r, col, row, 1, 1);
  g_signal_connect(frames_per_second_r, "value_changed", G_CALLBACK(frames_per_second_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter High");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_high_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_high_r), (double)myrx->panadapter_high);
  gtk_grid_attach(GTK_GRID(general_grid), panadapter_high_r, col, row, 1, 1);
  g_signal_connect(panadapter_high_r, "value_changed", G_CALLBACK(panadapter_high_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Low");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_low_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_low_r), (double)myrx->panadapter_low);
  gtk_grid_attach(GTK_GRID(general_grid), panadapter_low_r, col, row, 1, 1);
  g_signal_connect(panadapter_low_r, "value_changed", G_CALLBACK(panadapter_low_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Step");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_step_r = gtk_spin_button_new_with_range(1.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_step_r), (double)myrx->panadapter_step);
  gtk_grid_attach(GTK_GRID(general_grid), panadapter_step_r, col, row, 1, 1);
  g_signal_connect(panadapter_step_r, "value_changed", G_CALLBACK(panadapter_step_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Waterfall High");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *waterfall_high_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(waterfall_high_r), (double)myrx->waterfall_high);
  gtk_grid_attach(GTK_GRID(general_grid), waterfall_high_r, col, row, 1, 1);
  g_signal_connect(waterfall_high_r, "value_changed", G_CALLBACK(waterfall_high_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Waterfall Low");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *waterfall_low_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(waterfall_low_r), (double)myrx->waterfall_low);
  gtk_grid_attach(GTK_GRID(general_grid), waterfall_low_r, col, row, 1, 1);
  g_signal_connect(waterfall_low_r, "value_changed", G_CALLBACK(waterfall_low_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Waterfall Automatic");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *waterfall_automatic_b = gtk_check_button_new();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (waterfall_automatic_b), myrx->waterfall_automatic);
  gtk_grid_attach(GTK_GRID(general_grid), waterfall_automatic_b, col, row, 1, 1);
  g_signal_connect(waterfall_automatic_b, "toggled", G_CALLBACK(waterfall_automatic_cb), NULL);
  col++;
  label = gtk_label_new("Waterfall Height (%)");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *waterfall_percent = gtk_spin_button_new_with_range(20.0, 80.0, 5.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(waterfall_percent), (double) myrx->waterfall_percent);
  gtk_grid_attach(GTK_GRID(general_grid), waterfall_percent, col, row, 1, 1);
  g_signal_connect(waterfall_percent, "value-changed", G_CALLBACK(waterfall_percent_cb), NULL);
  col = 2;
  row = 1;
  label = gtk_label_new("Detector");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
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

  my_combo_attach(GTK_GRID(general_grid), detector_combo, col + 1, row, 1, 1);
  g_signal_connect(detector_combo, "changed", G_CALLBACK(detector_cb), NULL);
  row++;
  label = gtk_label_new("Averaging: ");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
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

  my_combo_attach(GTK_GRID(general_grid), average_combo, col + 1, row, 1, 1);
  g_signal_connect(average_combo, "changed", G_CALLBACK(average_cb), NULL);
  row++;
  label = gtk_label_new("Av. Time (ms)");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), label, col, row, 1, 1);
  GtkWidget *time_r = gtk_spin_button_new_with_range(1.0, 9999.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_r), (double)myrx->display_average_time);
  gtk_grid_attach(GTK_GRID(general_grid), time_r, col + 1, row, 1, 1);
  g_signal_connect(time_r, "value_changed", G_CALLBACK(time_value_changed_cb), NULL);
  row++;
  GtkWidget *filled_b = gtk_check_button_new_with_label("Fill Panadapter");
  gtk_widget_set_name (filled_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (filled_b), myrx->display_filled);
  gtk_grid_attach(GTK_GRID(general_grid), filled_b, col, row, 1, 1);
  g_signal_connect(filled_b, "toggled", G_CALLBACK(filled_cb), NULL);
  GtkWidget *gradient_b = gtk_check_button_new_with_label("Gradient");
  gtk_widget_set_name (gradient_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gradient_b), myrx->display_gradient);
  gtk_grid_attach(GTK_GRID(general_grid), gradient_b, col + 1, row, 1, 1);
  g_signal_connect(gradient_b, "toggled", G_CALLBACK(gradient_cb), NULL);
  row++;
  GtkWidget *b_display_panadapter = gtk_check_button_new_with_label("Display Panadapter");
  gtk_widget_set_name (b_display_panadapter, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_panadapter), myrx->display_panadapter);
  gtk_grid_attach(GTK_GRID(general_grid), b_display_panadapter, col, row, 1, 1);
  g_signal_connect(b_display_panadapter, "toggled", G_CALLBACK(display_panadapter_cb), NULL);
  GtkWidget *b_display_waterfall = gtk_check_button_new_with_label("Display Waterfall");
  gtk_widget_set_name (b_display_waterfall, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_waterfall), myrx->display_waterfall);
  gtk_grid_attach(GTK_GRID(general_grid), b_display_waterfall, col + 1, row, 1, 1);
  g_signal_connect(b_display_waterfall, "toggled", G_CALLBACK(display_waterfall_cb), NULL);
  //
  // Peaks container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), peaks_container, 0, 1, 4, 1);
  GtkWidget *peaks_grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(peaks_grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(peaks_grid), TRUE);
  gtk_container_add(GTK_CONTAINER(peaks_container), peaks_grid);
  col = 0;
  row = 0;
  GtkWidget *b_panadapter_peaks_on = gtk_check_button_new_with_label("Label Strongest Peaks");
  gtk_widget_set_name(b_panadapter_peaks_on, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_panadapter_peaks_on), myrx->panadapter_peaks_on);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_panadapter_peaks_on, col, row, 1, 1);
  g_signal_connect(b_panadapter_peaks_on, "toggled", G_CALLBACK(panadapter_peaks_on_cb), NULL);
  row++;
  GtkWidget *b_pan_peaks_in_passband = gtk_check_button_new_with_label("Label in Passband Only");
  gtk_widget_set_name(b_pan_peaks_in_passband, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_pan_peaks_in_passband), myrx->panadapter_peaks_in_passband_filled);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_pan_peaks_in_passband, col, row, 1, 1);
  g_signal_connect(b_pan_peaks_in_passband, "toggled", G_CALLBACK(panadapter_peaks_in_passband_filled_cb), NULL);
  GtkWidget *b_pan_hide_noise = gtk_check_button_new_with_label("No Labels Below Noise Floor");
  gtk_widget_set_name(b_pan_hide_noise, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_pan_hide_noise), myrx->panadapter_hide_noise_filled);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_pan_hide_noise, col, ++row, 1, 1);
  g_signal_connect(b_pan_hide_noise, "toggled", G_CALLBACK(panadapter_hide_noise_filled_cb), NULL);
  label = gtk_label_new("Number of Peaks to Label");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, ++row, 1, 1);
  col++;
  GtkWidget *panadapter_num_peaks_r = gtk_spin_button_new_with_range(1.0, 10.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_num_peaks_r), (double)myrx->panadapter_num_peaks);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_num_peaks_r, col, row, 1, 1);
  g_signal_connect(panadapter_num_peaks_r, "value_changed", G_CALLBACK(panadapter_num_peaks_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Ignore Adjacent Peaks");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_ignore_range_divider_r = gtk_spin_button_new_with_range(1.0, 150.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_ignore_range_divider_r),
                            (double)myrx->panadapter_ignore_range_divider);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_ignore_range_divider_r, col, row, 1, 1);
  g_signal_connect(panadapter_ignore_range_divider_r, "value_changed",
                   G_CALLBACK(panadapter_ignore_range_divider_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Noise Floor Percentile");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_ignore_noise_percentile_r = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_ignore_noise_percentile_r),
                            (double)myrx->panadapter_ignore_noise_percentile);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_ignore_noise_percentile_r, col, row, 1, 1);
  g_signal_connect(panadapter_ignore_noise_percentile_r, "value_changed",
                   G_CALLBACK(panadapter_ignore_noise_percentile_value_changed_cb), NULL);
  row++;
  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  // Only show one of the General, Peaks containers
  // This is the General container upon first invocation of the Display menu,
  // but subsequent Display menu openings will show the container that
  // was active when leaving this menu before.
  //
  switch (which_container) {
  case GENERAL_CONTAINER:
    gtk_widget_hide(peaks_container);
    break;

  case PEAKS_CONTAINER:
    gtk_widget_hide(general_container);
    break;
  }
}

