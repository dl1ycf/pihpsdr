/* dxcluster_db.h — SQLite-backed history database for DX cluster spots
 *
 * Stores every spot ever received for the configured retention period
 * (default 30 days). The panadapter ring buffer holds only ~500 spots
 * for fast in-memory access; this DB is for the searchable history viewer.
 *
 * Pruning of old records happens once at init time.
 */
#ifndef _DXCLUSTER_DB_H_
#define _DXCLUSTER_DB_H_

#include "dxcluster.h"

#define DXC_DB_RETENTION_DAYS  30

/* Lifecycle */
int   dxcluster_db_init(void);
void  dxcluster_db_shutdown(void);

/* Insert a single spot. Thread-safe. Best-effort: errors logged, not returned. */
void  dxcluster_db_insert(const DX_SPOT *spot);

/* Query parameters for the history viewer.
 *   - `since` and `until` are Unix timestamps (0 = no bound)
 *   - `callsign_substring` is matched case-insensitively against dx_call
 *     (NULL or empty = no filter)
 *   - `mode` is one of "" (all), "FT8", "CW", "SSB", etc.
 *   - `band_lo_hz` / `band_hi_hz` (0 = no bound)
 *   - Results returned newest-first, up to max_results
 */
typedef struct {
  time_t      since;
  time_t      until;
  const char *callsign_substring;
  const char *mode;
  long long   band_lo_hz;
  long long   band_hi_hz;
  int         max_results;
} DXC_DB_QUERY;

int   dxcluster_db_query(const DXC_DB_QUERY *q, DX_SPOT *out, int out_max);

/* Total + matching row counts, populated together for the toolbar status. */
int   dxcluster_db_count_total(void);
int   dxcluster_db_count_matching(const DXC_DB_QUERY *q);

/* Export query results to a CSV file at the given path. Returns 0 on success. */
int   dxcluster_db_export_csv(const DXC_DB_QUERY *q, const char *path);

/* Clear ALL history. Used by the "Clear History" button. */
void  dxcluster_db_clear_all(void);

#endif /* _DXCLUSTER_DB_H_ */
