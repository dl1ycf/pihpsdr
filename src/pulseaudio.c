/* Copyright (C)
* 2019 - John Melton, G0ORX/N6LYT
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

#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <pulse/simple.h>

#include "audio.h"
#include "client_server.h"
#include "message.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "vfo.h"

//
// Latency management:
// Note latency is higher than for ALSA so no special CW optimisation here.
//
// AUDIO_LAT_HIGH         If this latency is exceeded, audio output is stopped
// AUDIO_LAT_LOW          If latency is below this, audio output is resumed
//

#define AUDIO_LAT_HIGH    500000
#define AUDIO_LAT_LOW     250000

static const int out_buffer_size = 256;
static const int mic_buffer_size = 256;

int n_input_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
int n_output_devices;
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

//
// Ring buffer for local audio samples
// NOTE: need large buffer for some "loopback" devices which produce
//       samples in large chunks if fed from digimode programs.
//
#define MICRINGLEN 6000
static float  *mic_ring_buffer = NULL;
static int     mic_ring_read_pt = 0;
static int     mic_ring_write_pt = 0;

static pa_glib_mainloop *main_loop;
static pa_mainloop_api *main_loop_api;
static pa_operation *op;
static pa_context *pa_ctx;
static pa_simple* audio_stream;
static int local_audio_buffer_offset;
static float *local_audio_buffer = NULL;
static GThread *mic_read_thread_id = 0;
static gboolean running;

GMutex audio_mutex;

static void source_list_cb(pa_context *context, const pa_source_info *s, int eol, void *data) {
  if (eol > 0) {
    for (int i = 0; i < n_input_devices; i++) {
      t_print("Input: %d: %s (%s)\n", input_devices[i].index, input_devices[i].name, input_devices[i].description);
    }

    g_mutex_unlock(&audio_mutex);
  } else if (n_input_devices < MAX_AUDIO_DEVICES) {
    input_devices[n_input_devices].name = g_strdup(s->name);
    input_devices[n_input_devices].description = g_strdup(s->description);
    input_devices[n_input_devices].index = s->index;
    n_input_devices++;
  }
}

static void sink_list_cb(pa_context *context, const pa_sink_info *s, int eol, void *data) {
  if (eol > 0) {
    for (int i = 0; i < n_output_devices; i++) {
      t_print("Output: %d: %s (%s)\n", output_devices[i].index, output_devices[i].name, output_devices[i].description);
    }

    op = pa_context_get_source_info_list(pa_ctx, source_list_cb, NULL);
  } else if (n_output_devices < MAX_AUDIO_DEVICES) {
    output_devices[n_output_devices].name = g_strdup(s->name);
    output_devices[n_output_devices].description = g_strdup(s->description);
    output_devices[n_output_devices].index = s->index;
    n_output_devices++;
  }
}

static void state_cb(pa_context *c, void *userdata) {
  pa_context_state_t state;
  state = pa_context_get_state(c);
  t_print("%s: %d\n", __FUNCTION__, state);

  switch  (state) {
  // There are just here for reference
  case PA_CONTEXT_UNCONNECTED:
    t_print("%s: PA_CONTEXT_UNCONNECTED\n", __FUNCTION__);
    break;

  case PA_CONTEXT_CONNECTING:
    t_print("%s: PA_CONTEXT_CONNECTING\n", __FUNCTION__);
    break;

  case PA_CONTEXT_AUTHORIZING:
    t_print("%s: PA_CONTEXT_AUTHORIZING\n", __FUNCTION__);
    break;

  case PA_CONTEXT_SETTING_NAME:
    t_print("%s: PA_CONTEXT_SETTING_NAME\n", __FUNCTION__);
    break;

  case PA_CONTEXT_FAILED:
    t_print("%s: PA_CONTEXT_FAILED\n", __FUNCTION__);
    break;

  case PA_CONTEXT_TERMINATED:
    t_print("%s: PA_CONTEXT_TERMINATED\n", __FUNCTION__);
    break;

  case PA_CONTEXT_READY:
    t_print("%s: PA_CONTEXT_READY\n", __FUNCTION__);
    // get a list of the output devices
    n_input_devices = 0;
    n_output_devices = 0;
    op = pa_context_get_sink_info_list(pa_ctx, sink_list_cb, NULL);
    break;

  default:
    t_print("%s: unknown state %d\n", __FUNCTION__, state);
    break;
  }
}

void audio_get_cards() {
  g_mutex_init(&audio_mutex);
  g_mutex_lock(&audio_mutex);
  main_loop = pa_glib_mainloop_new(NULL);
  main_loop_api = pa_glib_mainloop_get_api(main_loop);
  pa_ctx = pa_context_new(main_loop_api, "piHPSDR");
  pa_context_connect(pa_ctx, NULL, 0, NULL);
  pa_context_set_state_callback(pa_ctx, state_cb, NULL);
}

int audio_open_output(RECEIVER *rx) {
  int result = 0;
  pa_sample_spec sample_spec;
  int err;
  t_print("%s: RX%d:%s\n", __FUNCTION__, rx->id+1, rx->audio_name);
  g_mutex_lock(&rx->local_audio_mutex);
  sample_spec.rate = 48000;
  sample_spec.channels = 2;
  sample_spec.format = PA_SAMPLE_FLOAT32NE;
  char stream_id[16];
  snprintf(stream_id, sizeof(stream_id), "RX-%d", rx->id);
  pa_buffer_attr attr;
  attr.maxlength = (uint32_t) -1;
  attr.tlength   = (uint32_t) -1;
  attr.prebuf    = (uint32_t) -1;
  attr.minreq    = (uint32_t) -1;
  attr.fragsize  = (uint32_t) -1;
  rx->playstream = pa_simple_new(NULL, // Use the default server.
                                 "piHPSDR",          // Our application's name.
                                 PA_STREAM_PLAYBACK,
                                 rx->audio_name,
                                 stream_id,          // Description of our stream.
                                 &sample_spec,       // Our sample format.
                                 NULL,               // Use default channel map
                                 &attr,              // Use default attributes
                                 &err                // error code if returns NULL
                                );

  if (rx->playstream != NULL) {
    rx->cwaudio = 0;
    rx->cwcount = 0;
    rx->local_audio_buffer_offset = 0;
    rx->local_audio_buffer = g_new0(float, 2 * out_buffer_size);
  } else {
    result = -1;
    t_print("%s: ERROR pa_simple_new: %s\n", __FUNCTION__, pa_strerror(err));
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return result;
}

static void *mic_read_thread(gpointer arg) {
  int err;
  t_print("%s: running=%d\n", __FUNCTION__, running);

  while (running) {
    //
    // It is guaranteed that local_audio_buffer, mic_ring_buffer, and audio_stream
    // will not be destroyed until this thread has terminated (and waited for via thread joining)
    //
    int rc = pa_simple_read(audio_stream,
                            local_audio_buffer,
                            mic_buffer_size * sizeof(float),
                            &err);

    if (rc < 0) {
      running = FALSE;
      t_print("%s: ERROR pa_simple_read: %s\n", __FUNCTION__, rc, pa_strerror(err));
    } else {
      for (int i = 0; i < mic_buffer_size; i++) {
        //
        // If we are a client, simply collect and transfer data
        // to the server without any buffering
        //
        if (radio_is_remote) {
          short s = local_audio_buffer[i] * 32767.0;
          server_tx_audio(s);
          continue;
        }

        //
        // put sample into ring buffer
        //
        int newpt = mic_ring_write_pt + 1;

        if (newpt == MICRINGLEN) { newpt = 0; }

        if (newpt != mic_ring_read_pt) {
          // buffer space available, do the write
          mic_ring_buffer[mic_ring_write_pt] = local_audio_buffer[i];
          // atomic update of mic_ring_write_pt
          mic_ring_write_pt = newpt;
        }
      }
    }
  }

  t_print("%s: exit\n", __FUNCTION__);
  return NULL;
}

int audio_open_input() {
  pa_sample_spec sample_spec;
  int err;
  int result = 0;

  if (!can_transmit) {
    return -1;
  }

  t_print("%s: TX:%s\n", __FUNCTION__, transmitter->audio_name);
  g_mutex_lock(&audio_mutex);
  pa_buffer_attr attr;
  attr.maxlength = (uint32_t) -1;
  attr.tlength = (uint32_t) -1;
  attr.prebuf = (uint32_t) -1;
  attr.minreq = (uint32_t) -1;
  attr.fragsize = 512;
  sample_spec.rate = 48000;
  sample_spec.channels = 1;
  sample_spec.format = PA_SAMPLE_FLOAT32NE;
  audio_stream = pa_simple_new(NULL,      // Use the default server.
                               "piHPSDR",                   // Our application's name.
                               PA_STREAM_RECORD,
                               transmitter->audio_name,
                               "TX",                        // Description of our stream.
                               &sample_spec,                // Our sample format.
                               NULL,                        // Use default channel map
                               &attr,                       // Use default buffering attributes but set fragsize
                               &err                         // Ignore error code.
                              );

  if (audio_stream != NULL) {
    local_audio_buffer_offset = 0;
    local_audio_buffer = g_new0(float, mic_buffer_size);
    t_print("%s: allocating ring buffer\n", __FUNCTION__);
    mic_ring_buffer = (float *) g_new(float, MICRINGLEN);
    mic_ring_read_pt = mic_ring_write_pt = 0;

    if (mic_ring_buffer == NULL) {
      g_mutex_unlock(&audio_mutex);
      audio_close_input();
      return -1;
    }

    running = TRUE;
    t_print("%s: PULSEAUDIO mic_read_thread\n", __FUNCTION__);
    mic_read_thread_id = g_thread_new("mic_thread", mic_read_thread, NULL);

    if (!mic_read_thread_id ) {
      t_print("%s: g_thread_new failed on mic_read_thread\n", __FUNCTION__);
      g_free(local_audio_buffer);
      local_audio_buffer = NULL;
      running = FALSE;
      result = -1;
    }
  } else {
    t_print("%s: ERROR pa_simple_new: %s\n", __FUNCTION__, pa_strerror(err));
    result = -1;
  }

  g_mutex_unlock(&audio_mutex);
  return result;
}

void audio_close_output(RECEIVER *rx) {
  t_print("%s: RX%d:%s\n", __FUNCTION__, rx->id+1, rx->audio_name);
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playstream != NULL) {
    pa_simple_free(rx->playstream);
    rx->playstream = NULL;
  }

  if (rx->local_audio_buffer != NULL) {
    g_free(rx->local_audio_buffer);
    rx->local_audio_buffer = NULL;
  }

  g_mutex_unlock(&rx->local_audio_mutex);
}

void audio_close_input() {
  running = FALSE;
  t_print("%s: TX:%s\n", __FUNCTION__, transmitter->audio_name);
  g_mutex_lock(&audio_mutex);

  if (mic_read_thread_id != NULL) {
    //
    // wait for the mic read thread to terminate,
    // then destroy the stream and the buffers
    // This way, the buffers cannot "vanish" in the mic read thread
    //
    g_thread_join(mic_read_thread_id);
    mic_read_thread_id = NULL;
  }

  if (audio_stream != NULL) {
    pa_simple_free(audio_stream);
    audio_stream = NULL;
  }

  if (local_audio_buffer != NULL) {
    g_free(local_audio_buffer);
    local_audio_buffer = NULL;
  }

  if (mic_ring_buffer != NULL) {
    g_free(mic_ring_buffer);
  }

  g_mutex_unlock(&audio_mutex);
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample() {
  float sample;
  g_mutex_lock(&audio_mutex);

  if ((mic_ring_buffer == NULL) || (mic_ring_read_pt == mic_ring_write_pt)) {
    // no buffer, or nothing in buffer: insert silence
    sample = 0.0;
  } else {
    int newpt = mic_ring_read_pt + 1;

    if (newpt == MICRINGLEN) { newpt = 0; }

    sample = mic_ring_buffer[mic_ring_read_pt];
    // atomic update of read pointer
    mic_ring_read_pt = newpt;
  }

  g_mutex_unlock(&audio_mutex);
  return sample;
}

int cw_audio_write(RECEIVER *rx, float sample) {
  int result = 0;
  int err;
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playstream != NULL && rx->local_audio_buffer != NULL) {
    //
    // Since this is mutex-protected, we know that both rx->playstream
    // and rx->local_audio_buffer will not be destroyed until we
    // are finished here.
    //
    rx->local_audio_buffer[rx->local_audio_buffer_offset * 2] = sample;
    rx->local_audio_buffer[(rx->local_audio_buffer_offset * 2) + 1] = sample;
    rx->local_audio_buffer_offset++;

    if (rx->local_audio_buffer_offset >= out_buffer_size) {
      pa_usec_t latency = pa_simple_get_latency(rx->playstream, &err);

      if (latency > AUDIO_LAT_HIGH && rx->cwcount == 0) {
        //
        // If the radio is running a a slightly too high clock rate, or if
        // the audio hardware clocks slightly below 48 kHz, then the PA audio
        // buffer will fill up. suppress audio data until the latency is below
        // AUDIO_LAT_LOW, or until a pre-calculated maximum number of output
        // buffers has been suppressed.
        //
        rx->cwcount = (AUDIO_LAT_HIGH - AUDIO_LAT_LOW) / (20 * out_buffer_size);
        t_print("%s: suppressing audio block\n", __FUNCTION__);
      }

      if (rx->cwcount > 0) {
        rx->cwcount--;
      }

      if (latency > AUDIO_LAT_HIGH && rx->cwcount == 0) {
        int rc = pa_simple_write(rx->playstream,
                                 rx->local_audio_buffer,
                                 out_buffer_size * sizeof(float) * 2,
                                 &err);

        if (rc < 0) {
          t_print("%s: ERROR pa_simple_write: %s\n", __FUNCTION__, pa_strerror(err));
        }
      }

      rx->local_audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return result;
}

int audio_write(RECEIVER *rx, float left_sample, float right_sample) {
  int result = 0;
  int err;
  int txmode = vfo_get_tx_mode();

  //
  // If a CW/TUNE side tone may occur, quickly return
  //
  if (rx == active_receiver && radio_is_transmitting()) {
    if (txmode == modeCWU || txmode == modeCWL) { return 0; }

    if (can_transmit && transmitter->tune && transmitter->swrtune) { return 0; }
  }

  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playstream != NULL && rx->local_audio_buffer != NULL) {
    //
    // Since this is mutex-protected, we know that both rx->playstream
    // and rx->local_audio_buffer will not be destroyes until we
    // are finished here.
    //
    rx->local_audio_buffer[rx->local_audio_buffer_offset * 2] = left_sample;
    rx->local_audio_buffer[(rx->local_audio_buffer_offset * 2) + 1] = right_sample;
    rx->local_audio_buffer_offset++;

    if (rx->local_audio_buffer_offset >= out_buffer_size) {
      pa_usec_t latency = pa_simple_get_latency(rx->playstream, &err);

      if (latency > AUDIO_LAT_HIGH && rx->cwcount == 0) {
        //
        // If the radio is running a a slightly too high clock rate, or if
        // the audio hardware clocks slightly below 48 kHz, then the PA audio
        // buffer will fill up. suppress audio data until the latency is below
        // AUDIO_LAT_LOW, or until a pre-calculated maximum number of output
        // buffers has been suppressed.
        //
        rx->cwcount = (AUDIO_LAT_HIGH - AUDIO_LAT_LOW) / (20 * out_buffer_size);
        t_print("%s: suppressing audio block\n", __FUNCTION__);
      }

      if (rx->cwcount > 0) {
        rx->cwcount--;
      }

      if (rx->cwcount == 0 || latency < AUDIO_LAT_LOW) {
        int rc = pa_simple_write(rx->playstream,
                                 rx->local_audio_buffer,
                                 out_buffer_size * sizeof(float) * 2,
                                 &err);

        if (rc < 0) {
          t_print("%s: ERROR pa_simple_write: %s\n", __FUNCTION__, pa_strerror(err));
        }
      }

      rx->local_audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return result;
}
