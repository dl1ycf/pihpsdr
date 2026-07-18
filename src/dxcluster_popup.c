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

#include "dxcluster_popup.h"
#include "band.h"
#include "main.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "message.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Tune to the spot's frequency on the active receiver. If the spot is on
 * a different band, ask first. */
static void tune_to_spot(const DX_SPOT *spot) {
  if (!spot || !active_receiver) { return; }
  long long target = spot->freq_hz;
  int target_band = get_band_from_frequency(target);
  int active_id = active_receiver->id;
  int current_band = get_band_from_frequency(vfo[active_id].frequency);
  if (target_band != current_band) {
    GtkWidget *dlg = gtk_message_dialog_new(
                       GTK_WINDOW(top_window), GTK_DIALOG_MODAL,
                       GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                       "%s is on a different band.\n\nSwitch band and tune?",
                       spot->dx_call);
    gtk_window_set_title(GTK_WINDOW(dlg), "Switch band?");
    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (resp != GTK_RESPONSE_YES) { return; }
    vfo_id_band_changed(active_id, target_band);
  }
  vfo_id_set_frequency(active_id, target);
  t_print("dxcluster: tuned to %s on %lld Hz\n", spot->dx_call, target);
}

/* ── Single-spot popup ──────────────────────────────────────────────── */

typedef struct {
  GtkWidget *dialog;
  DX_SPOT    spot;
} POPUP_CTX;

static void on_tune_clicked(GtkButton *btn, gpointer data) {
  POPUP_CTX *ctx = (POPUP_CTX *)data;
  tune_to_spot(&ctx->spot);
  gtk_widget_destroy(ctx->dialog);
}

static void on_close_clicked(GtkButton *btn, gpointer data) {
  POPUP_CTX *ctx = (POPUP_CTX *)data;
  gtk_widget_destroy(ctx->dialog);
}

static void on_popup_destroy(GtkWidget *w, gpointer data) {
  g_free(data);
}

static const char *age_string(time_t when, char *buf, int len) {
  long age = time(NULL) - when;
  if (age < 0 || age > 100000000L) { *buf = 0; }
  else if (age < 60) { snprintf(buf, len, "%ld sec",  age); }
  else if (age < 3600) { snprintf(buf, len, "%ld min",  age / 60); }
  else { snprintf(buf, len, "%ld hr",   age / 3600); }
  return buf;
}

void dxcluster_popup_show_single(const DX_SPOT *spot, int parent_x, int parent_y) {
  if (!spot) { return; }
  POPUP_CTX *ctx = g_new0(POPUP_CTX, 1);
  ctx->spot = *spot;
  GtkWidget *dlg = gtk_dialog_new();
  ctx->dialog = dlg;
  gtk_window_set_title(GTK_WINDOW(dlg), "DX Spot");
  gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(top_window));
  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_MOUSE);
  gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
  if (parent_x > 0 && parent_y > 0) {
    gtk_window_move(GTK_WINDOW(dlg), parent_x, parent_y);
  }
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
  /* Big callsign at top */
  char buf[128];
  snprintf(buf, sizeof(buf),
           "<span size='x-large' weight='bold'>%s</span>", spot->dx_call);
  GtkWidget *call_l = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(call_l), buf);
  gtk_widget_set_halign(call_l, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), call_l, 0, 0, 2, 1);
  /* Frequency and mode */
  snprintf(buf, sizeof(buf), "%.1f kHz · %s",
           spot->freq_hz / 1000.0,
           spot->mode[0] ? spot->mode : "—");
  GtkWidget *freq_l = gtk_label_new(buf);
  gtk_widget_set_halign(freq_l, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), freq_l, 0, 1, 2, 1);
  /* Detail rows */
  int row = 2;
  struct tm tm_utc;
  time_t when = spot->when;
  gmtime_r(&when, &tm_utc);
  char tstr[16];
  strftime(tstr, sizeof(tstr), "%H:%M:%SZ", &tm_utc);
  char age_buf[16];
  age_string(spot->when, age_buf, sizeof(age_buf));
  const struct { const char *label; const char *value; } rows[] = {
    { "Spotter",       spot->spotter },
    { "Time (UTC)",    tstr          },
    { "Age",           age_buf       },
    { "Comment",       spot->comment[0] ? spot->comment : "—" },
    { NULL, NULL }
  };
  for (int i = 0; rows[i].label; i++) {
    GtkWidget *l = gtk_label_new(rows[i].label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    GtkWidget *v = gtk_label_new(rows[i].value);
    gtk_widget_set_halign(v, GTK_ALIGN_END);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v, 1, row, 1, 1);
    row++;
  }
  gtk_container_add(GTK_CONTAINER(content), grid);
  /* Action buttons */
  GtkWidget *tune_btn  = gtk_button_new_with_label("Tune to spot");
  GtkWidget *close_btn = gtk_button_new_with_label("Close");
  gtk_dialog_add_action_widget(GTK_DIALOG(dlg), close_btn, GTK_RESPONSE_NONE);
  gtk_dialog_add_action_widget(GTK_DIALOG(dlg), tune_btn,  GTK_RESPONSE_NONE);
  g_signal_connect(tune_btn,  "clicked", G_CALLBACK(on_tune_clicked),  ctx);
  g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_clicked), ctx);
  g_signal_connect(dlg, "destroy", G_CALLBACK(on_popup_destroy), ctx);
  gtk_widget_show_all(dlg);
}

/* ── Group popup (stacked spots) ─────────────────────────────────────── */

typedef struct {
  GtkWidget *dialog;
  DX_SPOT    spot;
} GROUP_ROW_CTX;

static void on_group_tune_clicked(GtkButton *btn, gpointer data) {
  GROUP_ROW_CTX *ctx = (GROUP_ROW_CTX *)data;
  tune_to_spot(&ctx->spot);
  gtk_widget_destroy(ctx->dialog);
}

static void on_group_row_destroy(GtkWidget *w, gpointer data) {
  g_free(data);
}

void dxcluster_popup_show_group(const DX_SPOT *spots, int n_spots,
                                int parent_x, int parent_y) {
  if (!spots || n_spots <= 0) { return; }
  if (n_spots == 1) {
    dxcluster_popup_show_single(&spots[0], parent_x, parent_y);
    return;
  }
  GtkWidget *dlg = gtk_dialog_new();
  char title_buf[64];
  snprintf(title_buf, sizeof(title_buf), "%d spots", n_spots);
  gtk_window_set_title(GTK_WINDOW(dlg), title_buf);
  gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(top_window));
  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_MOUSE);
  gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
  if (parent_x > 0 && parent_y > 0) {
    gtk_window_move(GTK_WINDOW(dlg), parent_x, parent_y);
  }
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 14);
  for (int i = 0; i < n_spots; i++) {
    const DX_SPOT *sp = &spots[i];
    char freq_buf[24], info_buf[64];
    snprintf(freq_buf, sizeof(freq_buf), "%.1f", sp->freq_hz / 1000.0);
    snprintf(info_buf, sizeof(info_buf), "%s  %s",
             sp->mode[0] ? sp->mode : "",
             sp->comment[0] ? sp->comment : "");
    GtkWidget *call_l = gtk_label_new(NULL);
    char markup[64];
    snprintf(markup, sizeof(markup), "<b>%s</b>", sp->dx_call);
    gtk_label_set_markup(GTK_LABEL(call_l), markup);
    gtk_widget_set_halign(call_l, GTK_ALIGN_START);
    GtkWidget *freq_l = gtk_label_new(freq_buf);
    gtk_widget_set_halign(freq_l, GTK_ALIGN_END);
    GtkWidget *info_l = gtk_label_new(info_buf);
    gtk_widget_set_halign(info_l, GTK_ALIGN_START);
    GtkWidget *tune_btn = gtk_button_new_with_label("Tune");
    GROUP_ROW_CTX *ctx = g_new0(GROUP_ROW_CTX, 1);
    ctx->dialog = dlg;
    ctx->spot   = *sp;
    g_signal_connect(tune_btn, "clicked",
                     G_CALLBACK(on_group_tune_clicked), ctx);
    g_signal_connect(tune_btn, "destroy",
                     G_CALLBACK(on_group_row_destroy), ctx);
    gtk_grid_attach(GTK_GRID(grid), call_l,   0, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), freq_l,   1, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), info_l,   2, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tune_btn, 3, i, 1, 1);
  }
  gtk_container_add(GTK_CONTAINER(content), grid);
  GtkWidget *close_btn = gtk_button_new_with_label("Close");
  gtk_dialog_add_action_widget(GTK_DIALOG(dlg), close_btn, GTK_RESPONSE_CLOSE);
  g_signal_connect_swapped(close_btn, "clicked",
                           G_CALLBACK(gtk_widget_destroy), dlg);
  gtk_widget_show_all(dlg);
}
