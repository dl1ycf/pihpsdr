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

#ifndef _PIHPSDR_MIDI_H_
#define _PIHPSDR_MIDI_H_

#include <gtk/gtk.h>

#include "actions.h"

/*
 * MIDI support for pihpsdr
 *
 * Midi support works in three layers
 *
 * Layer-1: hardware specific (alsa_midi.c or mac_midi.c)
 * ------------------------------------------------------
 *
 * Layer1 either implements a callback function (if the operating system
 * supports MIDI) or a separate thread polling MIDI data. All bytes of the
 * MIDI byte stream are then delegated to the parser.
 *
 * Layer-2: MIDI specific (midi2.c)
 * --------------------------------
 *
 * Layer2 first parses the MIDI bytes and forms MIDI commands, and then
 * translates MIDI commands into pihpsdr actions. This is done with
 * a table-driven algorithm, such that the same translator can be used for
 * any MIDI device provided the tables have been set up correctly.
 * It seems overly complicated to create a user interface for setting up
 * these tables, instead a standard text file describing the MIDI device
 * is read and the tables are set up.
 *
 * Layer-3: SDR specific (midi3.c)
 * -------------------------------
 *
 * Layer 3 mostly just schedules "actions". For VFO commands, it "collects"
 * several such actions (over a time window of 100 msec) and combines them
 *
 */

typedef struct _midi_device {
  char *name;
  int  active;
} MIDI_DEVICE;

#define MAX_MIDI_DEVICES 10

extern MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
extern int n_midi_devices;

extern void get_midi_devices(void);

//
// Types for a state machine that parses incominig MIDI bytes
//

typedef  enum {
  STATE_SKIP,             // skip bytes until command bit is set
  STATE_ARG1,             // one arg byte to come
  STATE_ARG2,             // two arg bytes to come
} MIDI_PARSE_STATE;

typedef enum {
  CMD_NOTEON,
  CMD_NOTEOFF,
  CMD_CTRL,
  CMD_PITCH,
} MIDI_PARSE_COMMAND;

//
// MIDI_PARSER is the state of the parser
//
typedef struct {
  MIDI_PARSE_STATE state;
  MIDI_PARSE_COMMAND command;
  int chan;
  int arg1;
  int arg2;
} MIDI_PARSER;

//
// MIDIevent encodes the actual MIDI event "seen" in Layer-1 and
// passed to Layer-2.
// MIDI_NOTE  events always end up as AT_BTN
// MIDI_PITCH events became AT_KNB
// MIDI_CTRL can end up both as AT_KNB or AT_ENC, depending on the device description.
//
enum MIDIevent {
  EVENT_NONE = 0,
  MIDI_NOTE,
  MIDI_CTRL,
  MIDI_PITCH
};

//
// Data structure for Layer-2
//

//
// There is linked list of all specified MIDI events for a given "Note" value,
// which contains the defined actions for all MIDI_NOTE and MIDI_CTRL events
// with that given note and for all channels
//
// Note that with a MIDI KEY, normally only "Note on" messages
// are processed, except for the actions
// CW_KEYER, CW_LEFT, CW_RIGHT, PTT_KEYER which generate actions
// also for Note-Off.
//

struct desc {
  int               channel;     // -1 for ANY channel
  enum MIDIevent    event;       // type of event (NOTE on/off, Controller change, Pitch value)
  enum ACTIONtype   type;        // AT_BTN, AT_KNB, AT_ENC
  int               vfl1, vfl2;  // encoder only: range of controller values for "very fast left"
  int               fl1, fl2;    // encoder only: range of controller values for "fast left"
  int               lft1, lft2;  // encoder only: range of controller values for "slow left"
  int               vfr1, vfr2;  // encoder only: range of controller values for "very fast right"
  int               fr1, fr2;    // encoder only: range of controller values for "fast right"
  int               rgt1, rgt2;  // encoder only: range of controller values for "slow right"
  int               action;      // SDR "action" to generate
  struct desc       *next;       // Next defined action for a controller/key with that note value (NULL for end of list)
};

extern struct desc *MidiCommandsTable[129];  // slot #128 is for the pitch-bend

extern int midiIgnoreCtrlPairs;

//
// Layer-1 entry point, called once for all the MIDI devices
// that have been defined.
//
void register_midi_device(int index);
void close_midi_device(int index);
void configure_midi_device(gboolean state);

//
// Layer-2 entry point (called by Layer1)
//
// When Layer-1 has received a MIDI byte, it calls
// parse_midi_byte.
//

void parse_midi_byte(int byte, MIDI_PARSER *parser);
void MidiAddCommand(int note, struct desc *desc);
void MidiReleaseCommands(void);

//
// Layer-3 entry point (called by Layer2). In Layer-3, all the pihpsdr
// actions (such as changing the VFO frequency) are performed.
// The implementation of DoTheMIDI is tightly bound to pihpsr and contains
// tons of invocations of g_idle_add with routines from ext.c
//

void DoTheMidi(int code, enum ACTIONtype type, int val);

//
// props file gymnastics
//

extern char *MidiEvent2String(enum MIDIevent event);
extern enum MIDIevent String2MidiEvent(const char *s);
extern void midi_save_state(void);
extern void midi_restore_state(void);
#endif
