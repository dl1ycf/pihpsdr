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

#include "about_menu.h"
#include "actions.h"
#include "agc_menu.h"
#include "ant_menu.h"
#include "audio.h"
#include "band_menu.h"
#include "bandstack_menu.h"
#include "client_server.h"
#include "cw_menu.h"
#include "display_menu.h"
#include "diversity_menu.h"
#include "dxcluster_menu.h"
#include "dxcluster_history_menu.h"
#include "encoder_menu.h"
#include "equalizer_menu.h"
#include "exit_menu.h"
#include "fft_menu.h"
#include "filter_menu.h"
#include "g2panel_menu.h"
#include "gpio.h"
#include "main.h"
#include "meter_menu.h"
#ifdef MIDI
  #include "midi_menu.h"
  #include "midi.h"
#endif
#include "mode_menu.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "noise_menu.h"
#include "oc_menu.h"
#include "old_protocol.h"
#include "pa_menu.h"
#include "profile_menu.h"
#include "ps_menu.h"
#include "radio_menu.h"
#include "radio.h"
#include "rigctl_menu.h"
#include "rx_menu.h"
#include "server_menu.h"
#include "screen_menu.h"
#include "sliders_menu.h"
#include "store_menu.h"
#include "switch_menu.h"
#include "theme_menu.h"
#include "toolbar_menu.h"
#include "tx_menu.h"
#include "xvtr_menu.h"
#include "vfo_menu.h"
#include "vox_menu.h"

GtkWidget *main_menu = NULL;
GtkWidget *sub_menu = NULL;

int active_menu = NO_MENU;

int menu_active_receiver_changed(gpointer data) {
  if (sub_menu != NULL) {
    gtk_widget_destroy(sub_menu);
    sub_menu = NULL;
  }
  return FALSE;
}

static void cleanup(void) {
  if (main_menu != NULL) {
    gtk_widget_destroy(main_menu);
    main_menu = NULL;
  }
  if (sub_menu != NULL) {
    gtk_widget_destroy(sub_menu);
    sub_menu = NULL;
  }
  active_menu = NO_MENU;
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

//
// The "Restart" button restarts the protocol
// This may help to recover from certain error conditions
// Hitting this button automatically closes the menu window via cleanup()
//
// cppcheck-suppress constParameterCallback
static gboolean restart_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  if (radio_is_remote) {
    send_restart(cl_sock_tcp);
  } else {
    radio_protocol_restart();
  }
  return TRUE;
}

//
// This functionality may be useful in full-screen-mode where there is
// no top bar with an "Iconify" button.
//
// cppcheck-suppress constParameterCallback
static gboolean minimize_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  radio_iconify();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean theme_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  theme_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean about_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  about_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean exit_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  exit_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean radio_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_radio_menu();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean rx_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_rx_menu();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean ant_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  ant_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean display_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  display_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean pa_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  pa_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean rigctl_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  rigctl_menu(top_window);
  return TRUE;
}

#ifdef GPIO
// cppcheck-suppress constParameterCallback
static gboolean encoder_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  encoder_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean switch_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  switch_menu(top_window);
  return TRUE;
}

#endif

// cppcheck-suppress constParameterCallback
static gboolean g2panel_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  g2panel_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean toolbar_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  toolbar_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean sliders_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  sliders_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean cw_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  cw_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean oc_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  oc_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean xvtr_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  xvtr_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean profile_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  profile_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean equaliser_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  equalizer_menu(top_window);
  return TRUE;
}

void start_radio_menu(void) {
  cleanup();
  radio_menu(top_window);
}

void start_rx_menu(void) {
  cleanup();
  rx_menu(top_window);
}

void start_meter_menu(void) {
  cleanup();
  meter_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean meter_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_meter_menu();
  return TRUE;
}

void start_band_menu(int vfo) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != BAND_MENU) {
    band_menu(top_window, vfo);
    active_menu = BAND_MENU;
  }
}

void start_bandstack_menu(void) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != BANDSTACK_MENU) {
    bandstack_menu(top_window);
    active_menu = BANDSTACK_MENU;
  }
}

void start_mode_menu(int vfo) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != MODE_MENU) {
    mode_menu(top_window, vfo);
    active_menu = MODE_MENU;
  }
}

void start_filter_menu(int vfo) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != FILTER_MENU) {
    filter_menu(top_window, vfo);
    active_menu = FILTER_MENU;
  }
}

// cppcheck-suppress constParameterCallback
static gboolean mode_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_mode_menu(active_receiver->id);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean filter_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_filter_menu(active_receiver->id);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean noise_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_noise_menu();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean vfo_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_vfo_menu(active_receiver->id);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean band_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_band_menu(active_receiver->id);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean bstk_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_bandstack_menu();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean store_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_store_menu();
  return TRUE;
}

void start_noise_menu(void) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != NOISE_MENU) {
    noise_menu(top_window);
    active_menu = NOISE_MENU;
  }
}

// cppcheck-suppress constParameterCallback
static gboolean agc_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_agc_menu();
  return TRUE;
}

void start_agc_menu(void) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != AGC_MENU) {
    agc_menu(top_window);
    active_menu = AGC_MENU;
  }
}

static void start_vox_menu(void) {
  cleanup();
  vox_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean vox_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_vox_menu();
  return TRUE;
}

static void start_dsp_menu(void) {
  cleanup();
  fft_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean dsp_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_dsp_menu();
  return TRUE;
}

void start_diversity_menu(void) {
  cleanup();
  diversity_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean screen_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  cleanup();
  screen_menu(top_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean diversity_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_diversity_menu();
  return TRUE;
}

void start_vfo_menu(int vfo) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != VFO_MENU) {
    vfo_menu(top_window, vfo);
    active_menu = VFO_MENU;
  }
}

void start_store_menu(void) {
  int old_menu = active_menu;
  cleanup();
  if (old_menu != STORE_MENU) {
    store_menu(top_window);
    active_menu = STORE_MENU;
  }
}

void start_tx_menu(void) {
  cleanup();
  if (can_transmit) {
    tx_menu(top_window);
  }
}

// cppcheck-suppress constParameterCallback
static gboolean tx_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_tx_menu();
  return TRUE;
}

void start_ps_menu(void) {
  cleanup();
  if (can_transmit) {
    ps_menu(top_window);
  }
}

// cppcheck-suppress constParameterCallback
static gboolean ps_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_ps_menu();
  return TRUE;
}

void start_server_menu(void) {
  cleanup();
  server_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean server_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_server_menu();
  return TRUE;
}

static void start_dxcluster_menu(void) {
  cleanup();
  dxcluster_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean dxcluster_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_dxcluster_menu();
  return TRUE;
}

static void start_dxspots_menu(void) {
  cleanup();
  dxcluster_history_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean dxspots_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_dxspots_menu();
  return TRUE;
}

#ifdef MIDI
static void start_midi_menu(void) {
  cleanup();
  midi_menu(top_window);
}

// cppcheck-suppress constParameterCallback
static gboolean midi_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_midi_menu();
  return TRUE;
}

#endif

void new_menu(void) {
  GtkWidget *btn, *w;
  int col, row;
  if (sub_menu != NULL) {
    gtk_widget_destroy(sub_menu);
    sub_menu = NULL;
  }
  if (main_menu == NULL) {
    main_menu = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(main_menu), GTK_WINDOW(top_window));
    GtkWidget *headerbar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(main_menu), headerbar);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Menu");
    g_signal_connect (main_menu, "delete_event", G_CALLBACK (close_cb), NULL);
    g_signal_connect (main_menu, "destroy", G_CALLBACK (close_cb), NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(main_menu));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    //
    // First row is reserved for Close/About/Iconify/Restart/Exit
    //
    btn = gtk_button_new_with_label("Close");
    gtk_widget_set_name(btn, "close_button");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
    //
    btn = gtk_button_new_with_label("About");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(about_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, 3, 0, 1, 1);
    //
    btn = gtk_button_new_with_label("Icon");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(minimize_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, 4, 0, 1, 1);
    //
    btn = gtk_button_new_with_label("Restart");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(restart_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, 5, 0, 1, 1);
    //
    btn = gtk_button_new_with_label("Exit");
    gtk_widget_set_name(btn, "close_button");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(exit_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, 6, 0, 1, 1);
    //
    // Insert small separation between top column the the "many buttons"
    //
    w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(w, -1, 3);
    gtk_grid_attach(GTK_GRID(grid), w, 0, 1, 7, 1);
    row = 2;
    col = 0;
    //
    // Special menu:
    // Cat, Server, DX, Spots
    //
    btn = gtk_button_new_with_label("Screen");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(screen_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("CatTci");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(rigctl_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    if (!radio_is_remote) {
      btn = gtk_button_new_with_label("Server");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(server_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
      row++;
    }
    btn = gtk_button_new_with_label("DX");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(dxcluster_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    btn = gtk_button_new_with_label("Spots");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(dxspots_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row = 2;
    col++;
    //
    // Menus related to the Radio in general:
    //  Radio/Theme/Display/Meter/Xvtr
    //
    btn = gtk_button_new_with_label("Radio");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(radio_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Theme");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(theme_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Display");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(display_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Meter");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(meter_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("XVTR");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(xvtr_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row = 2;
    col++;
    //
    // VFO-related menus
    // Freq, Band, Bandstack, Mode, Memory, Xvtr
    //
    btn = gtk_button_new_with_label("VFO");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(vfo_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Band");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(band_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("BdStack");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(bstk_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Mode");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(mode_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("MEM");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(store_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row = 2;
    col++;
    //
    // RX-related menus
    // RX/Filter/Noise/AGC/Diversity
    //
    btn = gtk_button_new_with_label("RX");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(rx_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Filter");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(filter_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Noise");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(noise_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("AGC");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(agc_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    if (RECEIVERS == 2 && n_adc > 1) {
      btn = gtk_button_new_with_label("DIV");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(diversity_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    }
    row = 2;
    col++;
    //
    // TX-related menus
    // TX, PA, VOX, PS, CW
    //
    if (can_transmit) {
      btn = gtk_button_new_with_label("TX");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(tx_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
      row++;
      //
      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        btn = gtk_button_new_with_label("PA");
        g_signal_connect (btn, "button-press-event", G_CALLBACK(pa_cb), NULL);
        gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
        row++;
      }
      //
      btn = gtk_button_new_with_label("VOX");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(vox_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
      row++;
      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        btn = gtk_button_new_with_label("PS");
        g_signal_connect (btn, "button-press-event", G_CALLBACK(ps_cb), NULL);
        gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
        row++;
      }
    }
    btn = gtk_button_new_with_label("CW");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(cw_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row = 2;
    col++;
    //
    // Menus for RX and TX
    // DSP, Equaliser,  Ant, OC
    //
    btn = gtk_button_new_with_label("DSP");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(dsp_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("EQ");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(equaliser_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Profile");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(profile_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Ant");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(ant_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      btn = gtk_button_new_with_label("OC");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(oc_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    }
    row = 2;
    col++;
    //
    // Menus for controlling piHPSDR
    // Toolbar, Sliders, MIDI, Encoders, Switches
    //
    btn = gtk_button_new_with_label("Toolbar");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(toolbar_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
    btn = gtk_button_new_with_label("Sliders");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(sliders_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
    //
#ifdef MIDI
    btn = gtk_button_new_with_label("MIDI");
    g_signal_connect (btn, "button-press-event", G_CALLBACK(midi_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    row++;
#endif
#ifdef GPIO
    if (controller != NO_CONTROLLER) {
      btn = gtk_button_new_with_label("Encoders");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(encoder_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
      row++;
    }
    //
    // Note the switches of CONTROLLER1 are assigned via the Toolbar menu
    //
    if (controller != NO_CONTROLLER && controller != CONTROLLER1) {
      btn = gtk_button_new_with_label("Switches");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(switch_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
      row++;
    }
#endif
    if (have_g2v2) {
      btn = gtk_button_new_with_label("G2 Panel");
      g_signal_connect (btn, "button-press-event", G_CALLBACK(g2panel_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
    }
    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(main_menu);
  } else {
    gtk_widget_destroy(main_menu);
    main_menu = NULL;
  }
}
