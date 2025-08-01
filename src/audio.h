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

#ifndef _AUDIO_H_
#define _AUDIO_H_

#include "receiver.h"

#define MAX_AUDIO_DEVICES 64

typedef struct _audio_devices {
  char *name;
  int index;
  char *description;
} AUDIO_DEVICE;

extern int n_input_devices;
extern AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
extern int n_output_devices;
extern AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];
extern GMutex audio_mutex;

extern int audio_open_input(void);
extern void audio_close_input(void);
extern int audio_open_output(RECEIVER *rx);
extern void audio_close_output(RECEIVER *rx);
extern int audio_write(RECEIVER *rx, float left_sample, float right_sample);
extern int cw_audio_write(RECEIVER *rx, float sample);
extern void audio_get_cards(void);
char * audio_get_error_string(int err);
float  audio_get_next_mic_sample(void);
#endif
