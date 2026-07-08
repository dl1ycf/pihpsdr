/* Copyright (C)
*  2019 - Christoph van Wüllen, Dl1YCF
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

#ifdef __APPLE__

#include <gtk/gtk.h>

#include <Carbon/Carbon.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#include <CoreAudio/CoreAudio.h>

#include "message.h"
#include "midi.h"
#include "midi_menu.h"

/*
 * MIDI support for pihpsdr
 *
 * This is the "Layer-1" for Apple Macintosh.
 *
 * This file implements the function register_midi_device.
 *
 * If more than one MIDI device matches the name, the LAST ONE
 * found will be taken. This may not be predictable, so it is
 * better to say that one of the matching MIDI devices will be taken.
 * It is easy to change the code such that ALL devices matching the
 * name will be taken. But who cares? Normally there will only be a
 * single MIDI controller connected to the computer running the SDR
 * program.
 *
 * The name is actually specified by the user in the midi.inp file
 * (see midi2.c)
 *
 * This file must generate calls to Layer-2 NewMidiEvent().
 * Some type of messages are not consideres (pressure change, etc.),
 * they are silently dropped but must be processed.
 *
 */

MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
int n_midi_devices;
int running;

//
// state of the parser for each MIDI device
//
MIDI_PARSER parser_array[MAX_MIDI_DEVICES];

//
// MIDI callback function
// called by MacOSX when data from the specified MIDI device arrives.
// We process *all* data but only generate calls to layer-2 for Note On/Off
// and ControllerChange events.
//
//cppcheck-suppress constParameterCallback
static void ReadMIDIdevice(const MIDIPacketList *pktlist, void *refCon, void *connRefCon) {
  int index = GPOINTER_TO_INT(connRefCon);

  if (index < 0 || index >= MAX_MIDI_DEVICES) { return; }

  MIDI_PARSER *parser = parser_array + index;
  MIDIPacket *packet = (MIDIPacket *)pktlist->packet;

  //
  // loop through all bytes of all packets in the current list
  //
  for (unsigned int j = 0; j < pktlist->numPackets; ++j) {
    for (int i = 0; i < packet->length; i++) {
      parse_midi_byte((int) packet->data[i], parser);
    }

    packet = MIDIPacketNext(packet);
  }
}

//
// store the ports and clients locally such that we
// can properly close a MIDI connection.
// This can be local static data, no one outside this file
// needs it.
//
static MIDIPortRef myMIDIports[MAX_MIDI_DEVICES];
static MIDIClientRef myClients[MAX_MIDI_DEVICES];
static MIDIEndpointRef mySources[MAX_MIDI_DEVICES];

void close_midi_device(int index) {
  t_print("%s index=%d\n", __func__, index);

  if (index < 0 || index >= MAX_MIDI_DEVICES) { return; }

  if (midi_devices[index].active == 0 && myMIDIports[index] == 0 && myClients[index] == 0) { return; }

  //
  // This should release the resources associated with the pending connection
  //
  if (myMIDIports[index] != 0) {
    if (mySources[index] != 0) {
      MIDIPortDisconnectSource(myMIDIports[index], mySources[index]);
    }
    MIDIPortDispose(myMIDIports[index]);
    myMIDIports[index] = 0;
    mySources[index] = 0;
  }
  if (myClients[index] != 0) {
    MIDIClientDispose(myClients[index]);
    myClients[index] = 0;
  }
  midi_devices[index].active = 0;
  parser_array[index].state = STATE_SKIP;
  parser_array[index].command = CMD_NOTEON;
  parser_array[index].chan = 0;
  parser_array[index].arg1 = 0;
  parser_array[index].arg2 = 0;
}

void register_midi_device(int index) {
  OSStatus osret;
  t_print("%s: index=%d\n", __func__, index);

  //
  //  Register a callback routine for the device
  //
  if (index < 0 || index >= n_midi_devices) { return; }

  if (myClients[index] != 0 || myMIDIports[index] != 0) { return; }

  myClients[index] = 0;
  myMIDIports[index] = 0;
  mySources[index] = 0;
  MIDIEndpointRef source = MIDIGetSource(index);

  if (source == 0) {
    t_print("%s: MIDIGetSource failed for index=%d\n", __func__, index);
    midi_devices[index].active = 0;
    return;
  }

  //Create client and port, and connect
  osret = MIDIClientCreate(CFSTR("piHPSDR"), NULL, NULL, &myClients[index]);

  if (osret != 0) {
    t_print("%s: MIDIClientCreate failed with ret=%d\n", __func__, (int) osret);
    midi_devices[index].active = 0;
    return;
  }

  osret = MIDIInputPortCreate(myClients[index], CFSTR("FromMIDI"), ReadMIDIdevice, NULL, &myMIDIports[index]);

  if (osret != 0) {
    t_print("%s: MIDIInputPortCreate failed with ret=%d\n", __func__, (int) osret);
    MIDIClientDispose(myClients[index]);
    myClients[index] = 0;
    midi_devices[index].active = 0;
    return;
  }

  osret = MIDIPortConnectSource(myMIDIports[index], source, GINT_TO_POINTER(index));

  if (osret != 0) {
    t_print("%s: MIDIPortConnectSource failed with ret=%d\n", __func__, (int) osret);
    MIDIPortDispose(myMIDIports[index]);
    myMIDIports[index] = 0;
    MIDIClientDispose(myClients[index]);
    myClients[index] = 0;
    midi_devices[index].active = 0;
    return;
  }

  //
  // Now we have successfully opened the device.
  //
  mySources[index] = source;
  midi_devices[index].active = 1;
  return;
}

void get_midi_devices(void) {
  int n;
  int i;
  CFStringRef pname;   // MacOS name of the device
  char name[128];      // C name of the device
  static int first = 1;

  if (first) {
    //
    // perhaps not necessary in C, but good programming practise:
    // initialise the table upon the first call
    //
    first = 0;

    for (i = 0; i < MAX_MIDI_DEVICES; i++) {
      midi_devices[i].name = NULL;
      midi_devices[i].active = 0;
      parser_array[i].state = STATE_SKIP;
      parser_array[i].command = CMD_NOTEON;
      parser_array[i].chan = 0;
      parser_array[i].arg1 = 0;
      parser_array[i].arg2 = 0;
    }
  }

  //
  //  This is called at startup (via midi_restore) and each time
  //  the MIDI menu is opened. So we have to take care that this
  //  function is essentially a no-op if the device list has not
  //  changed.
  //  If the device list has changed because of hot-plugging etc.
  //  close any MIDI device which changed position and mark
  //  it as inactive. Note that upon a hot-plug, MIDI devices that were
  //  there before may change its position in the device list and will then
  //  be closed.
  //
  n = MIDIGetNumberOfSources();
  n_midi_devices = 0;

  for (i = 0; i < n; i++) {
    MIDIEndpointRef dev = MIDIGetSource(i);

    if (dev != 0) {
      OSStatus osret = MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &pname);

      if (osret != 0) { continue; } // in this case pname is invalid

      if (!CFStringGetCString(pname, name, sizeof(name), kCFStringEncodingUTF8)) {
        snprintf(name, sizeof(name), "NoPort%d", n_midi_devices);
      }

      CFRelease(pname);
      //
      // Some users have reported that MacOS reports a string of length zero
      // for some MIDI devices. In this case, we replace the name by
      // "NoPort<n>"
      //
      if (strlen(name) == 0) { snprintf(name, sizeof(name), "NoPort%d", n_midi_devices); }

      t_print("%s: %s\n", __func__, name);

      if (midi_devices[n_midi_devices].name != NULL) {
        if (strncmp(name, midi_devices[n_midi_devices].name, sizeof(name))) {
          //
          // This slot was occupied and the names do not match:
          // Close device (if active), insert new name
          //
          if (midi_devices[n_midi_devices].active || myMIDIports[n_midi_devices] != 0 || myClients[n_midi_devices] != 0) {
            close_midi_device(n_midi_devices);
          }

          g_free(midi_devices[n_midi_devices].name);
          midi_devices[n_midi_devices].name = g_strdup(name);
        } else {
          //
          // This slot was occupied and the names match: do nothing!
          // If there was no hot-plug or hot-unplug, we should always
          // arrive here!
          //
        }
      } else {
        //
        // This slot was unoccupied. Insert name and mark inactive
        //
        midi_devices[n_midi_devices].name = g_strdup(name);
        midi_devices[n_midi_devices].active = 0;
      }

      n_midi_devices++;
    }

    //
    // If there are more devices than we have slots in our Table
    // just stop processing.
    //
    if (n_midi_devices >= MAX_MIDI_DEVICES) { break; }
  }

  t_print("%s: number of devices=%d\n", __func__, n_midi_devices);

  //
  // Get rid of all devices lingering around above the high-water mark
  // (this happens in the case of hot-unplugging)
  //
  for (i = n_midi_devices; i < MAX_MIDI_DEVICES; i++) {
    if (midi_devices[i].active || myMIDIports[i] != 0 || myClients[i] != 0) {
      close_midi_device(i);
    }

    if (midi_devices[i].name != NULL) {
      g_free(midi_devices[i].name);
      midi_devices[i].name = NULL;
    }
  }
}
#endif
