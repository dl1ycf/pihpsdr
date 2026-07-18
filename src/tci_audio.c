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

#include "message.h"

#include "receiver.h"
#include "tci_audio.h"
#include "tci.h"

typedef struct _tci_rx_audio_ring {
  GMutex mutex;
  float samples[TCI_RX_AUDIO_RING_FRAMES * TCI_AUDIO_CHANNELS];
  unsigned int write_count;
  unsigned int dropped;
} TCI_RX_AUDIO_RING;

typedef struct _tci_tx_audio_ring {
  GMutex mutex;
  float samples[TCI_TX_AUDIO_RING_FRAMES];
  unsigned int write_count;
  unsigned int read_count;
  unsigned int dropped;
} TCI_TX_AUDIO_RING;

static TCI_RX_AUDIO_RING tci_rx_audio_ring[TCI_RX_AUDIO_MAX_RECEIVERS];
static TCI_TX_AUDIO_RING tci_tx_audio_ring;
static unsigned int tci_rx_audio_wakeup_count = 0;
static unsigned int tci_tx_audio_frames = 0;
static TCI_AUDIO_WAKEUP_CALLBACK tci_audio_wakeup_callback = NULL;

void tci_audio_tx_reset (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex);
  ring->write_count = 0;
  ring->read_count = 0;
  ring->dropped = 0;
  memset (ring->samples, 0, sizeof (ring->samples));
  g_mutex_unlock (&ring->mutex);
}

static void tci_audio_tx_push_block (const float* samples, unsigned int frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  if (samples == NULL || frames == 0) { return; }
  if (!g_mutex_trylock (&ring->mutex)) { return; }
  for (unsigned int i = 0; i < frames; i++) {
    unsigned int index;
    if (ring->write_count >= ring->read_count + TCI_TX_AUDIO_RING_FRAMES) {
      ring->read_count = ring->write_count - TCI_TX_AUDIO_RING_FRAMES + 1;
      ring->dropped++;
    }
    index = (unsigned int) (ring->write_count % TCI_TX_AUDIO_RING_FRAMES);
    ring->samples[index] = samples[i];
    ring->write_count++;
  }
  g_mutex_unlock (&ring->mutex);
}

unsigned int tci_audio_tx_available (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  unsigned int available;
  g_mutex_lock (&ring->mutex);
  available = ring->write_count - ring->read_count;
  g_mutex_unlock (&ring->mutex);
  return available;
}

unsigned int tci_audio_tx_read (float* out, unsigned int frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  unsigned int copied = 0;
  if (out == NULL || frames == 0) { return 0; }
  memset (out, 0, frames * sizeof (float));
  g_mutex_lock (&ring->mutex);
  while (copied < frames && ring->read_count < ring->write_count) {
    unsigned int index = (unsigned int) (ring->read_count % TCI_TX_AUDIO_RING_FRAMES);
    out[copied++] = ring->samples[index];
    ring->read_count++;
  }
  g_mutex_unlock (&ring->mutex);
  return copied;
}

void tci_audio_set_wakeup_callback (TCI_AUDIO_WAKEUP_CALLBACK callback) {
  tci_audio_wakeup_callback = callback;
}

void tci_audio_rx_sample (int id, double left, double right) {
  TCI_RX_AUDIO_RING *ring;
  unsigned int index;
  int do_wakeup = 0;
  if (id < 0 || id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  ring = &tci_rx_audio_ring[id];
  if (!g_mutex_trylock (&ring->mutex)) { return; }
  index = (unsigned int) (ring->write_count % TCI_RX_AUDIO_RING_FRAMES);
  ring->samples[ (index * TCI_AUDIO_CHANNELS)] = (float) left;
  ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1] = (float) right;
  ring->write_count++;
  if ((++tci_rx_audio_wakeup_count % TCI_RX_AUDIO_FRAME_FRAMES) == 0) {
    do_wakeup = 1;
  }
  g_mutex_unlock (&ring->mutex);
  if (do_wakeup && tci_audio_wakeup_callback != NULL) {
    tci_audio_wakeup_callback();
  }
}

unsigned int tci_audio_get_write_count (int receiver_id) {
  unsigned int write_count = 0;
  TCI_RX_AUDIO_RING *ring;
  if (receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }
  ring = &tci_rx_audio_ring[receiver_id];
  g_mutex_lock (&ring->mutex);
  write_count = ring->write_count;
  g_mutex_unlock (&ring->mutex);
  return write_count;
}

static unsigned int tci_audio_copy (int receiver_id, unsigned int *read_count, float* out, unsigned int max_frames) {
  TCI_RX_AUDIO_RING *ring;
  unsigned int available;
  unsigned int frames;
  if (read_count == NULL || out == NULL || receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }
  ring = &tci_rx_audio_ring[receiver_id];
  if (!g_mutex_trylock (&ring->mutex)) { return 0; }
  if (*read_count + TCI_RX_AUDIO_RING_FRAMES < ring->write_count) {
    *read_count = ring->write_count - TCI_RX_AUDIO_RING_FRAMES;
    ring->dropped++;
  }
  available = ring->write_count - *read_count;
  frames = (available < max_frames) ? (unsigned int) available : max_frames;
  for (unsigned int i = 0; i < frames; i++) {
    unsigned int index = (unsigned int) ((*read_count + i) % TCI_RX_AUDIO_RING_FRAMES);
    out[ (i * TCI_AUDIO_CHANNELS)] = ring->samples[ (index * TCI_AUDIO_CHANNELS)];
    out[ (i * TCI_AUDIO_CHANNELS) + 1] = ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1];
  }
  *read_count += frames;
  g_mutex_unlock (&ring->mutex);
  return frames;
}


unsigned int tci_audio_get_frame (int receiver_id, unsigned int *read_count, TCI_STREAM *stream, size_t frame_size,
                                  size_t *frame_len) {
  unsigned int frames;
  size_t len;
  if (frame_len != NULL) { *frame_len = 0; }
  if (read_count == NULL || stream == NULL || frame_len == NULL) { return 0; }
  frames = tci_audio_copy (receiver_id, read_count, stream->audio, TCI_RX_AUDIO_FRAME_FRAMES);
  if (frames == 0) { return 0; }
  len = sizeof(TCI_STREAM_HEADER) + frames * TCI_AUDIO_CHANNELS * sizeof(float);
  if (len > frame_size) { return 0; }
  *frame_len = len;
  memset (stream, 0, sizeof(TCI_STREAM_HEADER));
  stream->header.receiver = (uint32_t) receiver_id;
  stream->header.sample_rate = TCI_AUDIO_SAMPLE_RATE;
  stream->header.format = TCI_AUDIO_FORMAT_FLOAT32;
  stream->header.length = (uint32_t) (frames * TCI_AUDIO_CHANNELS);
  stream->header.type = TCI_STREAM_RX_AUDIO;
  stream->header.channels = TCI_AUDIO_CHANNELS;
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
  float samples[TCI_TX_AUDIO_FRAME_FRAMES];
  unsigned int frames = (sample_count / 2);
  if (frames > TCI_TX_AUDIO_FRAME_FRAMES) {
    frames = TCI_TX_AUDIO_FRAME_FRAMES;
  }
  const float *fps = stream->audio;
  float *fpt = samples;
  for (unsigned int i = 0; i < frames; i++) {
    *fpt++ = *fps++;  // use left sample
    fps++;            // skip right sample
  }
  tci_audio_tx_push_block (samples, frames);
  tci_tx_audio_frames++;
}

double tci_get_next_mic_sample() {
  static unsigned long tci_tx_underruns = 0;
  static int tci_tx_prebuffering = 1;
  static float tci_tx_cache[512];
  static unsigned int tci_tx_cache_len = 0;
  static unsigned int tci_tx_cache_pos = 0;
  const unsigned int tci_tx_prebuffer_frames = 4096;
  double sample = 0.0;
  tci_tx_chrono_loop();
  unsigned int cache_available = (tci_tx_cache_len > tci_tx_cache_pos) ? (tci_tx_cache_len - tci_tx_cache_pos) : 0;
  unsigned int ring_available = tci_audio_tx_available();
  if (tci_tx_prebuffering) {
    if ((ring_available + cache_available) < tci_tx_prebuffer_frames) {
    } else {
      tci_tx_prebuffering = 0;
    }
  }
  if (!tci_tx_prebuffering) {
    if (tci_tx_cache_pos >= tci_tx_cache_len) {
      tci_tx_cache_len = tci_audio_tx_read(tci_tx_cache, (unsigned int)(sizeof(tci_tx_cache) / sizeof(tci_tx_cache[0])));
      tci_tx_cache_pos = 0;
    }
    if (tci_tx_cache_pos < tci_tx_cache_len) {
      sample = (double) tci_tx_cache[tci_tx_cache_pos++];
    } else {
      tci_tx_underruns++;
      if ((tci_tx_underruns % 1000) == 0) {
        t_print("TCI TX audio underruns=%lu\n", tci_tx_underruns);
      }
      tci_tx_prebuffering = 1;
      tci_tx_cache_len = 0;
      tci_tx_cache_pos = 0;
    }
  }
  return sample;
}
