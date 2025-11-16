/* Copyright (C)
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
// This file implements the ANDROMEDA commands for the
// controller types 4 (updated G2MkI) and 5 (G2-Ultra).
// Commands can now be dynamically assiged to push-buttons
// and encoders (with the exception of the VFO encoder).
//

#include <gtk/gtk.h>
#include <stdlib.h>

#include "actions.h"
#include "g2panel_menu.h"
#include "property.h"

int *g2panel_default_buttons(int andromeda_type) {
  //
  // allocate a vector defining the ANDROMEDA button commands
  // and populate it with the default commands.
  // From the CAT protocol, button numbers can be between 0 and 99.
  //
  // Return NULL for "unknown" controller types.
  //
  int *result;

  switch (andromeda_type) {
  case 4:
    result = g_new(int, 100);

    for (int i = 0; i < 100; i++) { result[i] = NO_ACTION; }

    // left edge lower encoder push-button, silk print: "RX AF/AGC", default: MUTE
    result[1] = MUTE;
    // right edge upper encoder push-button, silk print: "Multi 1", default: FILTER_CUT_DEFAULT
    result[5] = FILTER_CUT_DEFAULT;
    // right edge lower encoder push-button, silk print: "Multi 2", default: RITXIT_CLEAR
    result[9] = RITXIT_CLEAR;
    // left edge upper encoder push-button, silk print: "MIC/DRIVE", default: MULTI_BUTTON
    result[11] = RITXIT_CLEAR;
    // 4x3 pad, row 4, column 1, silk print: "FCN", default: FUNCTION
    result[21] = FUNCTION;
    // 4x3 pad, row 1, column 3, silk print: "Band+", default: BAND_PLUS
    result[30] = BAND_PLUS;
    // 4x3 pad, row 1, column 1, silk print: "Mode+", default: MODE_PLUS
    result[31] = MODE_PLUS;
    // 4x3 pad, row 1, column 2, silk print: "Fil+", default: FILTER_PLUS
    result[32] = FILTER_PLUS;
    // 4x3 pad, row 2, column 3, silk print: "Band-", default: BAND_MINUS
    result[33] = BAND_MINUS;
    // 4x3 pad, row 2, column 1, silk print: "Mode-", default: MODE_MINUS
    result[34] = MODE_MINUS;
    // 4x3 pad, row 2, column 3, silk print: "Fil-", default: FILTER_MINUS
    result[35] = FILTER_MINUS;
    // 4x3 pad, row 3, column 1, silk print: "A>B", default: A_TO_B
    result[36] = A_TO_B;
    // 4x3 pad, row 3, column 2, silk print: "B>A", default: B_TO_A
    result[37] = B_TO_A;
    // 4x3 pad, row 3, column 3, silk print: "Split", default: SPLIT
    result[38] = SPLIT;
    // 4x3 pad, row 4, column 2, silk print: "RIT", default: RIT_ENABLE
    result[42] = RIT_ENABLE;
    // 4x3 pad, row 4, column 3, silk print: "XIT", default: XIT_ENABLE
    result[43] = XIT_ENABLE;
    // right button below VFO knob, silk print: "LOCK", default: LOCK
    result[44] = LOCK;
    // left button below VFO knob, silk print: "CTUNE", default: CTUN
    result[45] = CTUN;
    // upper button left of the screen, silk print: "MOX", default: MOX
    result[47] = MOX;
    // lower button left of screen, silk print: "2TONE/TUNE", default: TUNE
    result[50] = TUNE;
    break;

  case 5:
    result = g_new(int, 100);

    for (int i = 0; i < 100; i++) { result[i] = NO_ACTION; }

    // left edge lower encoder, push-button, silk print: "RX2 AF/AGC", default: MUTE_RX2
    result[1] = MUTE_RX2;
    // left edge upper encoder (directly below power button), push-button, silk print: "RX1 AF/AGC", default: MUTE_RX1
    result[2] = MUTE_RX1;
    // encoder between power button and screen, push-button, silk print: "DRIVE/MULTI", default: MULTI_BUTTON
    result[3] = MULTI_BUTTON;
    // lowest of the four buttons left of the screen, silk print: "ATU", not yet used
    result[4] = NO_ACTION;
    // second-lowest of the four buttons left of the screen, silk print: "2TONE", default: TWO_TONE
    result[5] = TWO_TONE;
    // second-highest of the four buttons left of the screen, silk print: "TUNE", default: TUNE
    result[6] = TUNE;
    // highest of the four buttons left of the screen, silk print: "MOX", default: MOX
    result[7] = MOX;
    // lower left of the VFO knob, silk print: "CTUNE", default: CTUN
    result[8] = CTUN;
    // lower right of the VFO knob, silk print: "LOCK", default: LOCK
    result[9] = LOCK;
    // button with silk print "A/B", default: SWAP_RX
    result[10] = SWAP_RX;
    // button with silk print "RIT/XIT", default: RITSELECT
    result[11] = RITSELECT;
    // right edge lower encoder, push-button, silk print: "RIT/XIT", default: RITXIT_CLEAR
    result[12] = RITXIT_CLEAR;
    // right edge upper encoder, push-button, SHIFT OFF, silk print: "MULTI 2", default: FILTER_CUT_DEFAULT
    result[13] = FILTER_CUT_DEFAULT;
    // 4x3 pad row 1 col 1, silk print: "160/MODE+", "no Band", default: MODE_PLUS
    result[14] = MODE_PLUS;
    // 4x3 pad row 1 col 2, silk print: "80/FIL+", "no Band", default: FILTER_PLUS
    result[15] = FILTER_PLUS;
    // 4x3 pad row 1 col 3, silk print: "60/BAND+", "no Band", default: BAND_PLUS
    result[16] = BAND_PLUS;
    // 4x3 pad row 2 col 1, silk print: "40/MODE-", "no Band", default: MODE_MINUS
    result[17] = MODE_MINUS;
    // 4x3 pad row 2 col 2, silk print: "30/FIL-", "no Band", default: FILTER_MINUS
    result[18] = FILTER_MINUS;
    // 4x3 pad row 2 col 3, silk print: "20/BAND-", "no Band", default: BAND_MINUS
    result[19] = BAND_MINUS;
    // 4x3 pad row 3 col 1, silk print: "17/A>B", "no Band", default: A_TO_B
    result[20] = A_TO_B;
    // 4x3 pad row 3 col 2, silk print: "15/B>A", "no Band", default: B_TO_A
    result[21] = B_TO_A;
    // 4x3 pad row 3 col 3, silk print: "12/SPLIT", "no Band", default: SPLIT
    result[22] = SPLIT;
    // 4x3 pad row 4 col 1, silk print: "10/F1", "no Band", default: SNB
    result[23] = SNB;
    // 4x3 pad row 4 col 2, silk print: "6/F2", "no Band", default: NB
    result[24] = NB;
    // 4x3 pad row 4 col 3, silk print: "LF/HF/F3", "no Band", default: NR
    result[25] = NR;
    // There is not button with this number
    result[26] = NO_ACTION;
    // 4x3 pad row 1 col 1, silk print: "160/MODE+", "Band", default: BAND_160
    result[27] = NR;
    // 4x3 pad row 1 col 2, silk print: "80/FIL+", "Band", default: BAND_80
    result[28] = BAND_80;
    // 4x3 pad row 1 col 3, silk print: "60/BAND+", "Band", default: BAND_60
    result[29] = BAND_60;
    // 4x3 pad row 2 col 1, silk print: "40/MODE-", "Band", default: BAND_40
    result[30] = BAND_40;
    // 4x3 pad row 2 col 2, silk print: "30/FIL-", "Band", default: BAND_30
    result[31] = BAND_30;
    // 4x3 pad row 2 col 3, silk print: "20/BAND-", "Band", default: BAND_20
    result[32] = BAND_20;
    // 4x3 pad row 3 col 1, silk print: "17/A>B", "Band", default: BAND_17
    result[33] = BAND_17;
    // 4x3 pad row 3 col 2, silk print: "15/B>A", "Band", default: BAND_15
    result[34] = BAND_15;
    // 4x3 pad row 3 col 3, silk print: "12/SPLIT", "Band", default: BAND_12
    result[35] = BAND_12;
    // 4x3 pad row 4 col 1, silk print: "10/F1", "Band", default: BAND_10
    result[36] = BAND_10;
    // 4x3 pad row 4 col 2, silk print: "6/F2", "Band", default: BAND_6
    result[37] = BAND_6;
    // 4x3 pad row 4 col 3, silk print: "LF/HF/F3", "Band", default: BAND_136
    result[38] = BAND_136;
    // Not used
    result[39] = NO_ACTION;
    // Not used
    result[40] = NO_ACTION;
    // right edge upper encoder, push-button, SHIFT ON, silk print: "MULTI 2", default: DIV
    result[41] = DIV;
    break;

  default:
    result = NULL;
    break;
  }

  return result;
}

int *g2panel_default_encoders(int andromeda_type) {
  //
  // allocate a vector defining the ANDROMEDA button commands
  // and populate it with the default commands.
  // From the CAT protocol, encoder numbers can be between 0 and 49.
  //
  // Return NULL for "unknown" controller types.
  //
  int *result;

  switch (andromeda_type) {
  case 4:
    result = g_new(int, 50);

    for (int i = 0; i < 50; i++) { result[i] = NO_ACTION; }

    // left edge lower encoder inner knob, silk print: "RX AF/AGC", default: AF_GAIN
    result[1] = AF_GAIN;
    // left edge lower encoder outer knob, silk print: "RX AF/AGC", default: AGC_GAIN
    result[2] = AF_GAIN;
    // right edge upper encoder inner knob, silk print: "Multi 1" (?), default: FILTER_CUT_HIGH
    result[5] = FILTER_CUT_HIGH;
    // right edge upper encoder outer knob, silk print: "Multi 1" (?), default: FILTER_CUT_LOW
    result[6] = FILTER_CUT_LOW;
    // right edge lower encoder inner knob, silk print: "Multi 2" (?), default: RIT
    result[9] = RIT;
    // right edge lower encoder outer knob, silk print: "Multi 2" (?), default: XIT
    result[10] = XIT;
    // left edge upper encoder inner knob, silk print: "MIC/DRIVE", default: MULTI_ENC
    result[11] = MULTI_ENC;
    // left edge upper encoder outer knob, silk print: "MIC/DRIVE", default: DRIVE
    result[12] = DRIVE;
    break;

  case 5:
    result = g_new(int, 50);

    for (int i = 0; i < 50; i++) { result[i] = NO_ACTION; }

    // left edge lower encoder, inner knob, silk print: "RX2 AF/AGC", default: AF_GAIN_RX2
    result[1] = AF_GAIN_RX2;
    // left edge lower encoder, outer knob, silk print: "RX2 AF/AGC", default: AGC_GAIN_RX2
    result[2] = AGC_GAIN_RX2;
    // left edge upper encoder (directly below power button), inner knob, silk print: "RX1 AF/AGC", default: AF_GAIN_RX1
    result[3] = AF_GAIN_RX1;
    // left edge upper encoder (directly below power button), outer knob, silk print: "RX1 AF/AGC", default: AGC_GAIN_RX1
    result[4] = AGC_GAIN_RX1;
    // encoder between power button and screen, inner knob, silk print: "DRIVE/MULTI", default: MULTI_ENC
    result[5] = MULTI_ENC;
    // encoder between power button and screen, outer knob, silk print: "DRIVE/MULTI", default: DRIVE
    result[6] = DRIVE;
    // right edge lower encoder inner knob, silk print: "RIT/ATTN", default: RIXXIT
    result[7] = RITXIT;
    // right edge lower encoder outer knob, silk print: "RIT/ATTN", default: ATTENUATION
    result[8] = ATTENUATION;
    // right edge upper encoder inner knob, (shift OFF), silk print:" MULTI 2" (?), default: FILTER_CUT_HIGH
    result[9] = FILTER_CUT_HIGH;
    // right edge upper encoder outer knob (shift OFF), silk print:" MULTI 2" (?), default: FILTER_CUT_LOW
    result[10] = FILTER_CUT_LOW;
    // right edge upper encoder inner knob, (shift ON), silk print:" MULTI 2" (?), default: DIV_GAIN
    result[11] = DIV_GAIN;
    // right edge upper encoder outer knob, (shift ON), silk print:" MULTI 2" (?), default: DIV_PHASE
    result[12] = DIV_PHASE;
    break;

  default:
    result = NULL;
    break;
  }

  return result;
}

void g2panel_execute_button(int type, const int *vec, int button, int tr01, int tr10, int tr12, int tr20) {
  enum ACTION action;

  if (vec == NULL || button < 0 || button > 99) { return; }

  action = vec[button];

  if (action == NO_ACTION) { return; }

  //
  // Some actions have a special second "longpress" meaning
  //
  switch (action) {
  case BAND_PLUS:
  case BAND_MINUS:
    if (tr10) { schedule_action(action, PRESSED, 0); }

    if (tr12) { schedule_action(MENU_BAND, PRESSED, 0); }

    break;

  case FILTER_PLUS:
  case FILTER_MINUS:
    if (tr10) { schedule_action(action, PRESSED, 0); }

    if (tr12) { schedule_action(MENU_FILTER, PRESSED, 0); }

    break;

  case MODE_PLUS:
  case MODE_MINUS:
    if (tr10) { schedule_action(action, PRESSED, 0); }

    if (tr12) { schedule_action(MENU_MODE, PRESSED, 0); }

    break;

  case ANF:
  case SNB:
  case NB:
  case NR:
    if (tr10) { schedule_action(action, PRESSED, 0); }

    if (tr12) { schedule_action(MENU_NOISE, PRESSED, 0); }

    break;

  case TWO_TONE:
    if (tr10) { schedule_action(action, PRESSED, 0); }

    if (tr12) { schedule_action(MENU_PS, PRESSED, 0); }

    break;

  default:
    if (tr01) { schedule_action(action, PRESSED, 0); }

    if (tr10 || tr20) { schedule_action(action, RELEASED, 0); }

    break;
  }
}

void g2panel_execute_encoder(int type, const int *vec, int encoder, int val) {
  enum ACTION action;

  if (vec == NULL || encoder < 0 || encoder > 49) { return; }

  action = vec[encoder];

  if (action == NO_ACTION) { return; }

  schedule_action(action, RELATIVE, val);
}

//
// Save ANDROMEDA state to props data base
//
void g2panel_save_state(int andromeda_type, const int *buttonvec, const int *encodervec) {
  if (buttonvec != NULL) {
    for (int i = 0; i < 100; i++) {
      SetPropA2("andromeda[%d].button[%d].action", andromeda_type, i, buttonvec[i]);
    }
  }

  if (encodervec != NULL) {
    for (int i = 0; i < 50; i++) {
      SetPropA2("andromeda[%d].encoder[%d].action", andromeda_type, i, encodervec[i]);
    }
  }
}

//
// Restore ANDROMEDA state from props database
//
void g2panel_restore_state(int andromeda_type, int *buttonvec, int *encodervec) {
  if (buttonvec != NULL) {
    for (int i = 0; i < 100; i++) {
      GetPropA2("andromeda[%d].button[%d].action", andromeda_type, i, buttonvec[i]);
    }
  }

  if (encodervec != NULL) {
    for (int i = 0; i < 50; i++) {
      GetPropA2("andromeda[%d].encoder[%d].action", andromeda_type, i, encodervec[i]);
    }
  }
}
