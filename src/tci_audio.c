/* Copyright (C)
* 2024,2025, 2026 - Heiko Amft, DL1BZ (from project deskHPSDR)
* 2026            - C. van Wüllen, DL1YCF
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

#include <glib.h>
#include <math.h>
#include <string.h>

#include "atomic.h"
#include "message.h"
#include "receiver.h"
#include "tci_audio.h"
#include "tci.h"

//
// Functions provided in this file:
//
// tci_audio_tx_reset          Reset TX audio ring buffer
// tci_audio_rx_sample         Put next RX audio sample into ring buffer
// tci_audio_get_frame         Obtain next bunch of RX audio samples
//                             from the ring buffer (to be sent to client)
// tci_audio_handle_tx_frame   put next bunch of TX audio samples from the client
//                             into the ring buffer
// tci_get_next_mic_sample     obtain next TX audio sample from the ring buffer
//
#define TCI_RX_AUDIO_RING_FRAMES 32768
#define TCI_RX_AUDIO_RING_MASK   32767
#define TCI_TX_AUDIO_RING_FRAMES 65536
#define TCI_TX_AUDIO_RING_MASK   65535


typedef struct _tci_rx_audio_ring {
  GMutex mutex;
  float samples[2 * TCI_RX_AUDIO_RING_FRAMES];
  atomic_int inpt;
  atomic_int outpt;
} TCI_RX_AUDIO_RING;

typedef struct _tci_tx_audio_ring {
  GMutex mutex;
  float samples[TCI_TX_AUDIO_RING_FRAMES];
  int inpt;
  int outpt;
  float cache[TCI_TX_AUDIO_FRAME_FRAMES];
  atomic_int cache_len;
  int cache_pos;
} TCI_TX_AUDIO_RING;

static TCI_RX_AUDIO_RING tci_rx_audio_ring[TCI_RX_AUDIO_MAX_RECEIVERS];
static TCI_TX_AUDIO_RING tci_tx_audio_ring;

void tci_audio_tx_reset (void) {
  //
  // Asynchronouos drain of the TX audio buffer.
  //
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex);
  ring->inpt = 0;
  ring->outpt = 0;
  ring->cache_len = 0;
  g_mutex_unlock (&ring->mutex);
}

void tci_audio_rx_sample (int id, double left, double right) {
  //
  // Put RX audio sample into ring buffer. This is the (only) producer,
  // called from the RX thread, and may update inpt.
  //
  TCI_RX_AUDIO_RING *ring;
  static int wakeup_count = TCI_RX_AUDIO_FRAME_FRAMES;
  if (id < 0 || id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  ring = &tci_rx_audio_ring[id];
  int newpt = (ring->inpt + 1) & TCI_RX_AUDIO_RING_MASK;
  if (newpt != ring->outpt) {
    // buffer space available, put in
    ring->samples[2 * ring->inpt    ] = (float) left;
    ring->samples[2 * ring->inpt + 1] = (float) right;
    MEMORY_BARRIER;
    ring->inpt = newpt;
  }
  if (--wakeup_count <= 0) {
    //
    // If both RX are active sending audio, this will
    // do the wake-up more often than needed. tci_audio_wakeup()
    // notifies LWS that there is "something to write".
    //
    tci_audio_wakeup();
    wakeup_count = TCI_RX_AUDIO_FRAME_FRAMES;
  }
}

unsigned int tci_audio_get_frame (int receiver_id, TCI_STREAM *stream, size_t frame_size, size_t *frame_len) {
  //
  // Retrieve up to TCI_RX_AUDIO_FRAME_FRAMES stereo samples from RX audio ring buffer, and form
  // a valid TCI_STREAM data structure therefrom
  //
  // Called from the LWS server, no mutex should be necessary
  // This is a consumer so outpt is updated only.
  //
  if (frame_len != NULL) { *frame_len = 0; }
  if (stream == NULL || frame_len == NULL || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }
  //
  // Retrieve up to TCI_RX_AUDIO_FRAME_FRAMES from RX ring buffer and put into <out>
  //
  TCI_RX_AUDIO_RING *ring = &tci_rx_audio_ring[receiver_id];
  g_mutex_lock(&ring->mutex);  // locks every 11 ms
  int frames = (ring->inpt - ring->outpt) & TCI_RX_AUDIO_RING_MASK;
  if (frames > TCI_RX_AUDIO_FRAME_FRAMES) {
    frames = TCI_RX_AUDIO_FRAME_FRAMES;
  }
  size_t len = sizeof(TCI_STREAM_HEADER) + 2 * frames * sizeof(float);
  if (len > frame_size || frames <= 0) {
    g_mutex_unlock(&ring->mutex);
    return 0;
  }
  *frame_len = len;
  memset (stream, 0, sizeof(TCI_STREAM_HEADER));
  stream->header.receiver = (uint32_t) receiver_id;
  stream->header.sample_rate = TCI_AUDIO_SAMPLE_RATE;
  stream->header.format = TCI_AUDIO_FORMAT_FLOAT32;
  stream->header.length = (uint32_t) (2 * frames);
  stream->header.type = TCI_STREAM_RX_AUDIO;
  stream->header.channels = 2;
  float *out = stream->audio;
  int newpt = ring->outpt;
  for (int i = 0; i < frames; i++) {
    *out++ = ring->samples[2 * newpt    ];
    *out++ = ring->samples[2 * newpt + 1];
    newpt = (newpt + 1) & TCI_RX_AUDIO_RING_MASK;
  }
  MEMORY_BARRIER;
  ring->outpt = newpt;
  g_mutex_unlock(&ring->mutex);
  return frames;
}

void tci_audio_handle_tx_frame (const TCI_STREAM *stream, size_t len) {
  size_t sample_count;
  if (stream == NULL || len < sizeof(TCI_STREAM_HEADER)) { return; }
  if (stream->header.type != TCI_STREAM_TX_AUDIO) { return; }
  sample_count = (size_t) stream->header.length;
  if (sample_count < 2) { return; }
  if (len <  sizeof(TCI_STREAM_HEADER) + sizeof (float) * sample_count) { return; }
  //
  // Reduce TX audio to mono and put into ring buffer
  //
  const float *fps = stream->audio;
  int frames = sample_count / 2;  // number of MONO samples to copy
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex); // locks every 11 ms

  for (int i = 0; i < frames; i++) {
    int newpt = (ring->inpt + 1) & TCI_TX_AUDIO_RING_MASK;
    if (newpt == ring->outpt) {
      // ring buffer full
      break;
    }
    ring->samples[ring->inpt] = *fps++; // use left sample
    ring->inpt = newpt;
    fps++; // skip right sample
  }
  g_mutex_unlock (&ring->mutex);
}

double tci_get_next_mic_sample() {
  static int tci_tx_prebuffering = 1;
  const int tci_tx_prebuffer_frames = 4096;
  double sample = 0.0;
  static int tci_chrono_counter = 1;
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  //
  // send chrono frame every TCI_TX_AUDIO_FRAME_FRAMES samples
  //
  if (--tci_chrono_counter <= 0) {
    tci_send_chrono_frame();
    tci_chrono_counter = TCI_TX_AUDIO_FRAME_FRAMES;
  }
  //
  // If samples are in cache, return without involving a mutex
  //
  if (ring->cache_pos < ring->cache_len) {
    return (double) ring->cache[ring->cache_pos++];
  }
  g_mutex_lock(&ring->mutex); // locks every 11 ms
  //
  // If we arrive here, the cache is empty
  //
  ring->cache_pos = 0;
  ring->cache_len = 0;
  int available = (ring->inpt - ring->outpt) & TCI_TX_AUDIO_RING_MASK;
  if (available <= 0) {
    tci_tx_prebuffering = 1;
  } else if (available >= tci_tx_prebuffer_frames) {
    tci_tx_prebuffering = 0;
  }
  if (!tci_tx_prebuffering) {
    //
    // Copy up to TCI_TX_AUDIO_FRAME_FRAMES samples from ring buffer to cache
    // and return first sample from cache.
    //
    int newpt = ring->outpt;
    while (ring->cache_len < TCI_TX_AUDIO_FRAME_FRAMES && newpt != ring->inpt) {
      ring->cache[ring->cache_len++] = ring->samples[newpt];
      newpt = (newpt + 1) & TCI_TX_AUDIO_RING_MASK;
    }
    ring->outpt = newpt;
    if (ring->cache_pos < ring->cache_len) {
      sample = ring->cache[ring->cache_pos++];
    } else {
      // if we arrive here, both cache and ring buffer are
      // empty, so a new pre-buffering phase will begin
      t_print("%s: underrun\n", __func__);
    }
  }
  g_mutex_unlock(&ring->mutex);
  return sample;
}
