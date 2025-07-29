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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wdsp.h>             // only needed for GetWDSPVersion

#include "about_menu.h"
#include "discovered.h"
#include "new_menu.h"
#include "radio.h"
#include "version.h"

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

void about_menu(GtkWidget *parent) {
  char text[1024];
  GtkWidget *label;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  snprintf(title, sizeof(title), "piHPSDR - About");
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 0);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  int row = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 4, 1);
  row++;
  snprintf(text, sizeof(text), "piHPSDR by John Melton (G0ORX/N6LYT) and Christoph van Wüllen (DL1YCF)");
  label = gtk_label_new(text);
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 19, 1);
  row++;
  snprintf(text, sizeof(text), "With help from:\n"
                               "  Steve Wilson, KA6S: RIGCTL (CAT over TCP)\n"
                               "  Laurence Barker, G8NJJ: USB OZY Support\n"
                               "  Ken Hopper, N9VV: Testing and Documentation");
  label = gtk_label_new(text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_name(label, "small_button");
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 19, 1);
  row++;
  snprintf(text, sizeof(text), "Build date: %s (commit %s)\n"
                               "Build version: %s\n"
                               "WDSP version: %d.%02d",
           build_date, build_commit, build_version, GetWDSPVersion() / 100, GetWDSPVersion() % 100);
  label = gtk_label_new(text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_name(label, "small_button");
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 19, 1);
  row++;

  switch (radio->protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    if (device == DEVICE_OZY) {
      snprintf(text, sizeof(text), "Device:  OZY (via USB)  Protocol %s v%d.%d",
               radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
               radio->software_version / 10, radio->software_version % 10);
    } else {
      char interface_addr[128];
      char addr[128];
      snprintf(addr, sizeof(addr), "%s", inet_ntoa(radio->network.address.sin_addr));
      snprintf(interface_addr, sizeof(interface_addr), "%s", inet_ntoa(radio->network.interface_address.sin_addr));

      if (have_saturn_xdma) {
        snprintf(text, sizeof(text), "Device: Saturn (via XDMA), Protocol %s, v%d.%d\n",
                 radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                 radio->software_version / 10, radio->software_version % 10);
      } else {
        snprintf(text, sizeof(text), "Device: %s, Protocol %s, v%d.%d\n"
                                     "Mac Address: %02X:%02X:%02X:%02X:%02X:%02X\n"
                                     "IP Address: %s on %s (%s)",
                 radio->name, radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                 radio->software_version / 10, radio->software_version % 10,
                 radio->network.mac_address[0],
                 radio->network.mac_address[1],
                 radio->network.mac_address[2],
                 radio->network.mac_address[3],
                 radio->network.mac_address[4],
                 radio->network.mac_address[5],
                 addr,
                 radio->network.interface_name,
                 interface_addr);
      }
    }

    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    snprintf(text, sizeof(text), "Device: %s (via SoapySDR)\n"
                                 "    %s (%s)",
             radio->name, radio->soapy.hardware_key, radio->soapy.driver_key);
    break;
#endif
  }

  label = gtk_label_new(text);
  gtk_widget_set_name(label, "small_button");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 19, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
