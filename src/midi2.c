/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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
 * Layer-2 of MIDI support
 *
 * Using the data in MIDICommandsTable, this subroutine translates the low-level
 * MIDI events into MIDI actions in the SDR console.
 */

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
  #include "MacOS.h"  // emulate clock_gettime on old MacOS systems
#endif

#include "main.h"
#include "message.h"
#include "midi.h"
#include "property.h"

struct desc *MidiCommandsTable[129];

void NewMidiEvent(enum MIDIevent event, int channel, int note, int val) {
  struct desc *desc;
  int new;
#ifdef MIDIDEBUG
  t_print("%s:EVENT=%d CHAN=%d NOTE=%d VAL=%d\n", __FUNCTION__, event, channel, note, val);
#endif

  //
  // Sometimes a "heart beat" from a device might be useful. Therefore, we resert
  // channel=16 note=0 for this purpose and filter this out here
  //
  if (event == MIDI_NOTE && channel == 15 && note == 0) {
    return;
  }

  if (event == MIDI_PITCH) {
    desc = MidiCommandsTable[128];
  } else {
    desc = MidiCommandsTable[note];
  }

  //t_print("%s: init DESC=%p\n",__FUNCTION__,desc);
  while (desc) {
    //t_print("%s: DESC=%p next=%p CHAN=%d EVENT=%d\n",__FUNCTION__,desc,desc->next,desc->channel,desc->event);
    if ((desc->channel == channel || desc->channel == -1) && (desc->event == event)) {
      // Found matching entry
      switch (desc->event) {
      case EVENT_NONE:
        // this cannot happen
        t_print("%s: Unknown Event\n", __FUNCTION__);
        break;

      case MIDI_NOTE:
        DoTheMidi(desc->action, desc->type, val);
        break;

      case MIDI_CTRL:
        if (desc->type == AT_KNB) {
          // CHANGED Jan 2024: report the "raw" value (0-127) upstream
          DoTheMidi(desc->action, desc->type, val);
        } else if (desc->type == AT_ENC) {
          // translate value to direction/speed
          new = 0;

          if ((val >= desc->vfl1) && (val <= desc->vfl2)) { new = -16; }

          if ((val >= desc-> fl1) && (val <= desc-> fl2)) { new = -4; }

          if ((val >= desc->lft1) && (val <= desc->lft2)) { new = -1; }

          if ((val >= desc->rgt1) && (val <= desc->rgt2)) { new = 1; }

          if ((val >= desc-> fr1) && (val <= desc-> fr2)) { new = 4; }

          if ((val >= desc->vfr1) && (val <= desc->vfr2)) { new = 16; }

          //                      t_print("%s: ENCODER PARAMS: val=%d new=%d thrs=%d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d\n",
          //                               __FUNCTION__,
          //                               val, new, desc->vfl1, desc->vfl2, desc->fl1, desc->fl2, desc->lft1, desc->lft2,
          //                               desc->rgt1, desc->rgt2, desc->fr1, desc->fr2, desc->vfr1, desc->vfr2);
          if (new != 0) { DoTheMidi(desc->action, desc->type, new); }
        }

        break;

      case MIDI_PITCH:
        if (desc->type == AT_KNB) {
          // use upper 7  bits
          DoTheMidi(desc->action, desc->type, val >> 7);
        }

        break;
      }

      break;
    } else {
      desc = desc->next;
    }
  }

  if (!desc) {
    // Nothing found. This is nothing to worry about, but log the key to stderr
    if (event == MIDI_PITCH) { t_print("%s: Unassigned PitchBend Value=%d\n", __FUNCTION__, val); }

    if (event == MIDI_NOTE ) { t_print("%s: Unassigned Key Note=%d Val=%d\n", __FUNCTION__, note, val); }

    if (event == MIDI_CTRL ) { t_print("%s: Unassigned Controller Ctl=%d Val=%d\n", __FUNCTION__, note, val); }
  }
}

/*
 * Release data from MidiCommandsTable
 */

void MidiReleaseCommands() {
  int i;
  struct desc *loop, *new;

  for (i = 0; i < 129; i++) {
    loop = MidiCommandsTable[i];

    while (loop != NULL) {
      new = loop->next;
      free(loop);
      loop = new;
    }

    MidiCommandsTable[i] = NULL;
  }
}

/*
 * Add a command to MidiCommandsTable
 */

void MidiAddCommand(int note, struct desc *desc) {
  struct desc *loop;

  if (note < 0 || note > 128) { return; }

  //
  // Actions with channel == -1 (ANY) must go to the end of the list
  //
  if (MidiCommandsTable[note] == NULL) {
    // initialise linked list
    MidiCommandsTable[note] = desc;
  } else if (desc->channel >= 0) {
    // add to top of the list
    desc->next = MidiCommandsTable[note];
    MidiCommandsTable[note] = desc;
  } else {
    // add to tail of the list
    loop = MidiCommandsTable[note];

    while (loop->next != NULL) {
      loop = loop->next;
    }

    loop->next = desc;
  }
}

//
// Utility functions to convert between enums and human-readable strings
//
char *MidiEvent2String(enum MIDIevent event) {
  switch (event) {
  case EVENT_NONE:
  default:
    return "NONE";
    break;

  case MIDI_NOTE:
    return "NOTE";
    break;

  case MIDI_CTRL:
    return "CTRL";
    break;

  case MIDI_PITCH:
    return "PITCH";
    break;
  }
}

enum ACTIONtype String2MidiType(const char *str) {
  if (!strcmp(str, "Button"     )) { return AT_BTN;   }

  if (!strcmp(str, "Knob/Slider")) { return AT_KNB;  }

  if (!strcmp(str, "Encoder"    )) { return AT_ENC; }

  return AT_NONE;
}

char *MidiType2String(enum ACTIONtype type) {
  switch (type) {
  case AT_NONE:
  default:
    return "None";
    break;

  case AT_BTN:
    return "Button";
    break;

  case AT_KNB:
    return "Knob/Slider";
    break;

  case AT_ENC:
    return "Encoder";
    break;
  }
}

enum MIDIevent String2MidiEvent(const char *str) {
  if (!strcmp(str, "NOTE"))  { return MIDI_NOTE;  }

  if (!strcmp(str, "CTRL"))  { return MIDI_CTRL;  }

  if (!strcmp(str, "PITCH")) { return MIDI_PITCH; }

  return EVENT_NONE;
}

void midiSaveState() {
  struct desc *cmd;
  int entry;
  int i;
  entry = 0;
  SetPropI0("midiIgnoreCtrlPairs", midiIgnoreCtrlPairs);

  for (i = 0; i < n_midi_devices; i++) {
    if (midi_devices[i].active) {
      SetPropS1("mididevice[%d].name", entry, midi_devices[i].name);
      entry++;
    }
  }

  // the value i=128 is for the PitchBend
  for (i = 0; i < 129; i++) {
    cmd = MidiCommandsTable[i];
    entry = -1;

    while (cmd != NULL) {
      entry++;
      int channel = cmd->channel;
      //t_print("%s:  channel=%d key=%d entry=%d event=%s type=%s action=%s\n",__FUNCTION__,channel,i,entry, Event2String(cmd->event),Type2String(cmd->type),ActionTable[cmd->action].str);
      SetPropI2("midi[%d].entry[%d].channel", i, entry,                      channel);
      SetPropS3("midi[%d].entry[%d].channel[%d].event", i, entry, channel,   MidiEvent2String(cmd->event));
      SetPropS3("midi[%d].entry[%d].channel[%d].type", i, entry, channel,    MidiType2String(cmd->type));
      SetPropA3("midi[%d].entry[%d].channel[%d].action", i, entry, channel,  cmd->action);

      //
      // For encoders, also store the additional parameters,
      //
      if (cmd->type == AT_ENC) {
        SetPropI3("midi[%d].entry[%d].channel[%d].vfl1", i, entry, channel,       cmd->vfl1);
        SetPropI3("midi[%d].entry[%d].channel[%d].vfl2", i, entry, channel,       cmd->vfl2);
        SetPropI3("midi[%d].entry[%d].channel[%d].fl1", i, entry, channel,        cmd->fl1);
        SetPropI3("midi[%d].entry[%d].channel[%d].fl2", i, entry, channel,        cmd->fl2);
        SetPropI3("midi[%d].entry[%d].channel[%d].lft1", i, entry, channel,       cmd->lft1);
        SetPropI3("midi[%d].entry[%d].channel[%d].lft2", i, entry, channel,       cmd->lft2);
        SetPropI3("midi[%d].entry[%d].channel[%d].rgt1", i, entry, channel,       cmd->rgt1);
        SetPropI3("midi[%d].entry[%d].channel[%d].rgt2", i, entry, channel,       cmd->rgt2);
        SetPropI3("midi[%d].entry[%d].channel[%d].fr1", i, entry, channel,        cmd->fr1);
        SetPropI3("midi[%d].entry[%d].channel[%d].fr2", i, entry, channel,        cmd->fr2);
        SetPropI3("midi[%d].entry[%d].channel[%d].vfr1", i, entry, channel,       cmd->vfr1);
        SetPropI3("midi[%d].entry[%d].channel[%d].vfr2", i, entry, channel,       cmd->vfr2);
      }

      cmd = cmd->next;
    }

    if (entry != -1) {
      SetPropI1("midi[%d].entries", i, entry + 1);
    }
  }
}

void midiRestoreState() {
  char str[128];
  int channel;
  int event;
  int type;
  int action;
  int vfl1, vfl2;
  int fl1, fl2;
  int lft1, lft2;
  int rgt1, rgt2;
  int fr1, fr2;
  int vfr1, vfr2;
  int i, j;
  get_midi_devices();
  MidiReleaseCommands();
  //t_print("%s\n",__FUNCTION__);
  GetPropI0("midiIgnoreCtrlPairs", midiIgnoreCtrlPairs);

  //
  // Note this is too early to open the MIDI devices, since the
  // radio has not yet fully been configured. Therefore, only
  // set the "active" flag, and the devices will be opened in
  // radio.c when it is appropriate
  //
  for (i = 0; i < MAX_MIDI_DEVICES; i++) {
    snprintf(str, sizeof(str), "NO_MIDI_DEVICE_FOUND");
    GetPropS1("mididevice[%d].name", i,  str);

    for (j = 0; j < n_midi_devices; j++) {
      if (strcmp(midi_devices[j].name, str) == 0) {
        midi_devices[j].active = 1;
        t_print("%s: MIDI device %s active=%d\n", __FUNCTION__, str, midi_devices[j].active);
      }
    }
  }

  // the value i=128 is for the PitchBend
  for (i = 0; i < 129; i++) {
    int entries = -1;
    GetPropI1("midi[%d].entries", i, entries);

    for (int entry = 0; entry < entries; entry++) {
      channel = -1;
      GetPropI2("midi[%d].entry[%d].channel", i, entry,      channel);

      if (channel < 0) { continue; }

      snprintf(str, sizeof(str), "NONE");
      GetPropS3("midi[%d].entry[%d].channel[%d].event", i, entry, channel, str);
      event = String2MidiEvent(str);
      snprintf(str, sizeof(str), "NONE");
      GetPropS3("midi[%d].entry[%d].channel[%d].type", i, entry, channel, str);
      type  = String2MidiType(str);
      action = NO_ACTION;
      GetPropA3("midi[%d].entry[%d].channel[%d].action", i, entry, channel, action);
      //
      // Look for encoder parameters. For those not found,
      // use default value
      //
      vfl1 = -1;
      vfl2 = -1;
      fl1 = -1;
      fl2 = -1;
      lft1 = 0;
      lft2 = 63;
      rgt1 = 65;
      rgt2 = 127;
      fr1 = -1;
      fr2 = -1;
      vfr1 = -1;
      vfr2 = -1;

      if (type == AT_ENC) {
        GetPropI3("midi[%d].entry[%d].channel[%d].vfl1", i, entry, channel,  vfl1);
        GetPropI3("midi[%d].entry[%d].channel[%d].vfl2", i, entry, channel,  vfl2);
        GetPropI3("midi[%d].entry[%d].channel[%d].fl1", i, entry, channel,   fl1);
        GetPropI3("midi[%d].entry[%d].channel[%d].fl2", i, entry, channel,   fl2);
        GetPropI3("midi[%d].entry[%d].channel[%d].lft1", i, entry, channel,  lft1);
        GetPropI3("midi[%d].entry[%d].channel[%d].lft2", i, entry, channel,  lft2);
        GetPropI3("midi[%d].entry[%d].channel[%d].rgt1", i, entry, channel,  rgt1);
        GetPropI3("midi[%d].entry[%d].channel[%d].rgt2", i, entry, channel,  rgt2);
        GetPropI3("midi[%d].entry[%d].channel[%d].fr1", i, entry, channel,   fr1);
        GetPropI3("midi[%d].entry[%d].channel[%d].fr2", i, entry, channel,   fr2);
        GetPropI3("midi[%d].entry[%d].channel[%d].vfr1", i, entry, channel,  vfr1);
        GetPropI3("midi[%d].entry[%d].channel[%d].vfr2", i, entry, channel,  vfr2);
      }

      //
      // Construct descriptor and add to the list of MIDI commands
      //
      struct desc *desc = (struct desc *) malloc(sizeof(struct desc));

      if (!desc) {
        fatal_error("FATAL: alloc desc in midi");
        return;
      }

      desc->next     = NULL;
      desc->action   = action; // MIDIaction
      desc->type     = type;   // MIDItype
      desc->event    = event;  // MIDIevent
      desc->vfl1     = vfl1;
      desc->vfl2     = vfl2;
      desc->fl1      = fl1;
      desc->fl2      = fl2;
      desc->lft1     = lft1;
      desc->lft2     = lft2;
      desc->rgt1     = rgt1;
      desc->rgt2     = rgt2;
      desc->fr1      = fr1;
      desc->fr2      = fr2;
      desc->vfr1     = vfr1;
      desc->vfr2     = vfr2;
      desc->channel  = channel;
      MidiAddCommand(i, desc);
    }
  }
}
