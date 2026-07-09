/* Copyright (C)
* 2024,2025, 2026 - Heiko Amft, DL1BZ (from project deskHPSDR)
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

#ifndef _TCI_AUDIO_H
#define _TCI_AUDIO_H

#include <glib.h>
#include <stdint.h>
#include <stddef.h>

typedef struct _receiver RECEIVER;

typedef void (*TCI_AUDIO_WAKEUP_CALLBACK) (void);

#define TCI_RX_AUDIO_MAX_RECEIVERS 2
#define TCI_RX_AUDIO_RING_FRAMES 48000
#define TCI_RX_AUDIO_FRAME_FRAMES 512
#define TCI_AUDIO_SAMPLE_RATE 48000
#define TCI_AUDIO_CHANNELS 2
#define TCI_AUDIO_FORMAT_FLOAT32 3
#define TCI_STREAM_RX_AUDIO 1
#define TCI_STREAM_TX_AUDIO 2
#define TCI_STREAM_TX_CHRONO 3
#define TCI_TX_AUDIO_FRAME_FRAMES 512
#define TCI_TX_AUDIO_CHRONO_LENGTH (TCI_TX_AUDIO_FRAME_FRAMES * 2)
#define TCI_AUDIO_MONITOR_RING_FRAMES (48000 * 4)
#define TCI_TX_AUDIO_RING_FRAMES (48000 * 4)

void tci_audio_set_wakeup_callback (TCI_AUDIO_WAKEUP_CALLBACK callback);
double tci_get_next_mic_sample();

//
// BigEndian NOTE: TCI has all binary data in little-endian,
// so the piHPSDR TCI code will currently not work on BigEndian
// CPUs where a big-to-little endian conversion is required
//
//
typedef struct __attribute__((__packed__)) _tci_stream_header {
  uint32_t receiver;
  uint32_t sample_rate;
  uint32_t format;
  uint32_t codec;
  uint32_t crc;
  uint32_t length;
  uint32_t type;
  uint32_t channels;
  uint32_t reserv[8];
} TCI_STREAM_HEADER;

typedef struct __attribute__((__packed__)) _tci_stream {
  TCI_STREAM_HEADER header;
  float    audio[8192];        // given here as float not uint8_t
} TCI_STREAM;

void tci_audio_rx_sample (int id, double left, double right);
unsigned int tci_audio_get_write_count (int receiver_id);
unsigned int tci_audio_get_frame (int receiver_id, unsigned int *read_count, TCI_STREAM *stream, size_t frame_size, size_t *frame_len);
void tci_audio_handle_tx_frame (const TCI_STREAM *stream, size_t len);

void tci_audio_tx_reset (void);

#endif
