/* Copyright (C)
 *  2026 - Brendan Minish & Antigravity AI Coding Assistant / Google DeepMind
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

#define RING_BUFFER_SIZE 65536
#define RING_BUFFER_MASK 65535
#define MICRINGLEN 8192
#define MICRINGMASK 8191

#define AUDIO_LAT_TARGET_MS 200
static const int AUDIO_LAT_TARGET_FRAMES = 48 * AUDIO_LAT_TARGET_MS; // 9600

int n_input_devices;
int n_output_devices;

AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

struct audio_ring {
  double buffer[RING_BUFFER_SIZE * 2];
  volatile int inpt;
  volatile int outpt;
};

struct pipewire_handle {
  struct pw_thread_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct pw_stream *playback_stream;
  struct pw_stream *stream; // for capture (mic)
  RECEIVER *rx;
  TRANSMITTER *tx;
  int channels;
  int is_output;

  struct audio_ring sidetone_ring;
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
          output_devices[n_output_devices].channels = 2;
          n_output_devices++;
        }
      } else if (strcmp(media_class, "Audio/Source") == 0) {
        if (n_input_devices < MAX_AUDIO_DEVICES) {
          input_devices[n_input_devices].name = g_strdup(name);
          input_devices[n_input_devices].description = g_strdup(desc);
          input_devices[n_input_devices].channels = 1;
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

static void on_playback_process(void *data) {
  struct pipewire_handle *h = data;
  RECEIVER *rx = h->rx;
  struct pw_buffer *b;
  struct spa_buffer *buf;
  float *samples;
  uint32_t n_frames;

  if ((b = pw_stream_dequeue_buffer(h->playback_stream)) == NULL) {
    return;
  }

  buf = b->buffer;
  samples = buf->datas[0].data;
  if (!samples) return;

  uint32_t max_size = buf->datas[0].maxsize;
  n_frames = max_size / (sizeof(float) * h->channels);
  if (b->requested && b->requested < n_frames) {
    n_frames = b->requested;
  }

  // 1. Pull receiver audio
  int rx_inpt = rx->audio_buffer_inpt;
  int rx_outpt = rx->audio_buffer_outpt;
  int rx_avail = (rx_inpt - rx_outpt) & RING_BUFFER_MASK;

  // 2. Pull sidetone audio
  int st_inpt = h->sidetone_ring.inpt;
  int st_outpt = h->sidetone_ring.outpt;
  int st_avail = (st_inpt - st_outpt) & RING_BUFFER_MASK;

  // RX audio is muted if break-in is ON, duplex is OFF, and we are keying (state 3)
  int mute_rx = (cw_breakin && !duplex && rx->cwaudio == 3);

  for (uint32_t i = 0; i < n_frames; i++) {
    double rx_left = 0.0;
    double rx_right = 0.0;
    double st_sample = 0.0;

    if (!mute_rx && i < (uint32_t)rx_avail) {
      int idx = (rx_outpt + i) & RING_BUFFER_MASK;
      if (h->channels == 1) {
        rx_left = rx->audio_buffer[idx];
        rx_right = rx_left;
      } else {
        rx_left = rx->audio_buffer[idx * 2];
        rx_right = rx->audio_buffer[idx * 2 + 1];
      }
    }

    if (i < (uint32_t)st_avail) {
      int idx = (st_outpt + i) & RING_BUFFER_MASK;
      st_sample = h->sidetone_ring.buffer[idx * h->channels];
    }

    if (h->channels == 1) {
      samples[i] = (float)(0.5 * (rx_left + rx_right) + st_sample);
    } else {
      samples[i * 2] = (float)(rx_left + st_sample);
      samples[i * 2 + 1] = (float)(rx_right + st_sample);
    }
  }

  if (n_frames < (uint32_t)rx_avail) {
    rx->audio_buffer_outpt = (rx_outpt + n_frames) & RING_BUFFER_MASK;
  } else {
    rx->audio_buffer_outpt = rx_inpt;
  }

  if (n_frames < (uint32_t)st_avail) {
    h->sidetone_ring.outpt = (st_outpt + n_frames) & RING_BUFFER_MASK;
  } else {
    h->sidetone_ring.outpt = st_inpt;
  }

  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->size = n_frames * sizeof(float) * h->channels;
  buf->datas[0].chunk->stride = sizeof(float) * h->channels;

  pw_stream_queue_buffer(h->playback_stream, b);
}

static void on_capture_process(void *data) {
  struct pipewire_handle *h = data;
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

  n_frames = buf->datas[0].chunk->size / (sizeof(float) * h->channels);

  if (tx->audio_buffer != NULL) {
    int inpt = tx->audio_buffer_inpt;
    int outpt = tx->audio_buffer_outpt;

    for (uint32_t i = 0; i < n_frames; i++) {
      double sample = 0.0;
      for (int c = 0; c < h->channels; c++) {
        sample += samples[i * h->channels + c];
      }
      sample /= h->channels;

      int newpt = (inpt + 1) & MICRINGMASK;
      if (newpt != outpt) {
        tx->audio_buffer[inpt] = sample;
        MEMORY_BARRIER;
        inpt = newpt;
      }
    }
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
  rx->audio_handle = NULL;
  rx->audio_buffer = NULL;

  struct pipewire_handle *h = g_new0(struct pipewire_handle, 1);
  h->rx = rx;
  h->channels = rx->local_audio_channels;
  h->is_output = 1;
  h->sidetone_ring.inpt = 0;
  h->sidetone_ring.outpt = 0;

  h->loop = pw_thread_loop_new("pihpsdr-playback", NULL);
  if (!h->loop) {
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  h->context = pw_context_new(pw_thread_loop_get_loop(h->loop), NULL, 0);
  if (!h->context) {
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  if (pw_thread_loop_start(h->loop) < 0) {
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
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
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  char latency_str[32];
  snprintf(latency_str, sizeof(latency_str), "%d/48000", rx->latency);

  // Playback Stream properties
  struct pw_properties *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, "DSP",
      PW_KEY_NODE_NAME, "pihpsdr-rx",
      PW_KEY_NODE_DESCRIPTION, "piHPSDR Playback",
      PW_KEY_TARGET_OBJECT, rx->audio_name,
      PW_KEY_NODE_LATENCY, latency_str,
      NULL
  );

  static const struct pw_stream_events stream_events = {
      PW_VERSION_STREAM_EVENTS,
      .process = on_playback_process,
  };

  h->playback_stream = pw_stream_new_simple(pw_thread_loop_get_loop(h->loop),
                                            "pihpsdr-playback",
                                            props,
                                            &stream_events,
                                            h);

  if (!h->playback_stream) {
    pw_core_disconnect(h->core);
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  struct spa_audio_info_raw info = {
      .format = SPA_AUDIO_FORMAT_F32,
      .rate = 48000,
      .channels = h->channels,
  };
  for (int i = 0; i < h->channels; i++) {
    info.position[i] = SPA_AUDIO_CHANNEL_MONO + i;
  }

  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  int res = pw_stream_connect(h->playback_stream,
                              PW_DIRECTION_OUTPUT,
                              PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT |
                              PW_STREAM_FLAG_MAP_BUFFERS |
                              PW_STREAM_FLAG_RT_PROCESS,
                              params, 1);
  if (res < 0) {
    pw_stream_destroy(h->playback_stream);
    pw_core_disconnect(h->core);
    pw_thread_loop_unlock(h->loop);
    pw_thread_loop_stop(h->loop);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  pw_thread_loop_unlock(h->loop);

  rx->audio_buffer_offset = 0;
  rx->audio_buffer_inpt = 0;
  rx->audio_buffer_outpt = 0;
  rx->audio_buffer = g_new0(double, rx->local_audio_channels * RING_BUFFER_SIZE);

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
    if (h->playback_stream) pw_stream_destroy(h->playback_stream);
    pw_core_disconnect(h->core);
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    rx->audio_handle = NULL;
  }

  if (rx->audio_buffer != NULL) {
    g_free(rx->audio_buffer);
    rx->audio_buffer = NULL;
  }

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

  struct pipewire_handle *h = g_new0(struct pipewire_handle, 1);
  h->tx = tx;
  h->channels = 1;
  h->is_output = 0;

  h->loop = pw_thread_loop_new("pihpsdr-capture", NULL);
  if (!h->loop) {
    g_free(h);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  h->context = pw_context_new(pw_thread_loop_get_loop(h->loop), NULL, 0);
  if (!h->context) {
    pw_thread_loop_destroy(h->loop);
    g_free(h);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  if (pw_thread_loop_start(h->loop) < 0) {
    pw_context_destroy(h->context);
    pw_thread_loop_destroy(h->loop);
    g_free(h);
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
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  struct pw_properties *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "DSP",
      PW_KEY_NODE_NAME, "piHPSDR capture",
      PW_KEY_TARGET_OBJECT, tx->audio_name,
      PW_KEY_NODE_LATENCY, "512/48000",
      NULL
  );

  static const struct pw_stream_events stream_events = {
      PW_VERSION_STREAM_EVENTS,
      .process = on_capture_process,
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
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  struct spa_audio_info_raw info = {
      .format = SPA_AUDIO_FORMAT_F32,
      .rate = 48000,
      .channels = h->channels,
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
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  pw_thread_loop_unlock(h->loop);

  tx->audio_buffer = g_new0(double, MICRINGLEN);
  tx->audio_buffer_inpt = 0;
  tx->audio_buffer_outpt = 0;
  tx->audio_handle = h;
  tx->audio_running = TRUE;

  g_mutex_unlock(&tx->audio_mutex);
  return 0;
}

void audio_close_input(TRANSMITTER *tx) {
  t_print("%s TX:%s\n", __func__, tx->audio_name);
  tx->audio_running = FALSE;

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
    int newpt = (tx->audio_buffer_outpt + 1) & MICRINGMASK;
    sample = tx->audio_buffer[tx->audio_buffer_outpt];
    MEMORY_BARRIER;
    tx->audio_buffer_outpt = newpt;
  }

  g_mutex_unlock(&tx->audio_mutex);
  return sample;
}

void audio_write(RECEIVER *rx, double left, double right) {
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return; }

  g_mutex_lock(&rx->audio_mutex);
  struct pipewire_handle *h = rx->audio_handle;
  if (h == NULL || rx->audio_buffer == NULL) {
    g_mutex_unlock(&rx->audio_mutex);
    return;
  }

  if (rx->cwaudio == 3) {
    // Transition TX -> RX
    if (cw_breakin && !duplex) {
      h->sidetone_ring.inpt = 0;
      h->sidetone_ring.outpt = 0;

      rx->audio_buffer_outpt = rx->audio_buffer_inpt;

      int inpt = rx->audio_buffer_inpt;
      for (int i = 0; i < AUDIO_LAT_TARGET_FRAMES; i++) {
        for (int c = 0; c < rx->local_audio_channels; c++) {
          rx->audio_buffer[inpt * rx->local_audio_channels + c] = 0.0;
        }
        inpt = (inpt + 1) & RING_BUFFER_MASK;
      }
      rx->audio_buffer_inpt = inpt;
    }

    rx->cwaudio = 0;
  }

  if (rx->cwaudio == 5) {
    int inpt = rx->audio_buffer_inpt;
    for (int i = 0; i < AUDIO_LAT_TARGET_FRAMES; i++) {
      for (int c = 0; c < rx->local_audio_channels; c++) {
        rx->audio_buffer[inpt * rx->local_audio_channels + c] = 0.0;
      }
      inpt = (inpt + 1) & RING_BUFFER_MASK;
    }
    rx->audio_buffer_inpt = inpt;
    rx->cwaudio = 0;
  }

  int inpt = rx->audio_buffer_inpt;
  int outpt = rx->audio_buffer_outpt;
  int next_inpt = (inpt + 1) & RING_BUFFER_MASK;

  if (next_inpt != outpt) {
    if (rx->local_audio_channels == 1) {
      rx->audio_buffer[inpt] = 0.5 * (left + right);
    } else {
      rx->audio_buffer[inpt * 2] = left;
      rx->audio_buffer[inpt * 2 + 1] = right;
    }
    MEMORY_BARRIER;
    rx->audio_buffer_inpt = next_inpt;
  }

  g_mutex_unlock(&rx->audio_mutex);
}

void tx_audio_write(RECEIVER *rx, double sample) {
  g_mutex_lock(&rx->audio_mutex);
  struct pipewire_handle *h = rx->audio_handle;
  if (h == NULL || rx->audio_buffer == NULL) {
    g_mutex_unlock(&rx->audio_mutex);
    return;
  }

  if (rx->cwaudio == 0 || rx->cwaudio == 5) {
    // Transition RX -> TX
    if (cw_breakin && !duplex) {
      h->sidetone_ring.inpt = 0;
      h->sidetone_ring.outpt = 0;
    }

    rx->cwaudio = 3;
  }

  // Write sample directly to sidetone_ring
  int inpt = h->sidetone_ring.inpt;
  int outpt = h->sidetone_ring.outpt;
  int next_inpt = (inpt + 1) & RING_BUFFER_MASK;

  if (next_inpt != outpt) {
    for (int c = 0; c < h->channels; c++) {
      h->sidetone_ring.buffer[inpt * h->channels + c] = sample;
    }
    MEMORY_BARRIER;
    h->sidetone_ring.inpt = next_inpt;
  }

  g_mutex_unlock(&rx->audio_mutex);
}
