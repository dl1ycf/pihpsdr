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
#include <time.h>

#include "band.h"
#include "dxcluster.h"
#include "dxcluster_db.h"
#include "dxcluster_history_menu.h"
#include "new_menu.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "message.h"

static GtkWidget   *dialog = NULL;
static GtkListStore *store = NULL;
static GtkTreeView *tree_view = NULL;
static GtkWidget   *search_entry, *date_combo, *band_combo, *mode_combo;
static GtkWidget   *summary_lbl;

#define MAX_RESULTS 2000

enum {
  COL_TIME,    /* "HH:MM:SS UTC dd/mm" */
  COL_FREQ,    /* "14076.0" */
  COL_MODE,
  COL_DXCALL,
  COL_SPOTTER,
  COL_COMMENT,
  COL_TIME_TS,    /* hidden: raw unix timestamp */
  COL_FREQ_HZ,    /* hidden: raw freq in Hz   */
  COL_COUNT
};

static void cleanup(void) {
  if (dialog) {
    GtkWidget *d = dialog;
    dialog = NULL;
    gtk_widget_destroy(d);
    sub_menu = NULL;
    active_menu = NO_MENU;
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

/* Build a DXC_DB_QUERY from current UI state */
static void build_query(DXC_DB_QUERY *q, char *mode_buf, int mode_buf_len) {
  memset(q, 0, sizeof(*q));
  q->max_results = MAX_RESULTS;
  const char *search = gtk_entry_get_text(GTK_ENTRY(search_entry));
  q->callsign_substring = (search && search[0]) ? search : NULL;
  /* Date range */
  time_t now = time(NULL);
  int date_choice = gtk_combo_box_get_active(GTK_COMBO_BOX(date_combo));
  switch (date_choice) {
  case 0:
    q->since = now - 86400;
    break;     /* today/last 24h */
  case 1:
    q->since = now - 7  * 86400;
    break;     /* last 7 days    */
  case 2:
    q->since = now - 30 * 86400;
    break;     /* last 30 days   */
  default:
    q->since = 0;
    break;
  }
  /* Mode */
  int mode_choice = gtk_combo_box_get_active(GTK_COMBO_BOX(mode_combo));
  mode_buf[0] = '\0';
  switch (mode_choice) {
  case 1:
    snprintf(mode_buf, mode_buf_len, "FT8");
    break;
  case 2:
    snprintf(mode_buf, mode_buf_len, "FT4");
    break;
  case 3:
    snprintf(mode_buf, mode_buf_len, "CW");
    break;
  case 4:
    snprintf(mode_buf, mode_buf_len, "SSB");
    break;
  case 5:
    snprintf(mode_buf, mode_buf_len, "RTTY");
    break;
  default:
    break;
  }
  q->mode = mode_buf[0] ? mode_buf : NULL;
  /* Band */
  int band_choice = gtk_combo_box_get_active(GTK_COMBO_BOX(band_combo));
  if (band_choice == 1 && active_receiver) {
    /* current band */
    int b = get_band_from_frequency(vfo[active_receiver->id].frequency);
    const BAND *band = band_get_band(b);
    if (band) {
      q->band_lo_hz = band->frequencyMin;
      q->band_hi_hz = band->frequencyMax;
    }
  }
}

static void refresh_list(void) {
  if (!store) { return; }
  gtk_list_store_clear(store);
  char mode_buf[16];
  DXC_DB_QUERY q;
  build_query(&q, mode_buf, sizeof(mode_buf));
  DX_SPOT *rows = g_malloc(sizeof(DX_SPOT) * MAX_RESULTS);
  int n = dxcluster_db_query(&q, rows, MAX_RESULTS);
  for (int i = 0; i < n; i++) {
    struct tm tm_utc;
    gmtime_r(&rows[i].when, &tm_utc);
    char tbuf[24], fbuf[16];
    strftime(tbuf, sizeof(tbuf), "%H:%M  %d/%m", &tm_utc);
    snprintf(fbuf, sizeof(fbuf), "%.1f", rows[i].freq_hz / 1000.0);
    GtkTreeIter it;
    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
                       COL_TIME,    tbuf,
                       COL_FREQ,    fbuf,
                       COL_MODE,    rows[i].mode,
                       COL_DXCALL,  rows[i].dx_call,
                       COL_SPOTTER, rows[i].spotter,
                       COL_COMMENT, rows[i].comment,
                       COL_TIME_TS, (gint64)rows[i].when,
                       COL_FREQ_HZ, (gint64)rows[i].freq_hz,
                       -1);
  }
  g_free(rows);
  int total = dxcluster_db_count_total();
  int matching = dxcluster_db_count_matching(&q);
  char sbuf[64];
  snprintf(sbuf, sizeof(sbuf), "%d match · %d total", matching, total);
  gtk_label_set_text(GTK_LABEL(summary_lbl), sbuf);
}

//cppcheck-suppress constParameterCallback
static void on_filter_changed(GtkWidget *w, gpointer data) {
  refresh_list();
}

static void on_clear_clicked(GtkButton *btn, gpointer data) {
  GtkWidget *dlg = gtk_message_dialog_new(
                     GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
                     GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                     "Permanently delete ALL spot history?\n\n"
                     "This cannot be undone.");
  int resp = gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
  if (resp == GTK_RESPONSE_YES) {
    dxcluster_db_clear_all();
    refresh_list();
  }
}

static void on_export_clicked(GtkButton *btn, gpointer data) {
  GtkWidget *fc = gtk_file_chooser_dialog_new(
                    "Export to CSV", GTK_WINDOW(dialog),
                    GTK_FILE_CHOOSER_ACTION_SAVE,
                    "_Cancel", GTK_RESPONSE_CANCEL,
                    "_Save",   GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
  gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), "dxspots.csv");
  if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
    char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    char mode_buf[16];
    DXC_DB_QUERY q;
    build_query(&q, mode_buf, sizeof(mode_buf));
    q.max_results = 1000000;   /* effectively unlimited for export */
    if (dxcluster_db_export_csv(&q, path) == 0) {
      t_print("dxcluster: exported to %s\n", path);
    }
    g_free(path);
  }
  gtk_widget_destroy(fc);
}

static void on_tune_clicked(GtkButton *btn, gpointer data) {
  if (!active_receiver) { return; }
  GtkTreeSelection *sel = gtk_tree_view_get_selection(tree_view);
  GtkTreeIter it;
  GtkTreeModel *mod = NULL;
  if (!gtk_tree_selection_get_selected(sel, &mod, &it)) { return; }
  gint64 freq_hz = 0;
  gtk_tree_model_get(mod, &it, COL_FREQ_HZ, &freq_hz, -1);
  if (freq_hz > 0) {
    vfo_id_set_frequency(active_receiver->id, (long long)freq_hz);
  }
}

static void on_row_activated(GtkTreeView *tv, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer data) {
  if (!active_receiver) { return; }
  GtkTreeIter it;
  GtkTreeModel *mod = gtk_tree_view_get_model(tv);
  if (!gtk_tree_model_get_iter(mod, &it, path)) { return; }
  gint64 freq_hz = 0;
  gtk_tree_model_get(mod, &it, COL_FREQ_HZ, &freq_hz, -1);
  if (freq_hz > 0) {
    vfo_id_set_frequency(active_receiver->id, (long long)freq_hz);
  }
}

void dxcluster_history_menu(GtkWidget *parent) {
  if (dialog) { cleanup(); }
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *hb = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(hb), "DX Spot History");
  gtk_window_set_titlebar(GTK_WINDOW(dialog), hb);
  g_signal_connect(dialog, "delete-event", G_CALLBACK(on_delete_event), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_container_add(GTK_CONTAINER(content), vbox);
  /* Close button row */
  GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *export_b = gtk_button_new_with_label("Export CSV");
  GtkWidget *clear_b  = gtk_button_new_with_label("Clear History");
  GtkWidget *tune_b   = gtk_button_new_with_label("Tune Selected");
  GtkWidget *close_b  = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  gtk_widget_set_name(tune_b, "boldlabel");
  g_signal_connect(export_b, "clicked", G_CALLBACK(on_export_clicked), NULL);
  g_signal_connect(clear_b,  "clicked", G_CALLBACK(on_clear_clicked),  NULL);
  g_signal_connect(tune_b,   "clicked", G_CALLBACK(on_tune_clicked),   NULL);
  g_signal_connect(close_b,  "button-press-event", G_CALLBACK(on_close_pressed), NULL);
  gtk_box_pack_start(GTK_BOX(btn_row), close_b,  FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(btn_row), tune_b,   FALSE, FALSE, 0);
  GtkWidget *spacer = gtk_label_new("");
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(btn_row), spacer,   TRUE,  TRUE,  0);
  gtk_box_pack_start(GTK_BOX(btn_row), export_b, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(btn_row), clear_b,  FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), btn_row, FALSE, FALSE, 0);
  /* Toolbar */
  GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  search_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search call sign");
  gtk_entry_set_width_chars(GTK_ENTRY(search_entry), 16);
  g_signal_connect(search_entry, "changed", G_CALLBACK(on_filter_changed), NULL);
  gtk_box_pack_start(GTK_BOX(toolbar), search_entry, FALSE, FALSE, 0);
  date_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(date_combo), "Last 24h");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(date_combo), "Last 7 days");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(date_combo), "Last 30 days");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(date_combo), "All available");
  gtk_combo_box_set_active(GTK_COMBO_BOX(date_combo), 1);
  g_signal_connect(date_combo, "changed", G_CALLBACK(on_filter_changed), NULL);
  gtk_box_pack_start(GTK_BOX(toolbar), date_combo, FALSE, FALSE, 0);
  band_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(band_combo), "All bands");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(band_combo), "Current band");
  gtk_combo_box_set_active(GTK_COMBO_BOX(band_combo), 0);
  g_signal_connect(band_combo, "changed", G_CALLBACK(on_filter_changed), NULL);
  gtk_box_pack_start(GTK_BOX(toolbar), band_combo, FALSE, FALSE, 0);
  mode_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "All modes");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FT8");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FT4");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "SSB");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "RTTY");
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 0);
  g_signal_connect(mode_combo, "changed", G_CALLBACK(on_filter_changed), NULL);
  gtk_box_pack_start(GTK_BOX(toolbar), mode_combo, FALSE, FALSE, 0);
  summary_lbl = gtk_label_new("");
  gtk_widget_set_hexpand(summary_lbl, TRUE);
  gtk_widget_set_halign(summary_lbl, GTK_ALIGN_END);
  gtk_box_pack_start(GTK_BOX(toolbar), summary_lbl, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
  /* The list view */
  store = gtk_list_store_new(COL_COUNT,
                             G_TYPE_STRING,  /* time   */
                             G_TYPE_STRING,  /* freq   */
                             G_TYPE_STRING,  /* mode   */
                             G_TYPE_STRING,  /* call   */
                             G_TYPE_STRING,  /* spotter*/
                             G_TYPE_STRING,  /* comment*/
                             G_TYPE_INT64,   /* time_ts*/
                             G_TYPE_INT64);  /* freq_hz*/
  GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  tree_view = GTK_TREE_VIEW(tv);
  gtk_tree_view_set_grid_lines(tree_view, GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
  g_signal_connect(tv, "row-activated", G_CALLBACK(on_row_activated), NULL);
  struct { const char *title; int col; int width; } cols[] = {
    { "Time (UTC)", COL_TIME,    110 },
    { "Freq (kHz)", COL_FREQ,     90 },
    { "Mode",       COL_MODE,     60 },
    { "DX",         COL_DXCALL,  100 },
    { "Spotter",    COL_SPOTTER, 100 },
    { "Comment",    COL_COMMENT, 300 },
    { NULL, 0, 0 }
  };
  for (int i = 0; cols[i].title; i++) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
                             cols[i].title, r, "text", cols[i].col, NULL);
    gtk_tree_view_column_set_fixed_width(c, cols[i].width);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_append_column(tree_view, c);
  }
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(scroll, 780, 320);
  gtk_container_add(GTK_CONTAINER(scroll), tv);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
  refresh_list();
  sub_menu = dialog;
  active_menu = NO_MENU;
  gtk_widget_show_all(dialog);
}
