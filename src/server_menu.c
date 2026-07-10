/* Copyright (C)
*  2020 - John Melton, G0ORX/N6LYT
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
#ifdef PORTFORWARD
  #include <miniupnpc/miniupnpc.h>
  #include <miniupnpc/upnpcommands.h>
#endif
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "client_server.h"
#include "main.h"
#include "message.h"
#include "new_menu.h"
#include "radio.h"
#include "server_menu.h"

static GtkWidget *dialog = NULL;
static GtkWidget *DuckUpdateBtn = NULL;
#ifdef PORTFORWARD
  static GtkWidget *PortUpdateBtn = NULL;
  static int fire_port = 1;
  static volatile int port_status = 0;
#endif
static GtkWidget *WanLbl = NULL;

static int fire_duck = 1;
// status = IDLE (0), TRYING (1), SUCCESS (2), FAILED (3), invalid credentials (4)
static volatile int duck_status = 0;
static int thread_spawned = 0;
static gulong button_timer_id = 0;
static char wan_ip_addr[64] = { 0 };

static void cleanup(void) {
  g_source_remove(button_timer_id);
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
  }
  radio_save_state();
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void* network_bkgd_thread(void *arg) {
  //
  // if this thread is started, it runs forever
  //
  int duck_counter = 1200;  // update DuckDNS (if requested) every 5 minutes
  int ip_counter = 0;       // query WAN address every 5 minutes
  char url[1024];
  char result[64];
  int rc;
  for (;;) {
    if (ip_counter == 0) {
      snprintf(url, sizeof(url), "https://ipv4.icanhazip.com");
      rc = run_curl(url, result, sizeof(result), 5);
      if (rc != 0) {
        snprintf(url, sizeof(url), "https://ipinfo.io/ip");
        rc = run_curl(url, result, sizeof(result), 5);
      }
      if (rc != 0) {
        snprintf(wan_ip_addr, sizeof(wan_ip_addr), "%s", "Not available.");
      } else {
        //
        // copy up to the first new-line
        //
        char *cp = strchr(result, '\n');
        if (cp) { *cp = '\0'; }
        snprintf(wan_ip_addr, sizeof(wan_ip_addr), "%s", result);
      }
      ip_counter = 1200;
    }
    if (server_duckdns) {
      if (fire_duck || duck_counter == 0) {
        fire_duck = 0;
        duck_status = 1;
        snprintf(url, sizeof(url), "https://www.duckdns.org/update?domains=%s&token=%s&ip=",
                 duckdns_host, duckdns_token);
        if (run_curl(url, result, sizeof(result), 5) == 0) {
          if (strncmp(result, "OK", 2) == 0) {
            duck_status = 2;
          } else if (strncmp(result, "KO", 2) == 0) {
            duck_status = 4;
          } else {
            duck_status = 3;
          }
        } else {
          duck_status = 3;
        }
        duck_counter = 1200;
      }
    } else {
      duck_status = 0;
    }
#ifdef PORTFORWARD
    if (server_port_fwd) {
      if (fire_port) {
        struct UPNPDev *devlist = NULL;
        char port_str[10];
        struct UPNPUrls   urls;
        struct IGDdatas   data;
        char lan_addr[64] = "";
        int error;
        fire_port = 0;
        port_status = 1;
#if (MINIUPNPC_API_VERSION >= 14)
        /* miniupnpc 1.9+ accepts ttl arg */
        devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
#else
        devlist = upnpDiscover(2000, NULL, NULL, 0, 0, &error);
#endif
        error = UPNPDISCOVER_UNKNOWN_ERROR;
        if (devlist) {
#if (MINIUPNPC_API_VERSION >= 18)
          /* miniupnpc 2.2.5+ adds wan_addr param */
          char wan_addr[64] = "";
          int  igd_status = UPNP_GetValidIGD(devlist, &urls, &data,
                                             lan_addr, sizeof(lan_addr),
                                             wan_addr, sizeof(wan_addr));
#else
          int  igd_status = UPNP_GetValidIGD(devlist, &urls, &data,
                                             lan_addr, sizeof(lan_addr));
#endif
          if (igd_status == 1 || igd_status == 2) {
            snprintf(port_str, sizeof(port_str), "%d", listen_port);
            /* Remove any stale mapping first (best-effort, ignore failure) */
            UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port_str, "TCP", NULL);
            error = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                        port_str, port_str,
                                        lan_addr, "piHPSDR remote", "TCP",
                                        NULL,    /* remoteHost — NULL = any */
                                        "0");    /* leaseDuration — 0 = permanent */
            FreeUPNPUrls(&urls);
          }
          freeUPNPDevlist(devlist);
        }
        if (error == UPNPCOMMAND_SUCCESS) {
          port_status = 2;
        } else {
          port_status = 3;
        }
      }
    } else {
      port_status = 0;
    }
#endif
    if (ip_counter > 0) { ip_counter--; }
    if (duck_counter > 0) { duck_counter--; }
    usleep(250000);
  }
  return NULL;
}

void start_network_helper() {
  pthread_t network_thread_id;
  if (!thread_spawned) {
    thread_spawned = 1;
    if (pthread_create(&network_thread_id, NULL, network_bkgd_thread, NULL) < 0) {
      t_print("%s: thread creation failed", __func__);
      thread_spawned = 0;
    }
    pthread_detach(network_thread_id);
  }
}

static int button_colour_timer(gpointer arg) {
  switch (duck_status) {
  case 0:
    gtk_widget_set_name(DuckUpdateBtn, "button");
    break;
  case 1:
    gtk_widget_set_name(DuckUpdateBtn, "yellowbutton");
    break;
  case 2:
    gtk_widget_set_name(DuckUpdateBtn, "greenbutton");
    break;
  case 3:
    gtk_widget_set_name(DuckUpdateBtn, "redbutton");
    break;
  case 4:
    gtk_widget_set_name(DuckUpdateBtn, "orangebutton");
    break;
  }
#ifdef PORTFORWARD
  switch (port_status) {
  case 0:
    gtk_widget_set_name(PortUpdateBtn, "button");
    break;
  case 1:
    gtk_widget_set_name(PortUpdateBtn, "yellowbutton");
    break;
  case 2:
    gtk_widget_set_name(PortUpdateBtn, "greenbutton");
    break;
  case 3:
    gtk_widget_set_name(PortUpdateBtn, "redbutton");
    break;
  }
#endif
  gtk_label_set_text(GTK_LABEL(WanLbl), wan_ip_addr);
  return G_SOURCE_CONTINUE;
}

static void server_duckdns_cb(GtkWidget *widget, gpointer data) {
  server_duckdns = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (server_duckdns) {
    if (!thread_spawned) { start_network_helper(); }
    fire_duck = 1;
  }
}

//cppcheck-suppress constParameterCallback
static gboolean duck_update_cb(GtkWidget *widget, gpointer data) {
  // do not fire while trying
  if (duck_status != 210) { fire_duck = 1; }
  return FALSE;
}

#ifdef PORTFORWARD
//cppcheck-suppress constParameterCallback
static gboolean port_update_cb(GtkWidget *widget, gpointer data) {
  // do not fire while trying
  if (port_status != 1) { fire_port = 1; }
  return FALSE;
}

static void server_port_cb(GtkWidget *widget, gpointer data) {
  server_port_fwd = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (server_port_fwd) {
    if (!thread_spawned) { start_network_helper(); }
    fire_port = 1;
  }
}
#endif

static void duck_host_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  snprintf(duckdns_host, sizeof(duckdns_host), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
}

static void duck_token_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  snprintf(duckdns_token, sizeof(duckdns_host), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
}


static void server_enable_cb(GtkWidget *widget, gpointer data) {
  hpsdr_server = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (hpsdr_server) {
    create_hpsdr_server();
  } else {
    destroy_hpsdr_server();
  }
}

static void server_stop_cb(GtkWidget *widget, gpointer data) {
  server_stops_protocol = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void port_value_changed_cb(GtkWidget *widget, gpointer data) {
  listen_port = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void pwd_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  snprintf(hpsdr_pwd, sizeof(hpsdr_pwd), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
}

void server_menu(GtkWidget *parent) {
  GtkWidget *lbl;
  GtkWidget *btn;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Server");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  int row = 0;
  //
  btn = gtk_button_new_with_label("Close");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 1, 1);
  row++;
  //
  btn = gtk_check_button_new_with_label("Server Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), hpsdr_server);
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(server_enable_cb), NULL);
  //
  btn = gtk_check_button_new_with_label("Server runs protocol only if client is connected");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), server_stops_protocol);
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 3, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(server_stop_cb), NULL);
  row++;
  //
  lbl = gtk_label_new("Server Port");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  //
  btn = gtk_spin_button_new_with_range(45000, 55000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)listen_port);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(port_value_changed_cb), NULL);
  //
  lbl = gtk_label_new("Server WAN IP: ");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, row, 1, 1);
  WanLbl = gtk_label_new(wan_ip_addr);
  gtk_widget_set_name(WanLbl, "boldlabel");
  gtk_widget_set_halign(WanLbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), WanLbl, 3, row, 1, 1);
  row++;
  //
  lbl = gtk_label_new("Server Password");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  //
  btn = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(btn), hpsdr_pwd);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 3, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(pwd_cb), NULL);
  row++;
  //
  btn = gtk_check_button_new_with_label("Use DuckDNS");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), server_duckdns);
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 2, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(server_duckdns_cb), NULL);
  //
  DuckUpdateBtn = gtk_button_new_with_label("Updt Duck");
  gtk_grid_attach(GTK_GRID(grid), DuckUpdateBtn, 1, row, 1, 1);
  g_signal_connect(DuckUpdateBtn, "button-press-event", G_CALLBACK(duck_update_cb), NULL);
#ifdef PORTFORWARD
  //
  btn = gtk_check_button_new_with_label("Port Mapping");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), server_port_fwd);
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(server_port_cb), NULL);
  //
  PortUpdateBtn = gtk_button_new_with_label("Updt Port");
  gtk_grid_attach(GTK_GRID(grid), PortUpdateBtn, 3, row, 1, 1);
  g_signal_connect(PortUpdateBtn, "button-press-event", G_CALLBACK(port_update_cb), NULL);
#endif
  row++;
  //
  lbl = gtk_label_new("DuckDNS Hostname");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  btn = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(btn), duckdns_host);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 3, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(duck_host_cb), NULL);
  row++;
  //
  lbl = gtk_label_new("DuckDNS Token");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  btn = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(btn), duckdns_token);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 3, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(duck_token_cb), NULL);
  //
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  button_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 250, button_colour_timer, NULL, NULL);
  gtk_widget_show_all(dialog);
}

