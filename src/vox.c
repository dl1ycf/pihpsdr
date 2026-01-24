/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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

#include "radio.h"
#include "transmitter.h"
#include "vox.h"
#include "vfo.h"
#include "ext.h"

static guint vox_timeout = 0;

static double peak = 0.0;

static int vox_timeout_cb(gpointer data) {
  //
  // First set vox_timeout to zero (via vox_cancel())
  // indicating no "hanging" timeout
  // then, remove VOX and update display
  //
  vox_cancel();
  g_idle_add(ext_radio_set_vox, GINT_TO_POINTER(0));
  g_idle_add(ext_vfo_update, NULL);
  return FALSE;
}

double vox_get_peak(void) {
  double result = peak;
  return result;
}

void vox_clear(void) {
  peak = 0.0;
}

void vox_update(double lvl) {
  peak = lvl;

  //
  // As long as a client controls us, VOX is done there
  //
  if (remoteclient.running) { return; }

  if (!can_transmit) { return; }

  if (vox_enabled && !mox && !transmitter->tune && !TxInhibit) {
    if (peak > vox_threshold) {
      // we use the value of vox_timeout to determine whether
      // the time-out is "hanging". We cannot use the value of vox
      // since this may be set with a delay, and we MUST NOT miss
      // a "hanging" timeout. Note that if a time-out fires, vox_timeout
      // is set to zero.
      if (vox_timeout > 0) {
        g_source_remove(vox_timeout);
        vox_timeout = 0;
      } else {
        //
        // no hanging time-out, assume that we just fired VOX
        //
        g_idle_add(ext_radio_set_vox, GINT_TO_POINTER(1));
        g_idle_add(ext_vfo_update, NULL);
      }

      // re-init "vox hang" time
      vox_timeout = g_timeout_add((int)vox_hang, vox_timeout_cb, NULL);
    }

    // if peak is not above threshold, do nothing (this shall be done later in the timeout event
  }
}

//
// If no vox time-out is hanging, this function is a no-op
//
void vox_cancel(void) {
  if (vox_timeout) {
    g_source_remove(vox_timeout);
    vox_timeout = 0;
  }
}
