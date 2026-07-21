/* Copyright (C)
*  2016 - John Melton, G0ORX/N6LYT
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

#include "radio.h"
#include "transmitter.h"
#include "vox.h"
#include "vfo.h"
#include "ext.h"

static guint vox_timeout = 0;
static guint txrx_timeout = 0;

static double peak = 0.0;

static int vox_timeout_cb(gpointer data) {
  //
  // Remove pending time-outs (vox_cancel)
  // Then, schedule "clear vox" with delay
  //
  vox_cancel();
  // ext_radio_clear_vox() sets txrx_timeout to zero
  txrx_timeout = g_timeout_add(ptt_delay, ext_radio_clear_vox, &txrx_timeout);
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
      int have_vox = 0;
      //
      // Cancel a RX/TX transition that has been scheduled by vox_timeout_cb
      // but not yet been processed
      if (txrx_timeout != 0) {
        g_source_remove(txrx_timeout);
        txrx_timeout = 0;
        have_vox = 1;
      }
      // we use the value of vox_timeout to determine whether
      // the time-out is "hanging". We cannot use the value of vox
      // since this may be set with a delay, and we MUST NOT miss
      // a "hanging" timeout. Note that if a time-out fires, vox_timeout
      // is set to zero.
      if (vox_timeout > 0) {
        g_source_remove(vox_timeout);
        vox_timeout = 0;
        have_vox = 1;
      }
      if (!have_vox) {
        //
        // No pending txrx_timeout, and no pending vox_timeout:
        // We need to activate vox
        //
        g_idle_add(ext_radio_set_vox, GINT_TO_POINTER(1));
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
  if (txrx_timeout) {
    g_source_remove(txrx_timeout);
    txrx_timeout = 0;
  }
}
