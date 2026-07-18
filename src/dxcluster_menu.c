/* Copyright (C)
*  2026 - piHPSDR Modernisation, contribution from AL
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
#include <stdlib.h>
#include <string.h>

#include "dxcluster.h"
#include "dxcluster_menu.h"
#include "new_menu.h"
#include "message.h"
#include "radio.h"

static GtkWidget *dialog       = NULL;

/* Widget handles for reading back values */
static GtkWidget *w_enable;
static GtkWidget *w_show_pan;
static GtkWidget *w_auto_recon;
static GtkWidget *w_server;
static GtkWidget *w_port;
static GtkWidget *w_call;
static GtkWidget *w_age_5, *w_age_10, *w_age_30, *w_age_60;
static GtkWidget *w_m_ft8, *w_m_ft4, *w_m_cw, *w_m_ssb, *w_m_rtty, *w_m_other;
static GtkWidget *w_r_na, *w_r_eu, *w_r_as, *w_r_sa, *w_r_af, *w_r_oc;
static GtkWidget *w_whitelist, *w_blacklist;
static GtkWidget *apply_btn;
static gulong button_timer_id = 0;

static void cleanup(void) {
  g_source_remove(button_timer_id);
  if (dialog) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu = NO_MENU;
    radio_save_state();
  }
}

//cppcheck-suppress constParameterCallback
static gboolean on_close_pressed(GtkWidget *w, GdkEventButton *e, gpointer d) {
  cleanup();
  return TRUE;
}

//cppcheck-suppress constParameterCallback
static gboolean on_delete_event(GtkWidget *w, GdkEvent *e, gpointer d) {
  cleanup();
  return TRUE;
}

/* Read dialog widgets into a DXC_SETTINGS struct */
static void read_settings(DXC_SETTINGS *s) {
  memset(s, 0, sizeof(*s));
  s->enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_enable));
  s->show_on_panadapter =
    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_show_pan));
  s->auto_reconnect =
    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_auto_recon));
  snprintf(s->server, sizeof(s->server), "%s",
           gtk_entry_get_text(GTK_ENTRY(w_server)));
  s->port = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w_port));
  snprintf(s->callsign, sizeof(s->callsign), "%s",
           gtk_entry_get_text(GTK_ENTRY(w_call)));
  /* Age */
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_age_5))) { s->age_limit_sec = 300; }
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_age_10))) { s->age_limit_sec = 600; }
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_age_30))) { s->age_limit_sec = 1800; }
  else { s->age_limit_sec = 3600; }
  /* Modes */
  s->mode_ft8   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_m_ft8));
  s->mode_ft4   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_m_ft4));
  s->mode_cw    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_m_cw));
  s->mode_ssb   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_m_ssb));
  s->mode_rtty  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_m_rtty));
  s->mode_other = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_m_other));
  /* Regions */
  s->region_na = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_r_na));
  s->region_eu = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_r_eu));
  s->region_as = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_r_as));
  s->region_sa = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_r_sa));
  s->region_af = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_r_af));
  s->region_oc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_r_oc));
  /* Lists */
  snprintf(s->whitelist, sizeof(s->whitelist), "%s", gtk_entry_get_text(GTK_ENTRY(w_whitelist)));
  snprintf(s->blacklist, sizeof(s->blacklist), "%s", gtk_entry_get_text(GTK_ENTRY(w_blacklist)));
}

static void on_save_clicked(GtkButton *btn, gpointer data) {
  //
  // Instead of connecting a signal to all the elements, we have a single "Apply" button.
  // This signal then reads all the states of the GUI elements into a DXC_SETTING structure
  // and applies&save the data. After that, the menu is closed.
  //
  DXC_SETTINGS s;
  read_settings(&s);
  dxcluster_apply_settings(&s);
}

static int button_colour_timer(gpointer arg) {
  DXC_STATE s = dxcluster_get_state();
  switch (s) {
  case DXC_DISABLED:
    gtk_widget_set_name(apply_btn, "button");
    break;
  case DXC_CONNECTING:
    gtk_widget_set_name(apply_btn, "yellowbutton");
    break;
  case DXC_CONNECTED:
    gtk_widget_set_name(apply_btn, "greenbutton");
    break;
  case DXC_ERROR:
    gtk_widget_set_name(apply_btn, "redbutton");
    break;
  case DXC_DISCONNECTED:
    gtk_widget_set_name(apply_btn, "orangebutton");
    break;
  }
  return G_SOURCE_CONTINUE;
}

/* ── Dialog construction ──────────────────────────────────────────────── */

void dxcluster_menu(GtkWidget *parent) {
  GtkWidget *lbl;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - DX Cluster");
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  g_signal_connect(dialog, "delete-event", G_CALLBACK(on_delete_event), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  //gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
  gtk_container_add(GTK_CONTAINER(content), grid);
  /* Snapshot current settings */
  DXC_SETTINGS cur;
  dxcluster_get_settings(&cur);
  int row = 1;
  /* Server / port / callsign */
  lbl = gtk_label_new("Server/Port");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl,   0, row, 1, 1);
  w_server = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(w_server), cur.server);
  gtk_entry_set_width_chars(GTK_ENTRY(w_server), 18);
  gtk_grid_attach(GTK_GRID(grid), w_server, 1, row, 4, 1);
  w_port = gtk_spin_button_new_with_range(1, 65535, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w_port), cur.port);
  gtk_grid_attach(GTK_GRID(grid), w_port, 5, row, 2, 1);
  row++;
  lbl = gtk_label_new("Your call sign");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  w_call = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(w_call), cur.callsign);
  gtk_entry_set_width_chars(GTK_ENTRY(w_call), 12);
  gtk_grid_attach(GTK_GRID(grid), w_call, 1, row, 4, 1);
  row++;
  /* Behaviour checkboxes */
  w_enable     = gtk_check_button_new_with_label("Enable DX cluster");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_enable),     cur.enabled);
  w_show_pan   = gtk_check_button_new_with_label("Show spots");
  w_auto_recon = gtk_check_button_new_with_label("Auto reconnect");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_show_pan),   cur.show_on_panadapter);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_auto_recon), cur.auto_reconnect);
  gtk_grid_attach(GTK_GRID(grid), w_enable,     1, row, 2, 1);
  gtk_grid_attach(GTK_GRID(grid), w_show_pan,   3, row, 2, 1);
  gtk_grid_attach(GTK_GRID(grid), w_auto_recon, 5, row, 2, 1);
  row++;
  /* Age limit (radio buttons) */
  lbl = gtk_label_new("Spot age limit");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  w_age_5  = gtk_radio_button_new_with_label(NULL, "5 min");
  w_age_10 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w_age_5),  "10 min");
  w_age_30 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w_age_5),  "30 min");
  w_age_60 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w_age_5),  "1 hour");
  switch (cur.age_limit_sec) {
  case 300:
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_age_5), TRUE);
    break;
  case 1800:
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_age_30), TRUE);
    break;
  case 3600:
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_age_60), TRUE);
    break;
  default:
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_age_10), TRUE);
    break;
  }
  gtk_grid_attach(GTK_GRID(grid), w_age_5,  1, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_age_10, 2, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_age_30, 3, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_age_60, 4, row, 1, 1);
  row++;
  /* Modes */
  lbl = gtk_label_new("Modes");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  w_m_ft8   = gtk_check_button_new_with_label("FT8");
  w_m_ft4   = gtk_check_button_new_with_label("FT4");
  w_m_cw    = gtk_check_button_new_with_label("CW");
  w_m_ssb   = gtk_check_button_new_with_label("SSB");
  w_m_rtty  = gtk_check_button_new_with_label("RTTY");
  w_m_other = gtk_check_button_new_with_label("Other");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_m_ft8),   cur.mode_ft8);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_m_ft4),   cur.mode_ft4);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_m_cw),    cur.mode_cw);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_m_ssb),   cur.mode_ssb);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_m_rtty),  cur.mode_rtty);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_m_other), cur.mode_other);
  gtk_grid_attach(GTK_GRID(grid), w_m_ft8,   1, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_m_ft4,   2, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_m_cw,    3, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_m_ssb,   4, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_m_rtty,  5, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_m_other, 6, row, 1, 1);
  row++;
  row++;
  /* Regions */
  lbl = gtk_label_new("Spotter regions");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  w_r_na = gtk_check_button_new_with_label("N. America");
  w_r_eu = gtk_check_button_new_with_label("Europe");
  w_r_as = gtk_check_button_new_with_label("Asia");
  w_r_sa = gtk_check_button_new_with_label("S. America");
  w_r_af = gtk_check_button_new_with_label("Africa");
  w_r_oc = gtk_check_button_new_with_label("Oceania");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_r_na), cur.region_na);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_r_eu), cur.region_eu);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_r_as), cur.region_as);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_r_sa), cur.region_sa);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_r_af), cur.region_af);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_r_oc), cur.region_oc);
  gtk_grid_attach(GTK_GRID(grid), w_r_na, 1, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_r_eu, 2, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_r_as, 3, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_r_sa, 4, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_r_af, 5, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), w_r_oc, 6, row, 1, 1);
  row++;
  /* Whitelist / blacklist */
  lbl = gtk_label_new("Whitelist");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  w_whitelist = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(w_whitelist), cur.whitelist);
  gtk_entry_set_placeholder_text(GTK_ENTRY(w_whitelist), "VK,ZL,YB");
  gtk_grid_attach(GTK_GRID(grid), w_whitelist, 1, row, 6, 1);
  row++;
  lbl = gtk_label_new("Blacklist");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  w_blacklist = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(w_blacklist), cur.blacklist);
  gtk_grid_attach(GTK_GRID(grid), w_blacklist, 1, row, 6, 1);
  /* Action buttons */
  apply_btn = gtk_button_new_with_label("Apply&Save");
  GtkWidget *close_btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_btn, "close_button");
  g_signal_connect(apply_btn,  "clicked", G_CALLBACK(on_save_clicked), NULL);
  g_signal_connect(close_btn, "button-press-event", G_CALLBACK(on_close_pressed), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_btn, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), apply_btn,  1, 0, 2, 1);
  sub_menu = dialog;
  active_menu = NO_MENU;
  button_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 250, button_colour_timer, NULL, NULL);
  gtk_widget_show_all(dialog);
}
