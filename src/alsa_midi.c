/* Copyright (C)
*  2019 - Christoph van Wüllen, DL1YCF
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
 * MIDI support for pihpsdr
 *
 * This is the "Layer-1" for ALSA-MIDI (Linux)
 * For further comments see file mac_midi.c
 */

/*
 * ALSA: MIDI devices are sub-devices to sound cards.
 *       Therefore we have to loop through the sound cards
 *       and then, for each sound card, through the
 *       sub-devices until we have found "our" MIDI
 *       input device.
 *
 *       The procedure how to find and talk with
 *       a MIDI device is taken from the sample
 *       program amidi.c in alsautils.
 */

#ifndef __APPLE__

#include <gtk/gtk.h>

#include "actions.h"
#include "message.h"
#include "midi.h"
#include "midi_menu.h"

#include <pthread.h>
#include <alsa/asoundlib.h>
#include <errno.h>

MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
int n_midi_devices;

//
// The following must not reside in midi_devices since it
// needs special #includes
//
static pthread_t midi_thread_id[MAX_MIDI_DEVICES];
static char *midi_port[MAX_MIDI_DEVICES];
static snd_rawmidi_t *midi_input[MAX_MIDI_DEVICES];

static void* midi_thread(void *);

static void *midi_thread(void *arg) {
  int index = (int) (uintptr_t) arg;
  if (index < 0 || index >= MAX_MIDI_DEVICES) { return NULL; }
  snd_rawmidi_t *input = midi_input[index];
  char *port = midi_port[index];
  int npfds;
  //struct pollfd *pfds;
  unsigned char buf[32];
  unsigned short revents;
  int i, ret;
  MIDI_PARSER parser;
  npfds = snd_rawmidi_poll_descriptors_count(input);
  if (npfds <= 0) {
    t_print("%s: invalid poll descriptor count for port \"%s\": %d\n", __func__, port, npfds);
    midi_devices[index].active = 0;
    return NULL;
  }
  // replaced alloca by variable-length array
  struct pollfd pfds[npfds];
  //pfds = alloca(npfds * sizeof(struct pollfd));
  ret = snd_rawmidi_poll_descriptors(input, pfds, npfds);
  if (ret < 0) {
    t_print("%s: cannot get poll descriptors for port \"%s\": %s\n", __func__, port, snd_strerror(ret));
    midi_devices[index].active = 0;
    return NULL;
  }
  parser.state = STATE_SKIP;
  for (;;) {
    ret = poll(pfds, npfds, 250);
    if (!midi_devices[index].active) { break; }
    if (ret < 0) {
      t_print("%s: poll failed: %s\n", __func__, strerror(errno));
      // Do not give up, but also do not fire too rapidly
      usleep(250000);
    }
    if (ret <= 0) { continue; }  // nothing arrived, do next poll()
    ret = snd_rawmidi_poll_descriptors_revents(input, pfds, npfds, &revents);
    if (ret < 0) {
      t_print("%s: cannot get poll events: %s\n", __func__, snd_strerror(ret));
      midi_devices[index].active = 0;
      break;
    }
    if (revents & (POLLERR | POLLHUP)) {
      t_print("%s: port \"%s\" reported poll error/hangup\n", __func__, port);
      midi_devices[index].active = 0;
      break;
    }
    if (!(revents & POLLIN)) { continue; }
    // something has arrived
    ret = snd_rawmidi_read(input, buf, sizeof(buf));
    if (ret == 0) { continue; }
    if (ret < 0) {
      t_print("%s: cannot read from port \"%s\": %s\n", __func__, port, snd_strerror(ret));
      continue;
    }
    //
    // Parse all bytes in the buffer
    //
    for (i = 0; i < ret; i++) {
      parse_midi_byte((int)buf[i], &parser);
    }
  }
  return NULL;
}

void register_midi_device(int index) {
  int ret = 0;
  if (index < 0 || index >= n_midi_devices) { return; }
  if (midi_input[index] != NULL) { return; }
  t_print("%s: open MIDI device %d\n", __func__, index);
  if ((ret = snd_rawmidi_open(&midi_input[index], NULL, midi_port[index], SND_RAWMIDI_NONBLOCK)) < 0) {
    t_print("%s: cannot open port \"%s\": %s\n", __func__, midi_port[index], snd_strerror(ret));
    midi_devices[index].active = 0;
    midi_input[index] = NULL;
    return;
  }
  snd_rawmidi_read(midi_input[index], NULL, 0); /* trigger reading */
  midi_devices[index].active = 1;
  ret = pthread_create(&midi_thread_id[index], NULL, midi_thread, (void *) (uintptr_t) index);
  if (ret != 0) {
    t_print("%s: Failed to create MIDI read thread: %s\n", __func__, strerror(ret));
    midi_devices[index].active = 0;
    ret = snd_rawmidi_close(midi_input[index]);
    if (ret < 0) {
      t_print("%s: cannot close port: %s\n", __func__, snd_strerror(ret));
    }
    midi_input[index] = NULL;
    return;
  }
  return;
}

void close_midi_device(int index) {
  t_print("%s: index=%d\n", __func__, index);
  if (index < 0 || index >= MAX_MIDI_DEVICES) { return; }
  if (midi_devices[index].active == 0 && midi_input[index] == NULL) { return; }
  //
  // Note that if this is called from get_midi_devices(),
  // the port and device names do exist but may be wrong.
  //
  // Tell thread to stop
  //
  midi_devices[index].active = 0;
  //
  // wait for thread to complete
  //
  if (midi_input[index] != NULL) {
    int ret;
    ret = pthread_join(midi_thread_id[index], NULL);
    if (ret  != 0)  {
      t_print("%s: cannot join: %s\n", __func__, strerror(ret));
    }
    //
    // Close MIDI device
    if ((ret = snd_rawmidi_close(midi_input[index])) < 0) {
      t_print("%s: cannot close port: %s\n", __func__, snd_strerror(ret));
    }
    midi_input[index] = NULL;
  }
}

void get_midi_devices(void) {
  snd_ctl_t *ctl;
  snd_rawmidi_info_t *info;
  int card, device, subs, sub, ret;
  const char *devnam, *subnam;
  char portname[64];
  n_midi_devices = 0;
  card = -1;
  if ((ret = snd_card_next(&card)) < 0) {
    t_print("%s: cannot determine card number: %s\n", __func__, snd_strerror(ret));
    return;
  }
  while (card >= 0) {
    //t_print("%s: Found Sound Card=%d\n", __func__, card);
    snprintf(portname, sizeof(portname), "hw:%d", card);
    if ((ret = snd_ctl_open(&ctl, portname, 0)) < 0) {
      t_print("%s: cannot open control for card %d: %s\n", __func__, card, snd_strerror(ret));
      if ((ret = snd_card_next(&card)) < 0) {
        t_print("%s: cannot determine card number: %s\n", __func__, snd_strerror(ret));
        break;
      }
      continue;
    }
    device = -1;
    // loop through devices of the card
    for (;;) {
      if (n_midi_devices >= MAX_MIDI_DEVICES) {
        break;
      }
      if ((ret = snd_ctl_rawmidi_next_device(ctl, &device)) < 0) {
        t_print("%s: cannot determine device number: %s\n", __func__, snd_strerror(ret));
        break;
      }
      if (device < 0) { break; }
      //t_print("%s: Found Device=%d on Card=%d\n", __func__, device, card);
      // found sub-device
      snd_rawmidi_info_alloca(&info);
      snd_rawmidi_info_set_device(info, device);
      snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
      ret = snd_ctl_rawmidi_info(ctl, info);
      if (ret >= 0) {
        subs = snd_rawmidi_info_get_subdevices_count(info);
      } else {
        subs = 0;
      }
      //t_print("%s: Number of MIDI input devices: %d\n", __func__, subs);
      if (!subs) { continue; }
      // subs: number of sub-devices to device on card
      for (sub = 0; sub < subs; ++sub) {
        if (n_midi_devices >= MAX_MIDI_DEVICES) {
          break;
        }
        snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
        snd_rawmidi_info_set_subdevice(info, sub);
        ret = snd_ctl_rawmidi_info(ctl, info);
        if (ret < 0) {
          t_print("%s: cannot get rawmidi information %d:%d:%d: %s\n", __func__,
                  card, device, sub, snd_strerror(ret));
          break;
        }
        devnam = snd_rawmidi_info_get_name(info);
        subnam = snd_rawmidi_info_get_subdevice_name(info);
        // If there is only one sub-device and it has no name, we  use
        // devnam for comparison and make a portname of form "hw:x,y",
        // else we use subnam for comparison and make a portname of form "hw:x,y,z".
        if (sub == 0 && subnam[0] == '\0') {
          snprintf(portname, sizeof(portname), "hw:%d,%d", card, device);
        } else {
          snprintf(portname, sizeof(portname), "hw:%d,%d,%d", card, device, sub);
          devnam = subnam;
        }
        //
        // If the name/port is unchanged at the same position, keep the
        // existing strings and the running thread. If either changes, close
        // the old device before replacing midi_port[], because the MIDI
        // thread keeps a local pointer to that string for diagnostics.
        //
        int match = 1;
        if (midi_devices[n_midi_devices].name == NULL ||
            strcmp(devnam, midi_devices[n_midi_devices].name) != 0) {
          match = 0;
        }
        if (midi_port[n_midi_devices] == NULL ||
            strcmp(midi_port[n_midi_devices], portname) != 0) {
          match = 0;
        }
        if (match == 0 && (midi_devices[n_midi_devices].active || midi_input[n_midi_devices] != NULL)) {
          close_midi_device(n_midi_devices);
        }
        if (midi_devices[n_midi_devices].name == NULL ||
            strcmp(devnam, midi_devices[n_midi_devices].name) != 0) {
          g_free(midi_devices[n_midi_devices].name);
          midi_devices[n_midi_devices].name = g_strdup(devnam);
        }
        if (midi_port[n_midi_devices] == NULL ||
            strcmp(midi_port[n_midi_devices], portname) != 0) {
          g_free(midi_port[n_midi_devices]);
          midi_port[n_midi_devices] = g_strdup(portname);
        }
        n_midi_devices++;
      }
    }
    snd_ctl_close(ctl);
    if (n_midi_devices >= MAX_MIDI_DEVICES) {
      break;
    }
    // next card
    if ((ret = snd_card_next(&card)) < 0) {
      t_print("%s: cannot determine card number: %s\n", __func__, snd_strerror(ret));
      break;
    }
  }
  for (int i = n_midi_devices; i < MAX_MIDI_DEVICES; i++) {
    if (midi_devices[i].active || midi_input[i] != NULL) {
      close_midi_device(i);
    }
    if (midi_devices[i].name != NULL) {
      g_free(midi_devices[i].name);
      midi_devices[i].name = NULL;
    }
    if (midi_port[i] != NULL) {
      g_free(midi_port[i]);
      midi_port[i] = NULL;
    }
  }
  for (int i = 0; i < n_midi_devices; i++) {
    t_print("%s: %d: %s %s\n", __func__, i, midi_devices[i].name, midi_port[i]);
  }
}

#endif
