/* Copyright (C)
* 2026 - piHPSDR Modernisation, contribution from AL
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

/*
 * Provides:
 *   - Background TCP/telnet client to a DX cluster server (e.g. VE7CC)
 *   - Login handshake with the user's callsign
 *   - Spot parser handling the common "DX de" line format
 *   - Thread-safe ring buffer of recent spots (last 500)
 *   - Filter-aware query API for the panadapter draw path
 *   - Auto-reconnect with exponential backoff
 *   - Persistence: ring buffer saved to CSV on shutdown, reloaded on startup
 *
 * Spots older than the configured age limit are still kept in the ring
 * buffer, just not returned by the standard query. This lets the user
 * change the age filter at runtime without losing data.
 */
#ifndef _DXCLUSTER_H_
#define _DXCLUSTER_H_

#include <gtk/gtk.h>
#include <time.h>

#define DXC_MAX_SPOTS     500     /* ring buffer size */
#define DXC_CALL_LEN      16
#define DXC_MODE_LEN      8
#define DXC_COMMENT_LEN   40

typedef enum {
  DXC_DISABLED       = 0,
  DXC_DISCONNECTED   = 1,
  DXC_CONNECTING     = 2,
  DXC_CONNECTED      = 3,
  DXC_ERROR          = 4,
} DXC_STATE;

typedef struct {
  long long freq_hz;
  char      dx_call[DXC_CALL_LEN];
  char      spotter[DXC_CALL_LEN];
  char      mode[DXC_MODE_LEN];
  char      comment[DXC_COMMENT_LEN];
  time_t    when;
} DX_SPOT;

typedef struct {
  /* Connection */
  int       enabled;
  char      server[64];
  int       port;
  char      callsign[16];
  int       auto_reconnect;
  /* Display */
  int       show_on_panadapter;
  int       age_limit_sec;        /* 300, 600, 1800, 3600 */
  /* Filters */
  int       mode_ft8;
  int       mode_ft4;
  int       mode_cw;
  int       mode_ssb;
  int       mode_rtty;
  int       mode_other;
  int       band_active_only;
  /* Spotter regions: bitmask of 6 continents */
  int       region_na;
  int       region_eu;
  int       region_as;
  int       region_sa;
  int       region_af;
  int       region_oc;
  /* Optional comma-separated prefix lists */
  char      whitelist[128];
  char      blacklist[128];
} DXC_SETTINGS;

/* Lifecycle */
void        dxcluster_init(void);
void        dxcluster_shutdown(void);

/* Apply current settings — disconnects + reconnects if enabled state or
 * server/port/callsign changed. Cheap if only filters changed. */
void        dxcluster_apply_settings(const DXC_SETTINGS *s);
void        dxcluster_get_settings(DXC_SETTINGS *out);

/* Persistence */
void        dxcluster_restore_state(void);
void        dxcluster_save_state(void);

/* Status snapshot — for status dot, settings dialog "connected" line, etc. */
DXC_STATE   dxcluster_get_state(void);
int         dxcluster_spots_received_this_session(void);
const char *dxcluster_state_label(DXC_STATE s);

/* Panadapter integration.
 *
 * dxcluster_draw_spots() is called from rx_panadapter.c's update routine.
 * It walks the spot store, decides which spots are in the visible window,
 * groups overlapping markers, draws Cairo triangles + callsign labels,
 * and as a side effect caches the on-screen pixel positions of each spot
 * (or spot group) so click hit-testing can find them.
 *
 * dxcluster_hit_test() returns the spot or spot group that was clicked,
 * if any. `*out_n_spots` is set to the number of spots in the group
 * (1 for a single spot, more for a stacked group). `out_spots` should
 * point to at least DXC_MAX_GROUP entries.
 *
 * `rx_id` lets us distinguish multi-receiver views — only the active
 * RX's spots are click-targetable.
 */
#define DXC_MAX_GROUP   16

#include <cairo.h>

void dxcluster_draw_spots(cairo_t *cr, int rx_id,
                          long long frequency,
                          double cAp, double cBp,
                          int width, int height);

int  dxcluster_hit_test(int rx_id, int click_x, int click_y,
                        DX_SPOT *out_spots, int *out_n_spots);

#endif /* _DXCLUSTER_H_ */
