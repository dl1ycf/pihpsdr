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
  guint64 write_count;
  guint dropped;
} TCI_RX_AUDIO_RING;

typedef struct _tci_tx_audio_ring {
  GMutex mutex;
  float samples[TCI_TX_AUDIO_RING_FRAMES];
  guint64 write_count;
  guint64 read_count;
  guint dropped;
} TCI_TX_AUDIO_RING;

static TCI_RX_AUDIO_RING tci_rx_audio_ring[TCI_RX_AUDIO_MAX_RECEIVERS];
static TCI_TX_AUDIO_RING tci_tx_audio_ring;
static guint tci_rx_audio_wakeup_count = 0;
static guint64 tci_tx_audio_frames = 0;
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

static void tci_audio_tx_push_block (const float* samples, guint frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;

  if (samples == NULL || frames == 0) { return; }

  if (!g_mutex_trylock (&ring->mutex)) { return; }

  for (guint i = 0; i < frames; i++) {
    guint index;

    if (ring->write_count >= ring->read_count + TCI_TX_AUDIO_RING_FRAMES) {
      ring->read_count = ring->write_count - TCI_TX_AUDIO_RING_FRAMES + 1;
      ring->dropped++;
    }

    index = (guint) (ring->write_count % TCI_TX_AUDIO_RING_FRAMES);
    ring->samples[index] = samples[i];
    ring->write_count++;
  }

  g_mutex_unlock (&ring->mutex);
}

guint64 tci_audio_tx_available (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  guint64 available;
  g_mutex_lock (&ring->mutex);
  available = ring->write_count - ring->read_count;
  g_mutex_unlock (&ring->mutex);
  return available;
}

guint tci_audio_tx_read (float* out, guint frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  guint copied = 0;

  if (out == NULL || frames == 0) { return 0; }

  memset (out, 0, frames * sizeof (float));
  g_mutex_lock (&ring->mutex);

  while (copied < frames && ring->read_count < ring->write_count) {
    guint index = (guint) (ring->read_count % TCI_TX_AUDIO_RING_FRAMES);
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
  guint index;
  int do_wakeup = 0;

  if (id < 0 || id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }

  ring = &tci_rx_audio_ring[id];

  if (!g_mutex_trylock (&ring->mutex)) { return; }

  index = (guint) (ring->write_count % TCI_RX_AUDIO_RING_FRAMES);
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

guint64 tci_audio_get_write_count (int receiver_id) {
  guint64 write_count = 0;
  TCI_RX_AUDIO_RING *ring;

  if (receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }

  ring = &tci_rx_audio_ring[receiver_id];
  g_mutex_lock (&ring->mutex);
  write_count = ring->write_count;
  g_mutex_unlock (&ring->mutex);
  return write_count;
}

static guint tci_audio_copy (int receiver_id, guint64 *read_count, float* out, guint max_frames) {
  TCI_RX_AUDIO_RING *ring;
  guint64 available;
  guint frames;

  if (read_count == NULL || out == NULL || receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }

  ring = &tci_rx_audio_ring[receiver_id];

  if (!g_mutex_trylock (&ring->mutex)) { return 0; }

  if (*read_count + TCI_RX_AUDIO_RING_FRAMES < ring->write_count) {
    *read_count = ring->write_count - TCI_RX_AUDIO_RING_FRAMES;
    ring->dropped++;
  }

  available = ring->write_count - *read_count;
  frames = (available < max_frames) ? (guint) available : max_frames;

  for (guint i = 0; i < frames; i++) {
    guint index = (guint) ((*read_count + i) % TCI_RX_AUDIO_RING_FRAMES);
    out[ (i * TCI_AUDIO_CHANNELS)] = ring->samples[ (index * TCI_AUDIO_CHANNELS)];
    out[ (i * TCI_AUDIO_CHANNELS) + 1] = ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1];
  }

  *read_count += frames;
  g_mutex_unlock (&ring->mutex);
  return frames;
}


guint tci_audio_get_frame (int receiver_id, guint64 *read_count, unsigned char* frame, size_t frame_size,
                           size_t *frame_len) {
  float audio[TCI_RX_AUDIO_FRAME_FRAMES * TCI_AUDIO_CHANNELS];
  TCI_STREAM_HEADER header;
  guint frames;
  size_t data_bytes;

  if (frame_len != NULL) {
    *frame_len = 0;
  }

  if (read_count == NULL || frame == NULL || frame_len == NULL) { return 0; }

  if (frame_size < TCI_AUDIO_RX_FRAME_MAX_BYTES) { return 0; }

  frames = tci_audio_copy (receiver_id, read_count, audio, TCI_RX_AUDIO_FRAME_FRAMES);

  if (frames == 0) { return 0; }

  memset (&header, 0, sizeof (header));
  header.receiver = (uint32_t) receiver_id;
  header.sample_rate = TCI_AUDIO_SAMPLE_RATE;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = (uint32_t) (frames * TCI_AUDIO_CHANNELS);
  header.type = TCI_STREAM_RX_AUDIO;
  header.channels = TCI_AUDIO_CHANNELS;
  data_bytes = (size_t) frames * TCI_AUDIO_CHANNELS * sizeof (float);
  memcpy (frame, &header, sizeof (header));
  memcpy (frame + sizeof (header), audio, data_bytes);
  *frame_len = sizeof (header) + data_bytes;
  return frames;
}

void tci_audio_handle_tx_frame (const unsigned char* data, size_t len) {
  TCI_STREAM_HEADER header;
  size_t payload_bytes;
  size_t sample_count;

  if (data == NULL || len < 64) { return; }

  memcpy (&header, data, sizeof (header));

  if (header.type != TCI_STREAM_TX_AUDIO) { return; }

  payload_bytes = len - 64;

  if (payload_bytes < sizeof (float)) { return; }

  if (header.length <= 0 || (64 + ((size_t) header.length * sizeof (float))) > len) {
    return;
  }

  sample_count = (size_t) header.length;

  if (sample_count < 2) { return; }

  float samples[TCI_TX_AUDIO_FRAME_FRAMES];
  float *fps, *fpt;
  guint frames = (guint) (sample_count / 2);

  if (frames > TCI_TX_AUDIO_FRAME_FRAMES) {
    frames = TCI_TX_AUDIO_FRAME_FRAMES;
  }

  //
  // If the memory from libsockets should not be aligned, we
  // make a memcpy in tci_handle_binary_lws(), so data,
  // and data+64 as well, is properly aligned for floats
  //
  fps = (float *) (data + 64); // CAST OK
  fpt = samples;

  for (guint i = 0; i < frames; i++) {
    *fpt++ = *fps++;
    fps++; // skip right sample
  }

  tci_audio_tx_push_block (samples, frames);
  tci_tx_audio_frames++;
}

double tci_get_next_mic_sample() {
  static unsigned long tci_tx_underruns = 0;
  static int tci_tx_prebuffering = 1;
  static float tci_tx_cache[512];
  static guint tci_tx_cache_len = 0;
  static guint tci_tx_cache_pos = 0;
  const guint tci_tx_prebuffer_frames = 4096;
  double sample = 0.0;
  tci_tx_chrono_loop();
  guint cache_available = (tci_tx_cache_len > tci_tx_cache_pos) ? (tci_tx_cache_len - tci_tx_cache_pos) : 0;
  guint64 ring_available = tci_audio_tx_available();

  if (tci_tx_prebuffering) {
    if ((ring_available + cache_available) < tci_tx_prebuffer_frames) {
    } else {
      tci_tx_prebuffering = 0;
    }
  }

  if (!tci_tx_prebuffering) {
    if (tci_tx_cache_pos >= tci_tx_cache_len) {
      tci_tx_cache_len = tci_audio_tx_read(tci_tx_cache, (guint)(sizeof(tci_tx_cache) / sizeof(tci_tx_cache[0])));
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
