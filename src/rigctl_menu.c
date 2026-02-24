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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "band.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "rigctl.h"
#include "tci.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;
static GtkWidget *serial_speed[MAX_SERIAL + 1];
static GtkWidget *serial_enable[MAX_SERIAL + 1];

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

static void tcp_autoreporting_cb(GtkWidget *widget, gpointer data) {
  rigctl_tcp_autoreporting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void rigctl_value_changed_cb(GtkWidget *widget, gpointer data) {
  if (rigctl_tcp_enable) { shutdown_tcp_rigctl(); }

  rigctl_tcp_port = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (rigctl_tcp_enable) { launch_tcp_rigctl(); }
}

static void rigctl_debug_cb(GtkWidget *widget, gpointer data) {
  rigctl_debug = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tci_enable_cb(GtkWidget *widget, gpointer data) {
  tci_enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (tci_enable) {
    launch_tci();
  } else {
    shutdown_tci();
  }
}

static void tci_port_changed_cb(GtkWidget *widget, gpointer data) {
  if (tci_enable) { shutdown_tci(); }

  tci_port = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (tci_enable) { launch_tci(); }
}

static void tci_txonly_changed_cb(GtkWidget *widget, gpointer data) {
  tci_txonly = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void rigctl_tcp_enable_cb(GtkWidget *widget, gpointer data) {
  rigctl_tcp_enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (rigctl_tcp_enable) {
    launch_tcp_rigctl();
  } else {
    shutdown_tcp_rigctl();
  }
}

//
// Note that this call-back is invoked on each and every key stroke
// in the text field
//
static void serial_port_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  const char *cp = gtk_entry_get_text(GTK_ENTRY(widget));

  //
  // - If the serial port is already running, do not allow changes.
  // - If the name occurs for the "auto-detected" port, do not allow
  //   to use that name.
  //
  if (SerialPorts[id].enable ||
      (SerialPorts[MAX_SERIAL - 1].g2 && !strcmp(SerialPorts[MAX_SERIAL - 1].port, cp))) {
    gtk_entry_set_text(GTK_ENTRY(widget), SerialPorts[id].port);
  } else {
    snprintf(SerialPorts[id].port, sizeof(SerialPorts[id].port), "%s", cp);
  }
}

static void tcp_andromeda_cb(GtkWidget *widget, gpointer data) {
  rigctl_tcp_andromeda = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

//
// Note the following call-backs will never be called for G2-internal serial ports:
//
// - serial_autoreporting_cb
// - andromeda_cb
// - serial_enable_cb
// - speed_cb
//
//
static void serial_autoreporting_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  SerialPorts[id].autoreporting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void andromeda_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  SerialPorts[id].andromeda = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (SerialPorts[id].andromeda) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(serial_speed[id]), 1);
    SerialPorts[id].speed = B9600;
  }
}

static void ptt_enable_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);

  if ((SerialPorts[id].enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))) {
    if (launch_serial_ptt(id) == 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
      SerialPorts[id].enable = 0;
    }
  } else {
    disable_serial_ptt(id);
  }

  t_print("%s: Serial PTT : ID=%d Enabled=%d\n", __func__, id, SerialPorts[id].enable);
}

static void serial_enable_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);

  if ((SerialPorts[id].enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))) {
    if (launch_serial_rigctl(id) == 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
      SerialPorts[id].enable = 0;
    }
  } else {
    disable_serial_rigctl(id);
  }

  t_print("%s: Serial enable : ID=%d Enabled=%d\n", __func__, id, SerialPorts[id].enable);
}

// Set Baud Rate
static void speed_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  int bd = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  speed_t new;

  //
  // If ANDROMEDA is active, keep 9600
  //
  if (SerialPorts[id].andromeda && SerialPorts[id].speed == B9600) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
    return;
  }

  //
  // Do nothing if the baud rate is already effective.
  // If a serial client is already running and the baud rate is changed, we close and re-open it
  //
  switch (bd) {
  case 0:
  default:
    new = B4800;
    break;

  case 1:
    new = B9600;
    break;

  case 2:
    new = B19200;
    break;

  case 3:
    new = B38400;
    break;
  }

  if (new == SerialPorts[id].speed) {
    return;
  }

  SerialPorts[id].speed = new;

  if (SerialPorts[id].enable) {
    t_print("%s: closing/re-opening serial port %s\n", __func__, SerialPorts[id].port);
    disable_serial_rigctl(id);

    if (launch_serial_rigctl(id) == 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(serial_enable[id]), FALSE);
      SerialPorts[id].enable = 0;
    }
  }

  t_print("%s: Baud rate changed: Port=%s SpeedCode=%d\n", __func__, SerialPorts[id].port,
          (int) SerialPorts[id].speed);
}

void rigctl_menu(GtkWidget *parent) {
  GtkWidget *w;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - CAT/TCI");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  int row = 0;
  w = gtk_button_new_with_label("Close");
  gtk_widget_set_name(w, "close_button");
  g_signal_connect (w, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 2, 1);
  w = gtk_check_button_new_with_label("Enable CAT/TCI Debug Logging");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_debug);
  gtk_grid_attach(GTK_GRID(grid), w, 4, row, 4, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(rigctl_debug_cb), NULL);
  row++;
  w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(w, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 7, 1);
  row++;
  w = gtk_label_new("TCI");
  gtk_widget_set_name(w, "boldlabel");
  gtk_widget_set_halign(w, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);
  w = gtk_spin_button_new_with_range(35000, 55000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), (double)tci_port);
  gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
  g_signal_connect(w, "value_changed", G_CALLBACK(tci_port_changed_cb), NULL);
  w = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), tci_enable);
  gtk_widget_show(w);
  gtk_grid_attach(GTK_GRID(grid), w, 4, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tci_enable_cb), NULL);
  w = gtk_check_button_new_with_label("Report TX Frequency Only");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), tci_txonly);
  gtk_widget_show(w);
  gtk_grid_attach(GTK_GRID(grid), w, 5, row, 3, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tci_txonly_changed_cb), NULL);
  row++;
  w = gtk_label_new("TCP");
  gtk_widget_set_name(w, "boldlabel");
  gtk_widget_set_halign(w, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);
  w = gtk_spin_button_new_with_range(18000, 21000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), (double)rigctl_tcp_port);
  gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
  g_signal_connect(w, "value_changed", G_CALLBACK(rigctl_value_changed_cb), NULL);
  w = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_tcp_enable);
  gtk_widget_show(w);
  gtk_grid_attach(GTK_GRID(grid), w, 4, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(rigctl_tcp_enable_cb), NULL);
  w = gtk_check_button_new_with_label("Andromeda");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_tcp_andromeda);
  gtk_grid_attach(GTK_GRID(grid), w, 5, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tcp_andromeda_cb), NULL);
  w = gtk_check_button_new_with_label("AutoRprt");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_tcp_autoreporting);
  gtk_grid_attach(GTK_GRID(grid), w, 6, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tcp_autoreporting_cb), NULL);
  row++;
  w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(w, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 7, 1);

  /* Put the Serial Port stuff here, one port per line */
  for (int i = 0; i <= MAX_SERIAL; i++) {
    char str[512];
    row++;
    int is_ptt = (i == MAX_SERIAL);
    //
    // If this serial port is used internally in a G2 simply state port name
    //
    snprintf (str, sizeof(str), is_ptt ? "PTT" : "Serial");
    w = gtk_label_new(str);
    gtk_widget_set_name(w, "boldlabel");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);

    if (!SerialPorts[i].g2) {
      w = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(w), SerialPorts[i].port);
      gtk_grid_attach(GTK_GRID(grid), w, 1, row, 2, 1);
      g_signal_connect(w, "changed", G_CALLBACK(serial_port_cb), GINT_TO_POINTER(i));
      if (is_ptt) {
        serial_enable[i] = gtk_check_button_new_with_label("Enable (serial port for PTT in/out)");
        gtk_widget_set_name(serial_enable[i], "boldlabel");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (serial_enable[i]), SerialPorts[i].enable);
        gtk_grid_attach(GTK_GRID(grid), serial_enable[i], 4, row, 3, 1);
        g_signal_connect(serial_enable[i], "toggled", G_CALLBACK(ptt_enable_cb), GINT_TO_POINTER(i));
      } else {
        serial_speed[i] = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_speed[i]), NULL, "4800 Bd");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_speed[i]), NULL, "9600 Bd");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_speed[i]), NULL, "19200 Bd");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_speed[i]), NULL, "38400 Bd");

        switch (SerialPorts[i].speed) {
        case B9600:
          gtk_combo_box_set_active(GTK_COMBO_BOX(serial_speed[i]), 1);
          break;

        case B19200:
          gtk_combo_box_set_active(GTK_COMBO_BOX(serial_speed[i]), 2);
          break;

        case B38400:
          gtk_combo_box_set_active(GTK_COMBO_BOX(serial_speed[i]), 3);
          break;

        default:
          SerialPorts[i].speed = B4800;
          gtk_combo_box_set_active(GTK_COMBO_BOX(serial_speed[i]), 0);
          break;
        }

        my_combo_attach(GTK_GRID(grid), serial_speed[i], 3, row, 1, 1);
        g_signal_connect(serial_speed[i], "changed", G_CALLBACK(speed_cb), GINT_TO_POINTER(i));
        serial_enable[i] = gtk_check_button_new_with_label("Enable");
        gtk_widget_set_name(serial_enable[i], "boldlabel");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (serial_enable[i]), SerialPorts[i].enable);
        gtk_grid_attach(GTK_GRID(grid), serial_enable[i], 4, row, 1, 1);
        g_signal_connect(serial_enable[i], "toggled", G_CALLBACK(serial_enable_cb), GINT_TO_POINTER(i));
        w = gtk_check_button_new_with_label("Andromeda");
        gtk_widget_set_name(w, "boldlabel");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), SerialPorts[i].andromeda);
        gtk_grid_attach(GTK_GRID(grid), w, 5, row, 1, 1);
        g_signal_connect(w, "toggled", G_CALLBACK(andromeda_cb), GINT_TO_POINTER(i));
        w = gtk_check_button_new_with_label("AutoRprt");
        gtk_widget_set_name(w, "boldlabel");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), SerialPorts[i].autoreporting);
        gtk_grid_attach(GTK_GRID(grid), w, 6, row, 1, 1);
        g_signal_connect(w, "toggled", G_CALLBACK(serial_autoreporting_cb), GINT_TO_POINTER(i));
      }
    } else {
      //
      // - If this serial port is auto-detected, simply report name but offer no
      //   GUI elements for changing things.
      // - If it is not enabled, this means the initial launch_serial() failed.
      //
      snprintf (str, sizeof(str), "%s %s for G2/Controller3", SerialPorts[i].port,
                SerialPorts[i].enable ? "used" : "failed");
      w = gtk_label_new(str);
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 1, row, 5, 1);
    }
  }
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  gtk_window_resize(GTK_WINDOW(dialog), 1, 1);
}

