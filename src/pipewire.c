/* Copyright (C)
 *  2026 - Brendan Minish & Antigravity AI Coding Assistant / Google DeepMind
 *  2026 - Christoph van Wüllen, DL1YCF
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

#include <gtk/gtk.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>
#include <spa/pod/builder.h>

#include "audio.h"
#include "client_server.h"
#include "message.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "vfo.h"
#include "atomic.h"

#define RING_BUFFER_SIZE 16384   // ring buffer for RX audio
#define RING_BUFFER_MASK 16383
#define ST_BUFFER_SIZE    2048   // ring buffer for side tone
#define ST_BUFFER_MASK    2047
#define MIC_BUFFER_SIZE   8192   // ring buffer for TX audio
#define MIC_BUFFER_MASK   8191

#define AUDIO_LAT_LOW      512   // RX audio low water mark
#define AUDIO_LAT_TARGET  8192   // RX audio target latency
#define AUDIO_LAT_HIGH   15872   // RX audio high water mark
#define CW_LAT_LOW         224   // sidetone low water mark
#define CW_LAT_TARGET      256   // sidetone target latency
#define CW_LAT_HIGH        288   // sidetone high water mark

int n_input_devices;
int n_output_devices;

AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

struct pipewire_handle {
  struct pw_thread_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct pw_stream *stream;  // can be input or output
  RECEIVER *rx;              // only used for output streams
  TRANSMITTER *tx;           // only used for input streams
};

static void on_discovery_timeout(void *data, uint64_t expirations) {
  struct pw_main_loop *loop = data;
  pw_main_loop_quit(loop);
}

static void registry_event_global(void *data, uint32_t id, uint32_t permissions,
                                  const char *type, uint32_t version,
                                  const struct spa_dict *props) {
  if (props == NULL) return;
  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char *desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (media_class && name && desc) {
      if (strcmp(media_class, "Audio/Sink") == 0) {
        if (n_output_devices < MAX_AUDIO_DEVICES) {
          output_devices[n_output_devices].name = g_strdup(name);
          output_devices[n_output_devices].description = g_strdup(desc);
          output_devices[n_output_devices].channels = 2;  // force STEREO
          n_output_devices++;
        }
      } else if (strcmp(media_class, "Audio/Source") == 0) {
        if (n_input_devices < MAX_AUDIO_DEVICES) {
          input_devices[n_input_devices].name = g_strdup(name);
          input_devices[n_input_devices].description = g_strdup(desc);
          input_devices[n_input_devices].channels = 1;  // force MONO
          n_input_devices++;
        }
      }
    }
  }
}

void audio_get_cards() {
  n_input_devices = 0;
  n_output_devices = 0;
  pw_init(NULL, NULL);

  struct pw_main_loop *loop = pw_main_loop_new(NULL);
  if (!loop) return;
  struct pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), NULL, 0);
  if (!context) {
    pw_main_loop_destroy(loop);
    return;
  }
  struct pw_core *core = pw_context_connect(context, NULL, 0);
  if (!core) {
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
    return;
  }

  struct pw_registry *registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  struct spa_hook registry_listener;
  static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
  };
  pw_registry_add_listener(registry, &registry_listener, &registry_events, NULL);

  struct spa_source *timer = pw_loop_add_timer(pw_main_loop_get_loop(loop), on_discovery_timeout, loop);
  struct timespec value, interval;
  value.tv_sec = 0;
  value.tv_nsec = 150 * 1000 * 1000; // 150ms timeout
  interval.tv_sec = 0;
  interval.tv_nsec = 0;
  pw_loop_update_timer(pw_main_loop_get_loop(loop), timer, &value, &interval, false);

  pw_main_loop_run(loop);

  spa_hook_remove(&registry_listener);
  pw_proxy_destroy((struct pw_proxy*)registry);
  pw_core_disconnect(core);
  pw_context_destroy(context);
  pw_main_loop_destroy(loop);

  for (int i = 0; i < n_input_devices; i++) {
    t_print("PipeWire Input: %s (%s)\n", input_devices[i].description, input_devices[i].name);
  }
  for (int i = 0; i < n_output_devices; i++) {
    t_print("PipeWire Output: %s (%s)\n", output_devices[i].description, output_devices[i].name);
  }
}

static void pw_out_cb(void *data) {
  const struct pipewire_handle *h = data;
  RECEIVER *rx = h->rx;
  struct pw_buffer *b;
  struct spa_buffer *buf;
  float *samples;
  uint32_t n_frames;

  if ((b = pw_stream_dequeue_buffer(h->stream)) == NULL) {
    return;
  }

  buf = b->buffer;
  samples = buf->datas[0].data;
  if (!samples) return;

  uint32_t max_size = buf->datas[0].maxsize;
  n_frames = max_size / (2 * sizeof(float));
  if (b->requested && b->requested < n_frames) {
    n_frames = b->requested;
  }

  for (uint32_t i = 0; i < n_frames; i++) {
    double rx_left = 0.0;
    double rx_right = 0.0;
    double st_sample = 0.0;
    int oldpt;

    oldpt = rx->audio_buffer_outpt;
    if (oldpt != rx->audio_buffer_inpt) {
      rx_left = rx->audio_buffer[oldpt * 2];
      rx_right = rx->audio_buffer[oldpt * 2 + 1];
      if (rx->cwaudio == 3) {
        rx_left *= rx->audiodamp;
        rx_right *= rx->audiodamp;
        rx->audiodamp *= 0.999;
      }
      MEMORY_BARRIER;
      rx->audio_buffer_outpt = (oldpt + 1) & RING_BUFFER_MASK;
    }

    oldpt = rx->st_buffer_outpt;
    if (oldpt != rx->st_buffer_inpt) {
      st_sample = rx->st_buffer[oldpt];
      MEMORY_BARRIER;
      rx->st_buffer_outpt = (oldpt + 1) & ST_BUFFER_MASK;
    }

    samples[i * 2] = (float)(rx_left + st_sample);
    samples[i * 2 + 1] = (float)(rx_right + st_sample);
  }

  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->size = n_frames * 2 * sizeof(float);
  buf->datas[0].chunk->stride = 2 * sizeof(float);

  pw_stream_queue_buffer(h->stream, b);
}

static void pw_in_cb(void *data) {
  const struct pipewire_handle *h = data;
  TRANSMITTER *tx = h->tx;
  struct pw_buffer *b;
  struct spa_buffer *buf;
  const float *samples;
  uint32_t n_frames;

  if ((b = pw_stream_dequeue_buffer(h->stream)) == NULL) {
    return;
  }

  buf = b->buffer;
  samples = buf->datas[0].data;
  if (!samples) {
    pw_stream_queue_buffer(h->stream, b);
    return;
  }

  n_frames = buf->datas[0].chunk->size / sizeof(float);

  if (tx->audio_buffer != NULL) {
    int inpt = tx->audio_buffer_inpt;

    for (uint32_t i = 0; i < n_frames; i++) {
      int newpt = (inpt + 1) & MIC_BUFFER_MASK;
      if (newpt != tx->audio_buffer_outpt) {
        tx->audio_buffer[inpt] = (double) samples[i];
        inpt = newpt;
      }
    }
    MEMORY_BARRIER;
    tx->audio_buffer_inpt = inpt;
  }

  pw_stream_queue_buffer(h->stream, b);
}

int audio_open_output(RECEIVER *rx) {
  t_print("%s RX%d:%s\n", __func__, rx->id + 1, rx->audio_name);

  int err = 1;
  for (int i = 0; i < n_output_devices; i++) {
    if (!strcmp(rx->audio_name, output_devices[i].name)) {
      rx->local_audio_channels = output_devices[i].channels;
      err = 0;
      break;
    }
  }
  if (err) {
    t_print("%s: not registered: %s\n", __func__, rx->audio_name);
    return -1;
  }

  g_mutex_lock(&rx->audio_mutex);

  rx->audio_buffer = NULL;
  rx->st_buffer = NULL;
  rx->audio_handle = NULL;

  double *aubuf = g_new(double, rx->local_audio_channels * RING_BUFFER_SIZE);
  double *stbuf = g_new(double, ST_BUFFER_SIZE);
  struct pipewire_handle *h = g_new0(struct pipewire_handle, 1);

  if (aubuf == NULL || stbuf == NULL || h == NULL) {
    g_free(aubuf);
    g_free(stbuf);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  h->rx = rx;
  h->loop = pw_thread_loop_new("pihpsdr-playback", NULL);
  if (!h->loop) {
    g_free(aubuf);
    g_free(stbuf);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  h->context = pw_context_new(pw_thread_loop_get_loop(h->loop), NULL, 0);
  if (!h->context) {
    pw_thread_loop_destroy(h->loop);
    g_free(aubuf);
    g_free(stbuf);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  if (pw_thread_loop_start(h->loop) < 0) {
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(aubuf);
    g_free(stbuf);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  pw_thread_loop_lock(h->loop);

  h->core = pw_context_connect(h->context, NULL, 0);
  if (!h->core) {
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(aubuf);
    g_free(stbuf);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  // Playback Stream properties
  struct pw_properties *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, "DSP",
      PW_KEY_NODE_NAME, "pihpsdr-rx",
      PW_KEY_NODE_DESCRIPTION, "piHPSDR Playback",
      PW_KEY_TARGET_OBJECT, rx->audio_name,
      PW_KEY_NODE_LATENCY, "128/48000",
      NULL
  );

  static const struct pw_stream_events stream_events = {
      PW_VERSION_STREAM_EVENTS,
      .process = pw_out_cb,
  };

  h->stream = pw_stream_new_simple(pw_thread_loop_get_loop(h->loop),
                                   "pihpsdr-playback",
                                   props,
                                   &stream_events,
                                   h);

  if (!h->stream) {
    pw_core_disconnect(h->core);
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_free(aubuf);
    g_free(stbuf);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  struct spa_audio_info_raw info = {
      .format = SPA_AUDIO_FORMAT_F32,
      .rate = 48000,
      .channels = 2,
  };
  info.position[0] = SPA_AUDIO_CHANNEL_MONO;
  info.position[1] = SPA_AUDIO_CHANNEL_MONO + 1;

  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  int res = pw_stream_connect(h->stream,
                              PW_DIRECTION_OUTPUT,
                              PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT |
                              PW_STREAM_FLAG_MAP_BUFFERS |
                              PW_STREAM_FLAG_RT_PROCESS,
                              params, 1);
  if (res < 0) {
    pw_stream_destroy(h->stream);
    pw_core_disconnect(h->core);
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(aubuf);
    g_free(stbuf);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  pw_thread_loop_unlock(h->loop);

  rx->audio_buffer_offset = 0;
  rx->audio_buffer_inpt = 0;
  rx->audio_buffer_outpt = 0;
  rx->st_buffer_inpt = 0;
  rx->st_buffer_outpt = 0;

  rx->audio_buffer = aubuf;
  rx->st_buffer = stbuf;
  rx->audio_handle = h;
  rx->cwaudio = 5;
  rx->cwcount = 0;
  rx->skipcnt = 0;
  rx->queued = 0;

  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

void audio_close_output(RECEIVER *rx) {
  t_print("%s RX%d:%s\n", __func__, rx->id + 1, rx->audio_name);
  g_mutex_lock(&rx->audio_mutex);

  struct pipewire_handle *h = rx->audio_handle;
  if (h != NULL) {
    pw_thread_loop_stop(h->loop);
    if (h->stream) pw_stream_destroy(h->stream);
    pw_core_disconnect(h->core);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    rx->audio_handle = NULL;
  }
  g_free(rx->audio_handle);
  g_free(rx->audio_buffer);
  g_free(rx->st_buffer);
  rx->audio_handle = NULL;
  rx->audio_buffer = NULL;
  rx->st_buffer = NULL;
  g_mutex_unlock(&rx->audio_mutex);
}

int audio_open_input(TRANSMITTER *tx) {
  t_print("%s TX:%s\n", __func__, tx->audio_name);

  int err = 1;
  for (int i = 0; i < n_input_devices; i++) {
    if (!strcmp(tx->audio_name, input_devices[i].name)) {
      err = 0;
      break;
    }
  }
  if (err) {
    t_print("%s: not registered: %s\n", __func__, tx->audio_name);
    return -1;
  }

  g_mutex_lock(&tx->audio_mutex);
  tx->audio_handle = NULL;
  tx->audio_buffer = NULL;

  double *aub = g_new(double, MIC_BUFFER_SIZE);
  struct pipewire_handle *h = g_new0(struct pipewire_handle, 1);

  if (aub == NULL || h == NULL) {
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  h->tx = tx;
  h->loop = pw_thread_loop_new("pihpsdr-capture", NULL);
  if (!h->loop) {
    g_free(h);
    g_free(aub);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  h->context = pw_context_new(pw_thread_loop_get_loop(h->loop), NULL, 0);
  if (!h->context) {
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_free(aub);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  if (pw_thread_loop_start(h->loop) < 0) {
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_free(aub);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  pw_thread_loop_lock(h->loop);

  h->core = pw_context_connect(h->context, NULL, 0);
  if (!h->core) {
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_free(aub);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  struct pw_properties *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "DSP",
      PW_KEY_NODE_NAME, "piHPSDR capture",
      PW_KEY_TARGET_OBJECT, tx->audio_name,
      PW_KEY_NODE_LATENCY, "256/48000",
      NULL
  );

  static const struct pw_stream_events stream_events = {
      PW_VERSION_STREAM_EVENTS,
      .process = pw_in_cb,
  };

  h->stream = pw_stream_new_simple(pw_thread_loop_get_loop(h->loop),
                                   "pihpsdr-capture-stream",
                                   props,
                                   &stream_events,
                                   h);
  if (!h->stream) {
    pw_core_disconnect(h->core);
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_free(aub);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  struct spa_audio_info_raw info = {
      .format = SPA_AUDIO_FORMAT_F32,
      .rate = 48000,
      .channels = 1,
  };
  info.position[0] = SPA_AUDIO_CHANNEL_MONO;

  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  int res = pw_stream_connect(h->stream,
                              PW_DIRECTION_INPUT,
                              PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT |
                              PW_STREAM_FLAG_MAP_BUFFERS |
                              PW_STREAM_FLAG_RT_PROCESS,
                              params, 1);
  if (res < 0) {
    pw_stream_destroy(h->stream);
    pw_core_disconnect(h->core);
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_free(aub);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  pw_thread_loop_unlock(h->loop);

  tx->audio_buffer = aub;
  tx->audio_handle = h;
  tx->audio_buffer_inpt = 0;
  tx->audio_buffer_outpt = 0;
  g_mutex_unlock(&tx->audio_mutex);
  return 0;
}

void audio_close_input(TRANSMITTER *tx) {
  t_print("%s TX:%s\n", __func__, tx->audio_name);
  g_mutex_lock(&tx->audio_mutex);
  struct pipewire_handle *h = tx->audio_handle;
  if (h != NULL) {
    pw_thread_loop_stop(h->loop);
    pw_stream_destroy(h->stream);
    pw_core_disconnect(h->core);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    tx->audio_handle = NULL;
  }

  if (tx->audio_buffer != NULL) {
    g_free(tx->audio_buffer);
    tx->audio_buffer = NULL;
  }
  g_mutex_unlock(&tx->audio_mutex);
}

double audio_get_next_mic_sample(TRANSMITTER *tx) {
  double sample;
  g_mutex_lock(&tx->audio_mutex);

  if ((tx->audio_buffer == NULL) || (tx->audio_buffer_outpt == tx->audio_buffer_inpt)) {
    sample = 0.0;
  } else {
    int newpt = (tx->audio_buffer_outpt + 1) & MIC_BUFFER_MASK;
    sample = tx->audio_buffer[tx->audio_buffer_outpt];
    MEMORY_BARRIER;
    tx->audio_buffer_outpt = newpt;
  }

  g_mutex_unlock(&tx->audio_mutex);
  return sample;
}

void audio_write(RECEIVER *rx, double left, double right) {
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return; }
  if (rx->audio_handle == NULL || rx->audio_buffer == NULL) { return; }

  g_mutex_lock(&rx->audio_mutex);
  double *buffer = rx->audio_buffer;
  rx->cwaudio = 0;

  int avail = (rx->audio_buffer_inpt - rx->audio_buffer_outpt) & RING_BUFFER_MASK;

  if (avail < AUDIO_LAT_LOW) {
    //
    // Running the RX-audio for a very long time
    // and with audio hardware whose "48000 Hz" are a little faster than the "48000 Hz" of
    // the SDR will very slowly drain the buffer. We recover from this by brutally
    // inserting half a buffer's length of silence.
    //
    // This is not always an "error" to be reported and necessarily happens if ...
    //  a) we come here for the first time
    //  b) we come from a TX/RX transition where the buffer ran empty during TX
    //
    int inpt = rx->audio_buffer_inpt;
    for (int i = 0; i < AUDIO_LAT_TARGET - avail; i++) {
      buffer[2 * inpt] = 0.0;
      buffer[2 * inpt + 1] = 0.0;
      inpt = (inpt + 1) & RING_BUFFER_MASK;
    }
    MEMORY_BARRIER;
    rx->audio_buffer_inpt = inpt;
    // Now buffer filling is exactly at AUDIO_LAT_TARGET
  }

  if (avail > AUDIO_LAT_HIGH) {
    //
    // Running the RX-audio for a very long time
    // and with audio hardware whose "48000 Hz" are a little slower than the "48000 Hz" of
    // the SDR will very slowly fill the buffer. This should be the only situation where
    // this "buffer overrun" condition should occur. We recover from this by brutally
    // deleting half a buffer size of audio, such that the next overrun is in the distant
    // future.
    //
    rx->audio_buffer_inpt = (rx->audio_buffer_inpt - avail + AUDIO_LAT_TARGET) & RING_BUFFER_MASK;
    // Now buffer filling ids exactly at AUDIO_LAT_TARGET
  }

  int newpt = (rx->audio_buffer_inpt + 1) & RING_BUFFER_MASK;

  if (newpt  != rx->audio_buffer_outpt) {
    rx->audio_buffer[rx->audio_buffer_inpt * 2] = left;
    rx->audio_buffer[rx->audio_buffer_inpt * 2 + 1] = right;
    MEMORY_BARRIER;
    rx->audio_buffer_inpt = newpt;
  }

  g_mutex_unlock(&rx->audio_mutex);
}

void tx_audio_write(RECEIVER *rx, double sample) {
  g_mutex_lock(&rx->audio_mutex);
  if (rx->st_buffer == NULL) {
    g_mutex_unlock(&rx->audio_mutex);
    return;
  }
  int inpt = rx->st_buffer_inpt;
  int newpt;
  int avail = (inpt - rx->st_buffer_outpt) & ST_BUFFER_MASK;
  int adjust = 0;

  if (rx->cwaudio != 3) {
    // Transition RX -> TX
    if (avail < CW_LAT_TARGET) {
      for (int i = 0; i < CW_LAT_TARGET - avail; i++) {
        rx->st_buffer[inpt] = 0.0;
        inpt = (inpt + 1) & ST_BUFFER_MASK;
      }
      MEMORY_BARRIER;
      rx->st_buffer_inpt = inpt;
    }
    rx->audiodamp = 1.0;
    rx->cwaudio = 3;
    rx->cwcount = 0;
    avail = CW_LAT_TARGET;
  }

  if (sample != 0.0) { rx->cwcount = 0; }
  if (++rx->cwcount > 16) {
    rx->cwcount = 0;
    //
    // We arrive here if we have seen 16 zero samples in a row.
    //
    if (avail > CW_LAT_HIGH) { adjust = 2; } // full: we are above high water mark
    if (avail < CW_LAT_LOW)  { adjust = 1; } // low: we are below low water mark
  }
  switch(adjust) {
  case 0:
    // Write sample directly to sidetone buffer
    newpt = (inpt + 1) & ST_BUFFER_MASK;
    if (newpt != rx->st_buffer_outpt) {
      // buffer space available
      rx->st_buffer[inpt] = sample;
      MEMORY_BARRIER;
      rx->st_buffer_inpt = newpt;
    }
    break;
  case 1:
    // We just saw 16 silence samples and buffer filling is low:
    // insert one extra
    rx->st_buffer[inpt] = 0.0;;
    inpt = (inpt + 1) & ST_BUFFER_MASK;
    rx->st_buffer[inpt] = 0.0;;
    inpt = (inpt + 1) & ST_BUFFER_MASK;
    MEMORY_BARRIER;
    rx->st_buffer_inpt = inpt;
    break;
  default:
    // We just saw 16 silence samples and buffer filling is high:
    // just skip current "silent" sample (do nothing)
    break;
  }

  g_mutex_unlock(&rx->audio_mutex);
}
