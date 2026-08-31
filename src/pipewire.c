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
#define CW_LAT_LOW         320   // sidetone low water mark
#define CW_LAT_TARGET      384   // sidetone target latency
#define CW_LAT_HIGH        448   // sidetone high water mark

//
// The Pipewire "Quantum" (number of samples to be transferred in one callback)
// is now set to 256 both for capture and playback.
// There have been reports that a value of 128 (previously used for playback)
// leads to frequent audio drop-outs with HDMI audio on some LINUX boxes.
//

#define PIPEWIRE_QUANTUM_CAPTURE  "256/48000"
#define PIPEWIRE_QUANTUM_PLAYBACK "256/48000"

int n_input_devices;
int n_output_devices;

AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

struct audio_data_ {
  double *audio_buffer;                    // ring buffer for main audio
  double *st_buffer;                       // ring buffer for side tone (RX audio only)
  volatile atomic_int audio_buffer_inpt;   // pointer for audio ring buffer
  volatile atomic_int audio_buffer_outpt;
  volatile atomic_int st_buffer_inpt;      // pointer for side tone ring buffer
  volatile atomic_int st_buffer_outpt;
  double audiodamp;                        // attenuation for blending
  int audio_flag;                          // flag to detect RX/TX transisions in audio in
  int cwaudio;                             // state flag used in RX/TX transitions for audio out
  int cwcount;                             // counter for "silence" in side tone
  struct pw_thread_loop *loop;             // PipeWire data
  struct pw_context *context;
  struct pw_core *core;
  struct pw_stream *stream;
};

typedef struct audio_data_ audio_data;

static void on_discovery_timeout(void *data, uint64_t expirations) {
  struct pw_main_loop *loop = data;
  pw_main_loop_quit(loop);
}

static void registry_event_global(void *data, uint32_t id, uint32_t permissions,
                                  const char *type, uint32_t version,
                                  const struct spa_dict *props) {
  if (props == NULL) { return; }
  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char *desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    //
    // If the "description" has not been given (e.g. when creating a null-sink
    // device intended as a virtual audio cable) use the name instead
    //
    if (!desc) { desc = name; }
    if (media_class && name) {
      if (strcmp(media_class, "Audio/Sink") == 0) {
        if (n_output_devices < MAX_AUDIO_DEVICES) {
          output_devices[n_output_devices].name = g_strdup(name);
          output_devices[n_output_devices].description = g_strdup(desc);
          output_devices[n_output_devices].channels = 2; // unused
          output_devices[n_output_devices].is_monitor = 0;
          n_output_devices++;
        }
        //
        // Each output device can also be used for input through its monitor
        // Pre-pend the description of the output device with "Monitor of"
        //
        if (n_input_devices < MAX_AUDIO_DEVICES) {
          input_devices[n_input_devices].name = g_strdup(name);
          size_t desclen = strlen(desc) + 16;
          char *mondesc = g_new(char, desclen);
          snprintf(mondesc, desclen, "Monitor of %s", desc);
          input_devices[n_input_devices].description = mondesc;
          input_devices[n_input_devices].channels = 1; // unused
          input_devices[n_input_devices].is_monitor = 1;
          n_input_devices++;
        }
      } else if (strcmp(media_class, "Audio/Source") == 0) {
        if (n_input_devices < MAX_AUDIO_DEVICES) {
          input_devices[n_input_devices].name = g_strdup(name);
          input_devices[n_input_devices].description = g_strdup(desc);
          input_devices[n_input_devices].channels = 1; // unused
          output_devices[n_output_devices].is_monitor = 0;
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
  if (loop == NULL) { return; }
  struct pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), NULL, 0);
  if (context == NULL) {
    pw_main_loop_destroy(loop);
    return;
  }
  struct pw_core *core = pw_context_connect(context, NULL, 0);
  if (core == NULL) {
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
  RECEIVER *rx = (RECEIVER *) data;
  if (rx == NULL) { return; }
  struct pw_buffer *b;
  struct spa_buffer *buf;
  float *samples;
  unsigned int n_frames;
  audio_data *ad = rx->audio_handle;
  if ((b = pw_stream_dequeue_buffer(ad->stream)) == NULL) {
    return;
  }
  buf = b->buffer;
  samples = buf->datas[0].data;
  if (samples == NULL) { return; }
  n_frames = buf->datas[0].maxsize / (2 * sizeof(float));
  if (b->requested && b->requested < n_frames) {
    n_frames = b->requested;
  }
  //
  // This is paranoia. We do not know how long the buffer dequeue took.
  //
  ad = rx->audio_handle;
  if (ad == NULL) {
    //
    // audio_close_output() is happening.
    // As long as callbacks happen, return silence
    //
    memset(samples, 0,  2 *n_frames * sizeof(float));
  } else {
    //
    // The existence of buffers is guaranteed for 50 msec
    //
    for (unsigned i = 0; i < n_frames; i++) {
      double rx_left = 0.0;
      double rx_right = 0.0;
      double st_sample = 0.0;
      int oldpt;
      oldpt = ad->audio_buffer_outpt;
      if (oldpt != ad->audio_buffer_inpt) {
        rx_left = ad->audio_buffer[oldpt * 2];
        rx_right = ad->audio_buffer[oldpt * 2 + 1];
        if (ad->cwaudio == 3) {
          rx_left *= ad->audiodamp;
          rx_right *= ad->audiodamp;
          ad->audiodamp *= 0.999;
        }
        MEMORY_BARRIER;
        ad->audio_buffer_outpt = (oldpt + 1) & RING_BUFFER_MASK;
      }
      oldpt = ad->st_buffer_outpt;
      if (oldpt != ad->st_buffer_inpt) {
        st_sample = ad->st_buffer[oldpt];
        MEMORY_BARRIER;
        ad->st_buffer_outpt = (oldpt + 1) & ST_BUFFER_MASK;
      }
      samples[i * 2] = (float)(rx_left + st_sample);
      samples[i * 2 + 1] = (float)(rx_right + st_sample);
    }
  }
  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->size = n_frames * 2 * sizeof(float);
  buf->datas[0].chunk->stride = 2 * sizeof(float);
  pw_stream_queue_buffer(ad->stream, b);
}

static void pw_in_cb(void *data) {
  TRANSMITTER *tx = (TRANSMITTER *) data;
  if (tx == NULL) { return; }
  struct pw_buffer *b;
  struct spa_buffer *buf;
  const float *samples;
  audio_data *ad = (audio_data *) tx->audio_handle;
  if (ad == NULL) { return; }
  if ((b = pw_stream_dequeue_buffer(ad->stream)) == NULL) {
    return;
  }
  buf = b->buffer;
  samples = buf->datas[0].data;
  ad = tx->audio_handle; // query again, if queue_buffer took "long"
  if (samples == NULL || ad == NULL ) {
    pw_stream_queue_buffer(ad->stream, b);
    return;
  }
  //
  // If we come here, the existence of the buffers is guaranteed for
  // the next 50 ms, more then enough to do the copy
  //
  if (!radio_is_transmitting() && ad->audio_flag) {
    //
    // Normally there is a slight mis-match between the 48kHz sample
    // rate of the audio input device and the 48kHz rate of the
    // HPSDR device. Thus, the mic buffer tends to either slowly
    // drain or slowly become full (which leads to large TX delays).
    //
    // The TX/RX transition seems to be the best moment to "reset"
    // the mic input buffer, and fill it with a little bit (20 msec)
    // of silence and the current batch of mic samples. During normal
    // RX operation, one cannot fiddle around with the mic samples since
    // VOX might be active.
    //
    // audio_flag ensures that we only come here *once* after a TXRX transition.
    //
    ad->audio_flag = 0;
    int avail = (ad->audio_buffer_inpt - ad->audio_buffer_outpt) & MIC_BUFFER_MASK;
    if (avail >= 960) {
      //
      // More than 960 sample in mic input buffer --> reduce to 960
      //
      ad->audio_buffer_inpt  = (ad->audio_buffer_outpt + 960) & MIC_BUFFER_MASK;
    } else {
      //
      // less than 960 samples in buffer --> add some silence
      //
      for (int i = avail; i < 960; i++) {
        ad->audio_buffer[ad->audio_buffer_inpt] = 0.0;
        ad->audio_buffer_inpt = (ad->audio_buffer_inpt + 1) & MIC_BUFFER_MASK;
      }
    }
  } else {
    //
    // If transmitting: reset audio_flag
    //
    ad->audio_flag = 1;
  }
  unsigned int n_frames = buf->datas[0].chunk->size / sizeof(float);
  int inpt = ad->audio_buffer_inpt;
  for (unsigned int i = 0; i < n_frames; i++) {
    int newpt = (inpt + 1) & MIC_BUFFER_MASK;
    if (newpt != ad->audio_buffer_outpt) {
      ad->audio_buffer[inpt] = (double) samples[i];
      inpt = newpt;
    }
  }
  MEMORY_BARRIER;
  ad->audio_buffer_inpt = inpt;
  pw_stream_queue_buffer(ad->stream, b);
}

int audio_open_output(RECEIVER *rx) {
  t_print("%s RX%d:%s\n", __func__, rx->id + 1, rx->audio_name);
  int err = 1;
  for (int i = 0; i < n_output_devices; i++) {
    if (!strcmp(rx->audio_name, output_devices[i].name)) {
      err = 0;
      break;
    }
  }
  if (err) {
    t_print("%s: not registered: %s\n", __func__, rx->audio_name);
    return -1;
  }
  if (rx->audio_handle != NULL) {
    // we should not come here
    rx->audio_handle = NULL;
    usleep(50000);
  }
  audio_data *ad = g_new(audio_data, 1);
  if (ad == NULL) { return -1; }
  ad->audio_buffer = g_new(double, 2 * RING_BUFFER_SIZE);
  ad->st_buffer = g_new(double, ST_BUFFER_SIZE);
  if (ad->audio_buffer == NULL || ad->st_buffer == NULL) {
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  ad->loop = pw_thread_loop_new("pihpsdr-playback", NULL);
  if (ad->loop == NULL) {
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  ad->context = pw_context_new(pw_thread_loop_get_loop(ad->loop), NULL, 0);
  if (ad->context == NULL) {
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  if (pw_thread_loop_start(ad->loop) < 0) {
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  pw_thread_loop_lock(ad->loop);
  ad->core = pw_context_connect(ad->context, NULL, 0);
  if (ad->core == NULL) {
    pw_thread_loop_unlock(ad->loop);
    pw_thread_loop_stop(ad->loop);
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
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
                                  PW_KEY_NODE_LATENCY, PIPEWIRE_QUANTUM_PLAYBACK,
                                  NULL
                                );
  static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = pw_out_cb,
  };
  ad->stream = pw_stream_new_simple(pw_thread_loop_get_loop(ad->loop),
                                   "pihpsdr-playback",
                                   props,
                                   &stream_events,
                                   rx);
  if (ad->stream == NULL) {
    pw_core_disconnect(ad->core);
    pw_thread_loop_unlock(ad->loop);
    pw_thread_loop_stop(ad->loop);
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
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
  int res = pw_stream_connect(ad->stream,
                              PW_DIRECTION_OUTPUT,
                              PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT |
                              PW_STREAM_FLAG_MAP_BUFFERS |
                              PW_STREAM_FLAG_RT_PROCESS,
                              params, 1);
  if (res < 0) {
    pw_stream_destroy(ad->stream);
    pw_core_disconnect(ad->core);
    pw_thread_loop_unlock(ad->loop);
    pw_thread_loop_stop(ad->loop);
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  pw_thread_loop_unlock(ad->loop);
  ad->audio_buffer_inpt = 0;
  ad->audio_buffer_outpt = 0;
  ad->st_buffer_inpt = 0;
  ad->st_buffer_outpt = 0;
  ad->cwaudio = 5;
  ad->cwcount = 0;
  //
  // Finished, set audio_handle
  rx->audio_handle = ad;
  return 0;
}

void audio_close_output(RECEIVER *rx) {
  if (rx == NULL) { return; }
  t_print("%s RX%d:%s\n", __func__, rx->id + 1, rx->audio_name);
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) { return; }
  rx->audio_handle = NULL;
  pw_thread_loop_stop(ad->loop);
  if (ad->stream) { pw_stream_destroy(ad->stream); }
  pw_core_disconnect(ad->core);
  pw_context_destroy(ad->context);
  pw_thread_loop_destroy(ad->loop);
  usleep(50000);
  g_free(ad->audio_buffer);
  g_free(ad->st_buffer);
  g_free(ad);
}

int audio_open_input(TRANSMITTER *tx) {
  if (tx == NULL) { return -1; }
  t_print("%s TX:%s\n", __func__, tx->audio_name);
  int err = 1;
  int monitor = 0;
  for (int i = 0; i < n_input_devices; i++) {
    if (!strcmp(tx->audio_name, input_devices[i].name)) {
      monitor = input_devices[i].is_monitor;
      err = 0;
      break;
    }
  }
  if (err) {
    t_print("%s: not registered: %s\n", __func__, tx->audio_name);
    return -1;
  }
  if (tx->audio_handle) {
    // should not occur, treat it as a bogus pointer
    tx->audio_handle = NULL;
    usleep(50000);
  }
  audio_data *ad = g_new(audio_data, 1);
  if (ad == NULL) { return -1; }
  ad->audio_buffer = g_new(double, MIC_BUFFER_SIZE);
  if (ad->audio_buffer == NULL) {
    g_free(ad);
    return -1;
  }
  ad->loop = pw_thread_loop_new("pihpsdr-capture", NULL);
  if (ad->loop == NULL) {
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  ad->context = pw_context_new(pw_thread_loop_get_loop(ad->loop), NULL, 0);
  if (ad->context == NULL) {
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  if (pw_thread_loop_start(ad->loop) < 0) {
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  pw_thread_loop_lock(ad->loop);
  ad->core = pw_context_connect(ad->context, NULL, 0);
  if (ad->core == NULL) {
    pw_thread_loop_unlock(ad->loop);
    pw_thread_loop_stop(ad->loop);
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  struct pw_properties *props = pw_properties_new(
                                  PW_KEY_MEDIA_TYPE, "Audio",
                                  PW_KEY_MEDIA_CATEGORY, "Capture",
                                  PW_KEY_MEDIA_ROLE, "DSP",
                                  PW_KEY_NODE_NAME, "piHPSDR capture",
                                  PW_KEY_TARGET_OBJECT, tx->audio_name,
                                  PW_KEY_NODE_LATENCY, PIPEWIRE_QUANTUM_CAPTURE,
                                  NULL
                                );

  if (monitor) {
    //
    // This is an audio output device, and we want to use its associated monitor
    //
    pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
  }

  static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = pw_in_cb,
  };
  ad->stream = pw_stream_new_simple(pw_thread_loop_get_loop(ad->loop),
                                   "pihpsdr-capture-stream",
                                   props,
                                   &stream_events,
                                   tx);
  if (ad->stream == NULL) {
    pw_core_disconnect(ad->core);
    pw_thread_loop_unlock(ad->loop);
    pw_thread_loop_stop(ad->loop);
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad);
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
  int res = pw_stream_connect(ad->stream,
                              PW_DIRECTION_INPUT,
                              PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT |
                              PW_STREAM_FLAG_MAP_BUFFERS |
                              PW_STREAM_FLAG_RT_PROCESS,
                              params, 1);
  if (res < 0) {
    pw_stream_destroy(ad->stream);
    pw_core_disconnect(ad->core);
    pw_thread_loop_unlock(ad->loop);
    pw_thread_loop_stop(ad->loop);
    pw_context_destroy(ad->context);
    pw_thread_loop_destroy(ad->loop);
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  pw_thread_loop_unlock(ad->loop);
  ad->audio_buffer_inpt = 0;
  ad->audio_buffer_outpt = 0;
  ad->audio_flag = 1;
  tx->audio_handle = ad;
  return 0;
}

void audio_close_input(TRANSMITTER *tx) {
  if (tx == NULL) { return; }
  t_print("%s TX:%s\n", __func__, tx->audio_name);
  audio_data *ad = tx->audio_handle;
  if (ad == NULL) { return; }
  tx->audio_handle = NULL;
  pw_thread_loop_stop(ad->loop);
  pw_stream_destroy(ad->stream);
  pw_core_disconnect(ad->core);
  pw_context_destroy(ad->context);
  pw_thread_loop_destroy(ad->loop);
  usleep(50000);
  g_free(ad->audio_buffer);
  g_free(ad);
}

double audio_get_next_mic_sample(TRANSMITTER *tx) {
  if (tx == NULL) { return 0.0; }
  audio_data *ad = tx->audio_handle;
  if (ad  == NULL) { return 0.0; }
  if (ad->audio_buffer_outpt == ad->audio_buffer_inpt) { return 0.0; }
  int newpt = (ad->audio_buffer_outpt + 1) & MIC_BUFFER_MASK;
  double  sample = ad->audio_buffer[ad->audio_buffer_outpt];
  MEMORY_BARRIER;
  ad->audio_buffer_outpt = newpt;
  return sample;
}

void audio_write(RECEIVER *rx, double left, double right) {
  if (rx == NULL) { return; }
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return; }
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) { return; }
  double *buffer = ad->audio_buffer;
  ad->cwaudio = 0;
  int avail = (ad->audio_buffer_inpt - ad->audio_buffer_outpt) & RING_BUFFER_MASK;
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
    int inpt = ad->audio_buffer_inpt;
    for (int i = 0; i < AUDIO_LAT_TARGET - avail; i++) {
      buffer[2 * inpt] = 0.0;
      buffer[2 * inpt + 1] = 0.0;
      inpt = (inpt + 1) & RING_BUFFER_MASK;
    }
    MEMORY_BARRIER;
    ad->audio_buffer_inpt = inpt;
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
    ad->audio_buffer_inpt = (ad->audio_buffer_inpt - avail + AUDIO_LAT_TARGET) & RING_BUFFER_MASK;
    // Now buffer filling ids exactly at AUDIO_LAT_TARGET
  }
  int newpt = (ad->audio_buffer_inpt + 1) & RING_BUFFER_MASK;
  if (newpt  != ad->audio_buffer_outpt) {
    ad->audio_buffer[ad->audio_buffer_inpt * 2] = left;
    ad->audio_buffer[ad->audio_buffer_inpt * 2 + 1] = right;
    MEMORY_BARRIER;
    ad->audio_buffer_inpt = newpt;
  }
}

void tx_audio_write(RECEIVER *rx, double sample) {
  if (rx == NULL) { return; }
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) { return; }
  int inpt = ad->st_buffer_inpt;
  int newpt;
  int avail = (inpt - ad->st_buffer_outpt) & ST_BUFFER_MASK;
  int adjust = 0;
  if (ad->cwaudio != 3) {
    // Transition RX -> TX
    if (avail < CW_LAT_TARGET) {
      for (int i = 0; i < CW_LAT_TARGET - avail; i++) {
        ad->st_buffer[inpt] = 0.0;
        inpt = (inpt + 1) & ST_BUFFER_MASK;
      }
      MEMORY_BARRIER;
      ad->st_buffer_inpt = inpt;
    }
    ad->audiodamp = 1.0;
    ad->cwaudio = 3;
    ad->cwcount = 0;
    avail = CW_LAT_TARGET;
  }
  if (sample != 0.0) { ad->cwcount = 0; }
  if (++ad->cwcount > 16) {
    ad->cwcount = 0;
    //
    // We arrive here if we have seen 16 zero samples in a row.
    //
    if (avail > CW_LAT_HIGH) { adjust = 2; } // full: we are above high water mark
    if (avail < CW_LAT_LOW)  { adjust = 1; } // low: we are below low water mark
  }
  switch (adjust) {
  case 0:
    // Write sample directly to sidetone buffer
    newpt = (inpt + 1) & ST_BUFFER_MASK;
    if (newpt != ad->st_buffer_outpt) {
      // buffer space available
      ad->st_buffer[inpt] = sample;
      MEMORY_BARRIER;
      ad->st_buffer_inpt = newpt;
    }
    break;
  case 1:
    // We just saw 16 silence samples and buffer filling is low:
    // insert one extra
    ad->st_buffer[inpt] = 0.0;;
    inpt = (inpt + 1) & ST_BUFFER_MASK;
    ad->st_buffer[inpt] = 0.0;;
    inpt = (inpt + 1) & ST_BUFFER_MASK;
    MEMORY_BARRIER;
    ad->st_buffer_inpt = inpt;
    break;
  default:
    // We just saw 16 silence samples and buffer filling is high:
    // just skip current "silent" sample (do nothing)
    break;
  }
}
