/* Copyright (C)
* 2023 - Rick Koch N1GP
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

//
// This file handles the original (type 1) ANDROMEDA case
//
#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/types.h>

#include "actions.h"
#include "band.h"
#include "ext.h"
#include "new_menu.h"
#include "radio.h"
#include "toolbar.h"
#include "vfo.h"

static int numpad_active = 0;
static int longpress = 0;
static int startstop = 1;
static int shift = 0;

int andromeda_execute_button(int v, int p) {
  //
  // derived from Rick's original ANDROMEDA console ZZZP code
  // the return value is the new shift state
  //
  if (!numpad_active) switch (p) {
    case 21: // Function Switches
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
      schedule_action(tb_actions[tb_function[0]][p - 21], (v == 0) ? PRESSED : RELEASED, 0);
      break;

    case 46: // SDR On
      if (v == 0) {
        if (longpress) {
          longpress = 0;
        } else {
          startstop ^= 1;
          startstop ? radio_protocol_run() : radio_protocol_stop();
        }
      } else if (v == 2) {
        new_menu();
        longpress = 1;
      }

      break;
    }

  if (numpad_active && v == 0) switch (p) {
    case 30: // Band Buttons
      schedule_action(NUMPAD_1, PRESSED, 0);
      break;

    case 31:
      schedule_action(NUMPAD_2, PRESSED, 0);
      break;

    case 32:
      schedule_action(NUMPAD_3, PRESSED, 0);
      break;

    case 33:
      schedule_action(NUMPAD_4, PRESSED, 0);
      break;

    case 34:
      schedule_action(NUMPAD_5, PRESSED, 0);
      break;

    case 35:
      schedule_action(NUMPAD_6, PRESSED, 0);
      break;

    case 36:
      schedule_action(NUMPAD_7, PRESSED, 0);
      break;

    case 37:
      schedule_action(NUMPAD_8, PRESSED, 0);
      break;

    case 38:
      schedule_action(NUMPAD_9, PRESSED, 0);
      break;

    case 39:
      schedule_action(NUMPAD_DEC, PRESSED, 0);
      break;

    case 40:
      schedule_action(NUMPAD_0, PRESSED, 0);
      break;

    case 41: {
      schedule_action(NUMPAD_ENTER, PRESSED, 0);
      numpad_active = 0;
      locked = 0;
    }
    break;

    case 45: {
      schedule_action(NUMPAD_MHZ, PRESSED, 0);
      numpad_active = 0;
      locked = 0;
    }
    } else if (!locked) switch (p) {
    case 1: // Rx1 AF Mute
      if (v == 0) { receiver[0]->mute_radio ^= 1; }

      break;

    case 3: // Rx2 AF Mute
      if (v == 0) { receiver[1]->mute_radio ^= 1; }

      break;

    case 5: // Filter Cut Defaults
      schedule_action(FILTER_CUT_DEFAULT, (v == 0) ? PRESSED : RELEASED, 0);
      break;

    case 7: // Diversity Enable
      if (RECEIVERS == 2 && n_adc > 1) {
        schedule_action(DIV, (v == 0) ? PRESSED : RELEASED, 0);
      }

      break;

    case 9: // RIT/XIT Clear
      schedule_action(RIT_CLEAR, (v == 0) ? PRESSED : RELEASED, 0);
      schedule_action(XIT_CLEAR, (v == 0) ? PRESSED : RELEASED, 0);
      break;

    case 29: // Shift
      if (v == 0) {
        shift ^= 1;
      }

      break;

    case 30: // Band Buttons
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
      if (shift && v == 0) {
        int band = band20;

        if (p == 30) { band = band160; }
        else if (p == 31) { band = band80; }
        else if (p == 32) { band = band60; }
        else if (p == 33) { band = band40; }
        else if (p == 34) { band = band30; }
        else if (p == 35) { band = band20; }
        else if (p == 36) { band = band17; }
        else if (p == 37) { band = band15; }
        else if (p == 38) { band = band12; }
        else if (p == 39) { band = band10; }
        else if (p == 40) { band = band6; }
        else if (p == 41) { band = bandGen; }

        vfo_id_band_changed(active_receiver->id ? VFO_B : VFO_A, band);
        shift = 0;
      } else if (!shift && v == 1) {
        if (p == 30) { start_tx_menu(); }                             // MODE DATA
        else if (p == 31) { schedule_action(MODE_PLUS, PRESSED, 0); } // MODE+
        else if (p == 32) { schedule_action(FILTER_PLUS, PRESSED, 0); } // FILTER+
        else if (p == 33) { radio_change_receivers(receivers == 1 ? 2 : 1); } // RX2
        else if (p == 34) { schedule_action(MODE_MINUS, PRESSED, 0); } // MODE-
        else if (p == 35) { schedule_action(FILTER_MINUS, PRESSED, 0); } // FILTER-
        else if (p == 36) { schedule_action(A_TO_B, PRESSED, 0); }    // A>B
        else if (p == 37) { schedule_action(B_TO_A, PRESSED, 0); }    // B>A
        else if (p == 38) { schedule_action(SPLIT, PRESSED, 0); }     // SPLIT
        else if (p == 39) { schedule_action(NB, PRESSED, 0); }        // U1 (use NB)
        else if (p == 40) { schedule_action(NR, PRESSED, 0); }        // U2 (use NR)
      } else if (p == 41) {
        if (v == 0 || v == 2) {
          numpad_active = 1;
          locked = 1;
          g_idle_add(ext_vfo_update, NULL);
          schedule_action(NUMPAD_CL, PRESSED, 0);               // U3 start Freq entry
        }
      }

      break;

    case 42: // RIT/XIT
      if (v == 0) {
        if (!vfo[active_receiver->id].rit_enabled && !vfo[vfo_get_tx_vfo()].xit_enabled) {
          // neither RIT nor XIT: ==> activate RIT
          vfo_id_rit_onoff(active_receiver->id, 1);
        } else if (vfo[active_receiver->id].rit_enabled && !vfo[vfo_get_tx_vfo()].xit_enabled) {
          // RIT but no XIT: ==> de-activate RIT and activate XIT
          vfo_id_rit_onoff(active_receiver->id, 0);
          vfo_xit_onoff(1);
        } else {
          // else deactivate both.
          vfo_id_rit_onoff(active_receiver->id, 0);
          vfo_xit_onoff(0);
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 43: // switch receivers
      if (receivers == 2) {
        if (v == 0) {
          if (active_receiver->id == 0) {
            schedule_action(RX2, PRESSED, 0);
          } else {
            schedule_action(RX1, PRESSED, 0);
          }

          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    case 45: // ctune
      if (v == 1) {
        schedule_action(CTUN, PRESSED, 0);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 47: // MOX
      if (v == 0) {
      } else {
        radio_toggle_mox();
      }

      break;

    case 48: // TUNE
      if (v == 0) {
      } else {
        radio_toggle_tune();
      }

      break;

    case 50: // TWO TONE
      schedule_action(TWO_TONE, (v == 0) ? PRESSED : RELEASED, 0);
      break;

    case 49: // PS ON
      if (v == 0) {
        if (longpress) {
          longpress = 0;
        } else {
          if (can_transmit) {
            tx_ps_onoff(transmitter, NOT(transmitter->puresignal));
          }
        }
      } else if (v == 2) {
        start_ps_menu();
        longpress = 1;
      }

      break;
    }

  if (p == 44) { // VFO lock
    if (v == 0) {
      if (numpad_active) {
        schedule_action(NUMPAD_KHZ, PRESSED, 0);
        numpad_active = 0;
        locked = 0;
      } else {
        locked ^= 1;
        g_idle_add(ext_vfo_update, NULL);
      }
    }
  }

  return shift;
}

void andromeda_execute_encoder(int p, int v) {
  //
  // ANDROMEDA console with six double-encoders
  // Enc1/2, ..., Enc11/12. The silk print given
  // below comes from the ApacheLabs document
  // 1022_Andromeda-manual-v1.pdf.
  // Note that Enc11 is here implemented as MICgain
  // but the silk print reads MULTI.
  //
  if (!locked) switch (p) {
    // Enc1/2: "RX1 AF/RF"
    case 1:
      schedule_action(AF_GAIN_RX1, RELATIVE, v);
      break;

    case 2:
      schedule_action(AGC_GAIN_RX1, RELATIVE, v);
      break;

    // Enc3/4: "RX2 AF/RF"
    case 3:
      schedule_action(AF_GAIN_RX2, RELATIVE, v);
      break;

    case 4:
      schedule_action(AGC_GAIN_RX2, RELATIVE, v);
      break;

    // Enc5/6: "IF FILTER HIGH/LOW CUT"
    case 5:
      schedule_action(FILTER_CUT_HIGH, RELATIVE, v);
      break;

    case 6:
      schedule_action(FILTER_CUT_LOW, RELATIVE, v);
      break;

    // Enc7/8: "DIVERSITY GAIN/PHASE"
    case 7:
      schedule_action(DIV_GAIN, RELATIVE, v);
      break;

    case 8:
      schedule_action(DIV_PHASE, RELATIVE, v);
      break;

    // Enc9/10: "RIT/XIT"
    case 9: // RIT of the VFO of the active receiver
      schedule_action(RIT, RELATIVE, v);
      break;

    case 10:
      schedule_action(XIT, RELATIVE, v);
      break;

    //Enc11/12: "MULTI/DRIVE", but here implemented as "MIC/DRIVE"
    case 11:
      schedule_action(MIC_GAIN, RELATIVE, v);
      break;

    case 12:
      schedule_action(DRIVE, RELATIVE, v);
      break;
    }
}


