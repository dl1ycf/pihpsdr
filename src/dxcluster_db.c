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

#include "dxcluster_db.h"
#include "message.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sqlite3.h>

static sqlite3        *db = NULL;
static pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *SCHEMA =
  "CREATE TABLE IF NOT EXISTS spots ("
  "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "  when_ts INTEGER NOT NULL, "
  "  freq_hz INTEGER NOT NULL, "
  "  dx_call TEXT NOT NULL, "
  "  spotter TEXT, "
  "  mode TEXT, "
  "  comment TEXT"
  ");"
  "CREATE INDEX IF NOT EXISTS idx_when ON spots(when_ts);"
  "CREATE INDEX IF NOT EXISTS idx_call ON spots(dx_call);"
  "CREATE INDEX IF NOT EXISTS idx_mode ON spots(mode);";

static int prune_old_rows(void) {
  if (!db) { return -1; }
  time_t cutoff = time(NULL) - (DXC_DB_RETENTION_DAYS * 86400);
  char sql[128];
  snprintf(sql, sizeof(sql), "DELETE FROM spots WHERE when_ts < %ld", (long)cutoff);
  char *err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (err) {
    t_print("dxcluster_db: prune error: %s\n", err);
    sqlite3_free(err);
  }
  return rc;
}

int dxcluster_db_init(void) {
  pthread_mutex_lock(&db_lock);
  if (db) { pthread_mutex_unlock(&db_lock); return 0; }
  if (sqlite3_open("dxhistory.db", &db) != SQLITE_OK) {
    t_print("dxcluster_db: failed to open dxhistory.db: %s\n", sqlite3_errmsg(db));
    if (db) { sqlite3_close(db); db = NULL; }
    pthread_mutex_unlock(&db_lock);
    return -1;
  }
  char *err = NULL;
  sqlite3_exec(db, SCHEMA, NULL, NULL, &err);
  if (err) {
    t_print("dxcluster_db: schema error: %s\n", err);
    sqlite3_free(err);
  }
  /* WAL gives better concurrent read-while-writing behaviour */
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
  sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
  prune_old_rows();
  pthread_mutex_unlock(&db_lock);
  t_print("dxcluster_db: opened database\n");
  return 0;
}

void dxcluster_db_shutdown(void) {
  pthread_mutex_lock(&db_lock);
  if (db) {
    sqlite3_close(db);
    db = NULL;
  }
  pthread_mutex_unlock(&db_lock);
}

void dxcluster_db_insert(const DX_SPOT *spot) {
  if (!spot) { return; }
  pthread_mutex_lock(&db_lock);
  if (!db) { pthread_mutex_unlock(&db_lock); return; }
  sqlite3_stmt *stmt = NULL;
  const char *sql =
    "INSERT INTO spots (when_ts, freq_hz, dx_call, spotter, mode, comment) "
    "VALUES (?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)spot->when);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)spot->freq_hz);
    sqlite3_bind_text (stmt, 3, spot->dx_call, -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 4, spot->spotter, -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 5, spot->mode,    -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 6, spot->comment, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  pthread_mutex_unlock(&db_lock);
}

/* Build a parameterised SELECT from the query struct. Returns prepared stmt or NULL. */
static sqlite3_stmt *build_query_stmt(const DXC_DB_QUERY *q, int count_only) {
  if (!db || !q) { return NULL; }
  char sql[1024];
  if (count_only) {
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM spots WHERE 1=1");
  } else {
    snprintf(sql, sizeof(sql),
             "SELECT when_ts, freq_hz, dx_call, spotter, mode, comment "
             "FROM spots WHERE 1=1");
  }
  size_t pos = strlen(sql);
  if (q->since > 0) {
    pos += snprintf(sql + pos, sizeof(sql) - pos, " AND when_ts >= ?");
  }
  if (q->until > 0) {
    pos += snprintf(sql + pos, sizeof(sql) - pos, " AND when_ts <= ?");
  }
  if (q->callsign_substring && q->callsign_substring[0]) {
    pos += snprintf(sql + pos, sizeof(sql) - pos, " AND dx_call LIKE ?");
  }
  if (q->mode && q->mode[0]) {
    pos += snprintf(sql + pos, sizeof(sql) - pos, " AND mode = ?");
  }
  if (q->band_lo_hz > 0) {
    pos += snprintf(sql + pos, sizeof(sql) - pos, " AND freq_hz >= ?");
  }
  if (q->band_hi_hz > 0) {
    pos += snprintf(sql + pos, sizeof(sql) - pos, " AND freq_hz <= ?");
  }
  if (!count_only) {
    snprintf(sql + pos, sizeof(sql) - pos, " ORDER BY when_ts DESC LIMIT %d",
             q->max_results > 0 ? q->max_results : 1000);
  }
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    t_print("dxcluster_db: prepare failed: %s\n", sqlite3_errmsg(db));
    return NULL;
  }
  int i = 1;
  if (q->since > 0) {
    sqlite3_bind_int64(stmt, i++, (sqlite3_int64)q->since);
  }
  if (q->until > 0) {
    sqlite3_bind_int64(stmt, i++, (sqlite3_int64)q->until);
  }
  if (q->callsign_substring && q->callsign_substring[0]) {
    char like[64];
    snprintf(like, sizeof(like), "%%%s%%", q->callsign_substring);
    sqlite3_bind_text(stmt, i++, like, -1, SQLITE_TRANSIENT);
  }
  if (q->mode && q->mode[0]) {
    sqlite3_bind_text(stmt, i++, q->mode, -1, SQLITE_TRANSIENT);
  }
  if (q->band_lo_hz > 0) {
    sqlite3_bind_int64(stmt, i++, (sqlite3_int64)q->band_lo_hz);
  }
  if (q->band_hi_hz > 0) {
    sqlite3_bind_int64(stmt, i++, (sqlite3_int64)q->band_hi_hz);
  }
  return stmt;
}

int dxcluster_db_query(const DXC_DB_QUERY *q, DX_SPOT *out, int out_max) {
  int count = 0;
  pthread_mutex_lock(&db_lock);
  if (!db) { pthread_mutex_unlock(&db_lock); return 0; }
  sqlite3_stmt *stmt = build_query_stmt(q, 0);
  if (!stmt) { pthread_mutex_unlock(&db_lock); return 0; }
  while (sqlite3_step(stmt) == SQLITE_ROW && count < out_max) {
    DX_SPOT *sp = &out[count++];
    memset(sp, 0, sizeof(*sp));
    sp->when    = (time_t)sqlite3_column_int64(stmt, 0);
    sp->freq_hz = sqlite3_column_int64(stmt, 1);
    const char *txt;
    if ((txt = (const char *)sqlite3_column_text(stmt, 2))) {
      snprintf(sp->dx_call, sizeof(sp->dx_call), "%s", txt);
    }
    if ((txt = (const char *)sqlite3_column_text(stmt, 3))) {
      snprintf(sp->spotter, sizeof(sp->spotter), "%s", txt);
    }
    if ((txt = (const char *)sqlite3_column_text(stmt, 4))) {
      snprintf(sp->mode,    sizeof(sp->mode),    "%s", txt);
    }
    if ((txt = (const char *)sqlite3_column_text(stmt, 5))) {
      snprintf(sp->comment, sizeof(sp->comment), "%s", txt);
    }
  }
  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&db_lock);
  return count;
}

int dxcluster_db_count_total(void) {
  int n = 0;
  pthread_mutex_lock(&db_lock);
  if (db) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM spots", -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
    }
  }
  pthread_mutex_unlock(&db_lock);
  return n;
}

int dxcluster_db_count_matching(const DXC_DB_QUERY *q) {
  int n = 0;
  pthread_mutex_lock(&db_lock);
  if (db) {
    sqlite3_stmt *stmt = build_query_stmt(q, 1);
    if (stmt) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
    }
  }
  pthread_mutex_unlock(&db_lock);
  return n;
}

int dxcluster_db_export_csv(const DXC_DB_QUERY *q, const char *path) {
  if (!path) { return -1; }
  FILE *f = fopen(path, "w");
  if (!f) { return -1; }
  fprintf(f, "when_utc,freq_hz,dx_call,spotter,mode,comment\n");
  pthread_mutex_lock(&db_lock);
  if (db) {
    sqlite3_stmt *stmt = build_query_stmt(q, 0);
    if (stmt) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        long when = (long)sqlite3_column_int64(stmt, 0);
        long long freq = sqlite3_column_int64(stmt, 1);
        const char *call    = (const char *)sqlite3_column_text(stmt, 2);
        const char *spotter = (const char *)sqlite3_column_text(stmt, 3);
        const char *mode    = (const char *)sqlite3_column_text(stmt, 4);
        const char *comment = (const char *)sqlite3_column_text(stmt, 5);
        fprintf(f, "%ld,%lld,%s,%s,%s,\"%s\"\n",
                when, freq,
                call ? call : "", spotter ? spotter : "",
                mode ? mode : "", comment ? comment : "");
      }
      sqlite3_finalize(stmt);
    }
  }
  pthread_mutex_unlock(&db_lock);
  fclose(f);
  return 0;
}

void dxcluster_db_clear_all(void) {
  pthread_mutex_lock(&db_lock);
  if (db) {
    char *err = NULL;
    sqlite3_exec(db, "DELETE FROM spots", NULL, NULL, &err);
    if (err) { sqlite3_free(err); }
    sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
  }
  pthread_mutex_unlock(&db_lock);
}
