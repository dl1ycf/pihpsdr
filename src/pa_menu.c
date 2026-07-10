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
#include <semaphore.h>
#include <stdio.h>
#include <string.h>

#include "band.h"
#include "client_server.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;

static GtkWidget *pa_container = NULL;
static GtkWidget *watt_container = NULL;

// we need all these "spin" widgets as a static variable
// to continously update their displayed values during
// a "single shot" calibration
//
static GtkWidget *spinbtn[11];
static GtkWidget *spinlbl[11];

enum _containers {
  PA_CONTAINER = 1,
  WATT_CONTAINER
};

static int which_container = PA_CONTAINER;

static void update() {
  char text[16];
  int digits;
  double increment = 0.1 * pa_power_list[pa_power];
  for (int i = 1; i < 11; i++) {
    double low, high;
    GtkAdjustment *adjustment = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spinbtn[i]));
    high = 5 * i * increment;
    switch (pa_power) {
    case PA_1W:
      low = 0.001;
      digits = 3;
      snprintf(text, sizeof(text), "%0.3fW", i * increment);
      break;
    case PA_5W:
    case PA_10W:
      low = 0.1;
      digits = 1;
      snprintf(text, sizeof(text), "%0.1fW", i * increment);
      break;
    case PA_30W:
    case PA_50W:
    case PA_100W:
      low = 1;
      digits = 0;
      snprintf(text, sizeof(text), "%dW", (int) (i * increment));
      break;
    default:
      low = 5;
      digits = 0;
      snprintf(text, sizeof(text), "%dW", (int) (i * increment));
      snprintf(text, sizeof(text), "%dW", (int) (i * increment));
      break;
    }
    gtk_label_set_text(GTK_LABEL(spinlbl[i]), text);
    gtk_adjustment_configure (adjustment, pa_trim[i], low, high, low, low, low);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbtn[i]), digits);
  }
}

static void reset() {
  double increment = 0.1 * pa_power_list[pa_power];
  for (int i = 1; i < 11; i++) {
    pa_trim[i] = i * increment;
  }
  update();
}

static void sel_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle radio button in the top row, this selects
  // which sub-menu is active
  //
  int c = GPOINTER_TO_INT(data);
  GtkWidget *my_container;
  switch (c) {
  case PA_CONTAINER:
    my_container = pa_container;
    break;
  case WATT_CONTAINER:
    my_container = watt_container;
    break;
  default:
    // We should never come here
    return;
    break;
  }
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(my_container);
    gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
    which_container = c;
  } else {
    gtk_widget_hide(my_container);
  }
}

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

static void pa_value_changed_cb(GtkWidget *widget, gpointer data) {
  int b = GPOINTER_TO_INT(data);
  BAND *band = band_get_band(b);
  band->pa_calibration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  if (radio_is_remote) {
    send_band_data(cl_sock_tcp, b);
    return;
  } else {
    radio_calc_drive_level();
  }
}

//cppcheck-suppress constParameterCallback
static gboolean reset_trim_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  reset();
  if (radio_is_remote) {
    send_patrim(cl_sock_tcp);
  }
  return FALSE;
}

static void trim_changed_cb(GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  int k, flag;
  flag = 0;
  //
  // The 'flag' indicates that we do a single-shot calibration,
  // that is, the pa_trim[] values reflect a constant
  // factor and the last pa_trim[] value is changed.
  // In a single-shot calibration, change all the "lower" pa_trim
  // values to maintain the constant factor, and update the
  // text fields of the spinners.
  //
  if (i == 10) {
    flag = 1;
    for (k = 1; k < 10; k++) {
      double fac = ((double) k * pa_trim[10]) / ( 10.0 * pa_trim[k]);
      if ( fac < 0.99 || fac > 1.01) { flag = 0; }
    }
  }
  pa_trim[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  if (flag) {
    // note that we have i==10 if the flag is nonzero.
    for (k = 1; k < 10; k++) {
      pa_trim[k] = 0.1 * k * pa_trim[10];
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbtn[k]), (double)pa_trim[k]);
    }
  }
  if (radio_is_remote) {
    send_patrim(cl_sock_tcp);
  }
}

void pa_menu(GtkWidget *parent) {
  GtkWidget *lbl, *btn, *mbtn, *sep;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - PA Calibration");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  int col;
  int row = 0;
  which_container = PA_CONTAINER;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 2, 1);
  mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "PA calibration");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), 1);
  gtk_widget_set_halign(mbtn, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), mbtn, 2, row, 3, 1);
  g_signal_connect(mbtn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(PA_CONTAINER));
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Watt meter calibration");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), 0);
  gtk_widget_set_halign(btn, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), btn, 5, row, 4, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(WATT_CONTAINER));
  row++;
  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(sep, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), sep, 0, row, 9, 1);
  row++;
  pa_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), pa_container, 0, row, 9, 1);
  GtkWidget *pa_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(pa_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(pa_grid), 5);
  gtk_container_add(GTK_CONTAINER(pa_container), pa_grid);
  watt_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), watt_container, 0, row, 9, 1);
  GtkWidget *watt_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(watt_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(watt_grid), 5);
  gtk_container_add(GTK_CONTAINER(watt_container), watt_grid);
  //
  // Populate "Calibration" container
  //
  int bands = radio_max_band();
  int b = 0;
  if (tx_out_of_band_allowed) {
    //
    // If out-of-band TXing is allowed, we need a PA calibration value
    // for the "general" band. Note that if out-of-band TX is allowed
    // while the menu is open, this will not appear (one has to close
    // and re-open the menu).
    //
    const BAND *band = band_get_band(bandGen);
    lbl = gtk_label_new(band->title);
    gtk_widget_set_name(lbl, "boldlabel");
    gtk_grid_attach(GTK_GRID(pa_grid), lbl, (b / 6) * 2, (b % 6) + 1, 1, 1);
    btn = gtk_spin_button_new_with_range(38.8, 100.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)band->pa_calibration);
    gtk_grid_attach(GTK_GRID(pa_grid), btn, ((b / 6) * 2) + 1, (b % 6) + 1, 1, 1);
    g_signal_connect(btn, "value_changed", G_CALLBACK(pa_value_changed_cb), GINT_TO_POINTER(bandGen));
    b++;
  }
  for (int i = 0; i <= bands; i++) {
    const BAND *band = band_get_band(i);
    GtkWidget *band_label = gtk_label_new(band->title);
    gtk_widget_set_name(band_label, "boldlabel");
    gtk_widget_show(band_label);
    gtk_grid_attach(GTK_GRID(pa_grid), band_label, (b / 6) * 2, (b % 6) + 1, 1, 1);
    GtkWidget *pa_r = gtk_spin_button_new_with_range(38.8, 100.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pa_r), (double)band->pa_calibration);
    gtk_widget_show(pa_r);
    gtk_grid_attach(GTK_GRID(pa_grid), pa_r, ((b / 6) * 2) + 1, (b % 6) + 1, 1, 1);
    g_signal_connect(pa_r, "value_changed", G_CALLBACK(pa_value_changed_cb), GINT_TO_POINTER(i));
    b++;
  }
  for (int i = BANDS; i < BANDS + XVTRS; i++) {
    const BAND *band = band_get_band(i);
    if (strlen(band->title) > 0) {
      lbl = gtk_label_new(band->title);
      gtk_widget_set_name(lbl, "boldlabel");
      gtk_grid_attach(GTK_GRID(pa_grid), lbl, (b / 6) * 2, (b % 6) + 1, 1, 1);
      btn = gtk_spin_button_new_with_range(38.8, 100.0, 0.1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)band->pa_calibration);
      gtk_grid_attach(GTK_GRID(pa_grid), btn, ((b / 6) * 2) + 1, (b % 6) + 1, 1, 1);
      g_signal_connect(btn, "value_changed", G_CALLBACK(pa_value_changed_cb), GINT_TO_POINTER(i));
      b++;
    }
  }
  //
  // Populate "Watt" container
  //
  row = 1;
  col = 0;
  for (int i = 1; i < 11; i++) {
    spinlbl[i] = gtk_label_new(NULL);
    gtk_widget_set_halign(spinlbl[i], GTK_ALIGN_END);
    gtk_widget_set_name(spinlbl[i], "boldlabel");
    gtk_grid_attach(GTK_GRID(watt_grid), spinlbl[i], col++, row, 1, 1);
    spinbtn[i] = gtk_spin_button_new_with_range(0.001, 1.0, 0.001);
    gtk_grid_attach(GTK_GRID(watt_grid), spinbtn[i], col++, row, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbtn[i]), (double)i);
    g_signal_connect(spinbtn[i], "value_changed", G_CALLBACK(trim_changed_cb), GINT_TO_POINTER(i));
    if (col == 4) {
      row++;
      col = 0;
    }
  }
  update();
  btn = gtk_button_new_with_label("Reset Watt Meter Calibration");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_grid_attach(GTK_GRID(watt_grid), btn, 0, row, 2, 1);
  g_signal_connect(btn, "button-press-event", G_CALLBACK(reset_trim_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  gtk_widget_hide(watt_container);
}

