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

#include "atomic.h"
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
// Note latency is higher than for ALSA
//
// AUDIO_LAT_MAX          If this latency is exceeded, RX audio output is stopped
// AUDIO_LAT_TARGET       target RX audio latency
//
// CW_LAT_MAX             If this latency is exceeded, TX audio output is stopped
// CW_LAT_TARGET          target TX audio latency
// CW_LAT_LOW             low  water for keeping at target filling
//

#define AUDIO_LAT_MAX     400000
#define AUDIO_LAT_TARGET  200000
#define CW_LAT_MAX         60000
#define CW_LAT_TARGET      20000
#define CW_LAT_LOW         10000

//
// Loopback devices, when connected to digimode programs, sometimes
// deliver audio in large chungs, so we need a large ring buffer as well
//
#define MICRINGLEN  8192
#define MICRINGMASK 8191

#define out_buffer_size  256
#define inp_buffer_size  256

int n_input_devices;
int n_output_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

// Flag that indicates PA context has been established
static int pa_ready = 0;

struct audio_data_ {
  double *audio_buffer;                    // ring buffer for main audio
  volatile atomic_int audio_buffer_inpt;   // TX: pointer for audio ring buffer
  volatile atomic_int audio_buffer_outpt;
  volatile atomic_int audio_buffer_offset;
  int cwaudio;                             // state flag used in RX/TX transitions for audio out
  int cwcount;                             // counter for "silence" in side tone
  int skipcnt;
  GMutex audio_mutex;
  GThread *audio_thread_id;
  pa_simple *pastream;                     // PulseAudio stream
  pa_usec_t latency;
};

typedef struct audio_data_ audio_data;

static void source_list_cb(pa_context *context, const pa_source_info *s, int eol, void *data) {
  if (eol > 0) { return; }
  if (n_input_devices < MAX_AUDIO_DEVICES) {
    input_devices[n_input_devices].name = g_strdup(s->name);
    input_devices[n_input_devices].description = g_strdup(s->description);
    n_input_devices++;
  }
}

static void sink_list_cb(pa_context *context, const pa_sink_info *s, int eol, void *data) {
  if (eol > 0) { return; }
  if (n_output_devices < MAX_AUDIO_DEVICES) {
    output_devices[n_output_devices].name = g_strdup(s->name);
    output_devices[n_output_devices].description = g_strdup(s->description);
    n_output_devices++;
  }
}

static void state_cb(pa_context *c, void *userdata) {
  pa_context_state_t state;
  state = pa_context_get_state(c);
  switch  (state) {
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED:
    pa_ready = 2;
    break;
  case PA_CONTEXT_READY:
    pa_ready = 1;
    break;
  default:
    break;
  }
}

//
// Enumeration of Devices. This thread is spawned off by audio_get_cards
// (which quickly returns) and the enumeration is done in the background.
// If it fails, piHPDSR may run normally for radios with codecs.
//
static gpointer enumerate_thread(gpointer arg) {
  pa_context *context = (pa_context *) arg;
  pa_operation *op;
  // wait for context change
  while (pa_ready == 0) { usleep(100000); }
  if (pa_ready == 2) { return NULL; }
  // get input devices
  op = pa_context_get_sink_info_list(context, sink_list_cb, NULL);
  while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) { usleep(100000); }
  pa_operation_unref(op);
  op = pa_context_get_source_info_list(context, source_list_cb, NULL);
  while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) { usleep(100000); }
  pa_operation_unref(op);
  for (int i = 0; i < n_input_devices; i++) {
    t_print("PulseAudio Input: %s\n", input_devices[i].description);
  }
  for (int i = 0; i < n_output_devices; i++) {
    t_print("PulseAudio Output: %s\n", output_devices[i].description);
  }
  return NULL;
}

void audio_get_cards() {
  //
  // If the radio has its own codec, we may not need the audio module at all.
  // So do all the enumeration in the background and quickly return from
  // audio_get_cards. We now span a background thread that does this.
  //
  // Since audio_get_cards() is called before the discovery process, there
  // should be enough time for the enumeration thread to complete before
  // the radio is started.
  //
  t_print("%s: PulseAudio\n", __func__);
  n_input_devices = 0;
  n_output_devices = 0;
  pa_glib_mainloop *main_loop = pa_glib_mainloop_new(NULL);
  pa_mainloop_api *main_loop_api = pa_glib_mainloop_get_api(main_loop);
  pa_context *pa_ctx = pa_context_new(main_loop_api, "piHPSDR");
  pa_context_connect(pa_ctx, NULL, 0, NULL);
  pa_context_set_state_callback(pa_ctx, state_cb, NULL);
  g_thread_new("PAenum", enumerate_thread, (gpointer)pa_ctx);
}

int audio_open_output(RECEIVER *rx) {
  if (rx == NULL) { return -1; }
  pa_sample_spec sample_spec;
  int err;
  sample_spec.rate = 48000;
  sample_spec.format = PA_SAMPLE_FLOAT32NE;
  char stream_id[16];
  snprintf(stream_id, sizeof(stream_id), "RX-%d", rx->id);
  pa_buffer_attr attr;
  //
  // My experiments indicate that opening a MONO device while setting
  // the number of channes to 2 is OK with PulseAudio (there is a
  // "mono-fallback" device), so no need to handle this case.
  //
  sample_spec.channels = 2;
  //
  // PulseAudio/Pipewire accept ALSA device names as well. So when changing
  // from ALSA to pulseaudio, a device may be successfully opened although
  // its device name is not contained in our device list. This can lead to
  // inconsistencies. Therefore, return with error if the name is not
  // registered.
  //
  err = 1;
  for (int i = 0; i < n_output_devices; i++) {
    if (!strcmp(rx->audio_name, output_devices[i].name)) {
      err = 0;
      t_print("%s RX%d:%s\n", __func__, rx->id + 1, output_devices[i].description);
      break;
    }
  }
  if (err) {
    t_print("%s: not registered: %s\n", __func__, rx->audio_name);
    return -1;
  }
  if (rx->audio_handle != NULL) {
    // we should never come her
    rx->audio_handle = NULL;
    usleep(50000);
  }
  attr.maxlength = pa_usec_to_bytes(2 * AUDIO_LAT_MAX, &sample_spec);
  attr.tlength   = pa_usec_to_bytes(AUDIO_LAT_TARGET, &sample_spec);
  attr.prebuf    = pa_usec_to_bytes(AUDIO_LAT_TARGET, &sample_spec);
  attr.minreq    = (uint32_t) -1;
  attr.fragsize  = (uint32_t) -1;
  audio_data *ad = g_new(audio_data, 1);
  if (ad == NULL) {
    return -1;
  }
  ad->audio_buffer = g_new(double, 2 * out_buffer_size);
  if (ad->audio_buffer == NULL) {
    g_free(ad);
    return -1;
  }
  ad->pastream   = pa_simple_new(NULL, // Use the default server.
                                 "piHPSDR",          // Our application's name.
                                 PA_STREAM_PLAYBACK,
                                 rx->audio_name,
                                 stream_id,          // Description of our stream.
                                 &sample_spec,       // Our sample format.
                                 NULL,               // Use default channel map
                                 &attr,              // Latency
                                 &err                // error code if returns NULL
                                );
  if (ad->pastream == NULL) {
    t_print("%s: ERROR pa_simple_new: %s\n", __func__, pa_strerror(err));
    g_free(ad);
    return -1;
  }
  ad->cwaudio = 5;
  ad->cwcount = 0;
  ad->skipcnt = 0;
  ad->audio_buffer_offset = 0;
  g_mutex_init(&ad->audio_mutex);
  rx->audio_handle = ad;
  return 0;
}

static gpointer tx_audio_thread(gpointer arg) {
  TRANSMITTER *tx = (TRANSMITTER *)arg;
  if (tx == NULL) { return NULL; }
  int err;
  float *buffer = g_new(float, inp_buffer_size);
  if (!buffer) { return NULL; }
  volatile audio_data *ad;
  while ((ad = tx->audio_handle)) {
    int rc = pa_simple_read(ad->pastream,
                            buffer,
                            inp_buffer_size * sizeof(float),
                            &err);
    if (rc < 0) { continue; }
    ad = (audio_data *) tx->audio_handle;
    if (ad == NULL) { break; }
    //
    // Now the existence of the buffers is guaranteed for 50 ms
    //
    for (unsigned int i = 0; i < inp_buffer_size; i++) {
      //
      // put sample into ring buffer
      //
      int newpt = (ad->audio_buffer_inpt + 1) & MICRINGMASK;
      if (newpt != ad->audio_buffer_outpt) {
          // buffer space available, do the write
          ad->audio_buffer[ad->audio_buffer_inpt] = (double) buffer[i];
          // atomic update of ad->audio_buffer_inpt
          ad->audio_buffer_inpt = newpt;
      }
    }
  }
  t_print("%s: exit\n", __func__);
  g_free(buffer);
  return NULL;
}

int audio_open_input(TRANSMITTER *tx) {
  if (tx == NULL) { return -1; }
  pa_sample_spec sample_spec;
  int err;
  pa_buffer_attr attr;
  //
  // PulseAudio/Pipewire accept ALSA device names as well. So when changing
  // from ALSA to pulseaudio, a device may be successfully opened although
  // its device name is not contained in our device list. This can lead to
  // inconsistencies. Therefore, return with error if the name is not
  // registered.
  //
  err = 1;
  for (int i = 0; i < n_input_devices; i++) {
    if (!strcmp(tx->audio_name, input_devices[i].name)) {
      t_print("%s TX:%s\n", __func__, input_devices[i].description);
      err = 0;
      break;
    }
  }
  if (err) {
    t_print("%s: not registered: %s\n", __func__, tx->audio_name);
    return -1;
  }
  attr.maxlength = (uint32_t) -1;
  attr.tlength = (uint32_t) -1;
  attr.prebuf = (uint32_t) -1;
  attr.minreq = (uint32_t) -1;
  attr.fragsize = 512;
  sample_spec.rate = 48000;
  sample_spec.format = PA_SAMPLE_FLOAT32NE;
  //
  // It is no problem to open stereo input devices as MONO.
  //
  sample_spec.channels = 1;
  if (tx->audio_handle != NULL) {
    tx->audio_handle = NULL;
    usleep(50000);
  }
  audio_data *ad = g_new(audio_data, 1);

  ad->pastream = pa_simple_new(NULL,      // Use the default server.
                               "piHPSDR",                   // Our application's name.
                               PA_STREAM_RECORD,
                               tx->audio_name,
                               "TX",                        // Description of our stream.
                               &sample_spec,                // Our sample format.
                               NULL,                        // Use default channel map
                               &attr,                       // Use default buffering attributes but set fragsize
                               &err                         // Ignore error code.
                              );
  if (ad->pastream == NULL) {
    g_free(ad);
    return -1;
  }
  ad->audio_buffer = g_new(double, MICRINGLEN);
  ad->audio_buffer_outpt = ad->audio_buffer_inpt = 0;
  if (ad->audio_buffer == NULL) {
    pa_simple_free(ad->pastream);
    g_free(ad);
    return -1;
  }
  GError *error;
  ad->audio_thread_id = g_thread_try_new("TxAudioIn", tx_audio_thread, tx, &error);
  if (ad->audio_thread_id == NULL) {
    t_print("%s: g_thread_new failed on tx_audio_thread: %s\n", __func__, error->message);
    pa_simple_free(ad->pastream);
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  tx->audio_handle = ad;
  return 0;
}

void audio_close_output(RECEIVER *rx) {
  if (rx == NULL) { return; }
  t_print("%s: RX%d:%s\n", __func__, rx->id + 1, rx->audio_name);
  audio_data *ad = rx->audio_handle;
  if (ad == NULL) { return; }
  rx->audio_handle = NULL;
  usleep(50000);
  pa_simple_free(ad->pastream);
  g_mutex_clear((GMutex *)(&ad->audio_mutex));
  g_free(ad->audio_buffer);
  g_free(ad);
}

void audio_close_input(TRANSMITTER *tx) {
  if (tx == NULL) { return ; }
  t_print("%s: TX:%s\n", __func__, tx->audio_name);
  audio_data *ad = (audio_data *) tx->audio_handle;
  if (ad == NULL) { return; }
  tx->audio_handle = NULL;
  if (ad->audio_thread_id != NULL) {
    g_thread_join(ad->audio_thread_id);
  }
  usleep(50000);
  pa_simple_free(ad->pastream);
  g_free(ad->audio_buffer);
  g_free(ad);
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
double audio_get_next_mic_sample(TRANSMITTER *tx) {
  if (tx == NULL) { return 0.0; }
  audio_data *ad = (audio_data *) tx->audio_handle;
  if ((ad == NULL) || (ad->audio_buffer_outpt == ad->audio_buffer_inpt)) { return 0.0; }
  int newpt = (ad->audio_buffer_outpt + 1) & MICRINGMASK;
  double sample = ad->audio_buffer[ad->audio_buffer_outpt];
  // atomic update of read pointer
  ad->audio_buffer_outpt = newpt;
  return sample;
}

//
// In the PulseAudio module, tx_audio_write() is essentially a copy
// of audio_write(). audio_write() is for RXaudio and called from the
// receiver thread, while tx_audio_write() is for CW sidetone, TUNE
// side tone and TX monitor, and is called from the transmitter thread.
//
// We want a much smaller latency for TXaudio and therefore close and
// re-open the audio stream each time we switch between RX and TX
// audio. Note that when running duplex, or when not doing CW and
// not using the TX monitor, there is no switching from RX to TX audio.
//
// The variable rx->cwaudio can have four values, indicating four phases:
//
// cwaudio == 0:  playing RX audio
// cwaudio == 1:  transition between RX and TX audio, skip samples
// cwaudio == 2:  TXaudio: play some silence to pre-fill audio buffer
// cwaudio == 3:  playing TX audio
// cwaudio == 4:  transition between TX and RX audio, skip samples
// cwaudio == 5:  RXaudio: play some silence to pre-fill audio buffer
//
// During normal RX, cwaudio is zero, and when starting playing TX audio
// it adpots values 1, 2 for a short time and then 3 during TX audio.
// On the next TX/RX transition it adopts values 4, 5 for a short time
// before resuming "normal" RX audio
//
static int do_rxtx(gpointer data) {
  RECEIVER *rx = (RECEIVER *) data;
  if (rx == NULL) { return G_SOURCE_REMOVE; }
  volatile audio_data *ad = rx->audio_handle;
  if (ad == NULL) { return G_SOURCE_REMOVE; }
  int err;
  //
  // RXTX transition: close stream and re-open with CW latency settings
  //
  g_mutex_lock((GMutex *)(&ad->audio_mutex));
  ad = rx->audio_handle;
  if (ad == NULL) { return G_SOURCE_REMOVE; }
  if (ad->cwaudio == 1) {
    pa_sample_spec sample_spec;
    sample_spec.rate = 48000;
    sample_spec.channels = 2;
    sample_spec.format = PA_SAMPLE_FLOAT32NE;
    char stream_id[16];
    snprintf(stream_id, sizeof(stream_id), "RX-%d", rx->id);
    pa_buffer_attr attr;
    //
    // Close and re-open stream
    //
    if (ad->pastream != NULL) {
      pa_simple_flush(ad->pastream, &err);
      pa_simple_free(ad->pastream);
    }
    attr.maxlength = pa_usec_to_bytes(2 * CW_LAT_MAX, &sample_spec);
    attr.tlength   = pa_usec_to_bytes(CW_LAT_TARGET,  &sample_spec);
    attr.prebuf    = pa_usec_to_bytes(CW_LAT_TARGET,  &sample_spec);
    attr.minreq    = (uint32_t) -1;
    attr.fragsize  = (uint32_t) -1;
    ad->pastream = pa_simple_new(NULL, // Use the default server.
                                 "piHPSDR",          // Our application's name.
                                 PA_STREAM_PLAYBACK,
                                 rx->audio_name,
                                 stream_id,          // Description of our stream.
                                 &sample_spec,       // Our sample format.
                                 NULL,               // Use default channel map
                                 &attr,              // Latency
                                 &err                // error code if returns NULL
                                );
    if (ad->pastream == NULL) {
      t_print("%s: ERROR pa_simple_new: %s\n", __func__, pa_strerror(err));
    }
    ad->cwaudio = 2;
  }
  g_mutex_unlock((GMutex *)(&ad->audio_mutex));
  return G_SOURCE_REMOVE;
}

void tx_audio_write(RECEIVER *rx, double sample) {
  if (rx == NULL) { return; }
  int err;
  //
  // While audio stream is being re-opened, return
  //
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL || ad->cwaudio == 1 || ad->cwaudio == 4) { return; }
  g_mutex_lock(&ad->audio_mutex);
  ad = rx->audio_handle;
  if (ad != NULL) {
    if (ad->cwaudio == 0 || ad->cwaudio == 5) {
      ad->cwaudio = 1;
      ad->cwcount = 0;
      ad->skipcnt = 0;
      ad->audio_buffer_offset = 0;
      g_idle_add(do_rxtx, (gpointer) rx);
      g_mutex_unlock(&ad->audio_mutex);
      return;
    }
    if (ad->cwaudio == 2) {
      //
      // Insert silence that amounts to low-water filling
      //
      float buffer[2 * out_buffer_size];
      memset(buffer, 0, 2 * out_buffer_size * sizeof(float));
      for (int i = 0; i < (CW_LAT_LOW + 10 * out_buffer_size) / (20 * out_buffer_size); i++) {
        int rc = pa_simple_write(ad->pastream, buffer,
                                 2 * out_buffer_size * sizeof(float),
                                 &err);
        if (rc < 0) {
          t_print("%s: ERROR pa_simple_write: %s\n", __func__, pa_strerror(err));
        }
      }
      ad->cwaudio = 3;
      ad->latency = CW_LAT_LOW; // do not adjust until first measured
    }
    int adjust = 1;
    if (sample != 0.0) { ad->cwcount = 0; }
    if (++ad->cwcount >= 16) {
      ad->cwcount = 0;
      //
      // We arrive here if we have seen 16 zero samples in a row.
      //
      if (ad->latency > CW_LAT_TARGET) { adjust = 0; } // full: we are above target
      if (ad->latency < CW_LAT_LOW ) { adjust = 2; } // low: we are below low water mark
    }
    switch (adjust) {
    case 1:
      //
      // default case: put sample into buffer and that's it
      //
      ad->audio_buffer[ad->audio_buffer_offset * 2] = sample;
      ad->audio_buffer[ad->audio_buffer_offset * 2 + 1] = sample;
      ad->audio_buffer_offset++;
      break;
    case 2:
      //
      // write it twice if space permits
      //
      ad->audio_buffer[ad->audio_buffer_offset * 2] = sample;
      ad->audio_buffer[ad->audio_buffer_offset * 2 + 1] = sample;
      ad->audio_buffer_offset++;
      if (ad->audio_buffer_offset <  out_buffer_size) {
        ad->audio_buffer[ad->audio_buffer_offset * 2] = sample;
        ad->audio_buffer[ad->audio_buffer_offset * 2 + 1] = sample;
        ad->audio_buffer_offset++;
      }
      break;
    default:
      //
      // Skip sample
      //
      break;
    }
    if (ad->audio_buffer_offset >= out_buffer_size) {
      ad->latency = pa_simple_get_latency(ad->pastream, &err);
      if (ad->latency > CW_LAT_MAX && ad->skipcnt == 0) {
        //
        // If the radio is running a a slightly too high clock rate, or if
        // the audio hardware clocks slightly below 48 kHz, then the PA audio
        // buffer will fill up. suppress audio data until the latency is below
        // TARGET, or until a pre-calculated maximum number of output
        // buffers has been suppressed.
        // 20 * out_buffer_size is the number of microseconds one buffer
        // contains.
        //
        ad->skipcnt = (ad->latency - CW_LAT_TARGET) / (20 * out_buffer_size);
        t_print("%s: suppressing audio block\n", __func__);
      }
      if (ad->skipcnt > 0) {
        ad->skipcnt--;
      }
      if (ad->skipcnt == 0 || ad->latency < CW_LAT_TARGET) {
        //
        // Write output buffer. To this end, a C variable length
        // array is allocated to do the conversion from internal (double)
        // to PulseAudio (float) format.
        //
        float buffer[2 * out_buffer_size];
        for (int i = 0; i < 2 * out_buffer_size; i++) {
          buffer[i] = (float) ad->audio_buffer[i];
        }
        int rc = pa_simple_write(ad->pastream,
                                 buffer,
                                 2 * out_buffer_size * sizeof(float),
                                 &err);
        if (rc < 0) {
          t_print("%s: ERROR pa_simple_write: %s\n", __func__, pa_strerror(err));
        }
      }
      ad->audio_buffer_offset = 0;
    }
  }
  g_mutex_unlock(&ad->audio_mutex);
  return;
}


static int do_txrx(gpointer data) {
  RECEIVER *rx = (RECEIVER *) data;
  if (rx == NULL) { return G_SOURCE_REMOVE; }
  int err;
  //
  // TXRX transition: close stream and re-open with RX latency settings
  //
  volatile audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) { return G_SOURCE_REMOVE; }
  g_mutex_lock((GMutex *)(&ad->audio_mutex));
  ad = rx->audio_handle;
  if (ad == NULL) { return G_SOURCE_REMOVE; }
  if (ad->cwaudio == 4) {
    pa_sample_spec sample_spec;
    sample_spec.rate = 48000;
    sample_spec.channels = 2;
    sample_spec.format = PA_SAMPLE_FLOAT32NE;
    char stream_id[16];
    snprintf(stream_id, sizeof(stream_id), "RX-%d", rx->id);
    pa_buffer_attr attr;
    //
    // Close and re-open stream
    //
    if (ad->pastream != NULL) {
      pa_simple_flush(ad->pastream, &err);
      pa_simple_free(ad->pastream);
    }
    attr.maxlength = pa_usec_to_bytes(2 * AUDIO_LAT_MAX, &sample_spec);
    attr.tlength   = pa_usec_to_bytes(AUDIO_LAT_TARGET,  &sample_spec);
    attr.prebuf    = pa_usec_to_bytes(AUDIO_LAT_TARGET, &sample_spec);
    attr.minreq    = (uint32_t) -1;
    attr.fragsize  = (uint32_t) -1;
    ad->pastream = pa_simple_new(NULL, // Use the default server.
                                 "piHPSDR",          // Our application's name.
                                 PA_STREAM_PLAYBACK,
                                 rx->audio_name,
                                 stream_id,          // Description of our stream.
                                 &sample_spec,       // Our sample format.
                                 NULL,               // Use default channel map
                                 &attr,              // Latency
                                 &err                // error code if returns NULL
                                );
    if (ad->pastream == NULL) {
      t_print("%s: ERROR pa_simple_new: %s\n", __func__, pa_strerror(err));
    }
    ad->cwaudio = 5;
  }
  g_mutex_unlock((GMutex *)(&ad->audio_mutex));
  return G_SOURCE_REMOVE;
}

void audio_write(RECEIVER *rx, double left, double right) {
  if (rx == NULL) { return; }
  int err;
  //
  // If transmitting without duplex, quickly return
  //
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return; }
  volatile audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad->cwaudio == 1 || ad->cwaudio == 4) { return; }
  g_mutex_lock((GMutex *)(&ad->audio_mutex));
  ad = rx->audio_handle;
  if (ad != NULL) {
    if (ad->cwaudio == 3) {
      ad->cwaudio = 4;
      ad->cwcount = 0;
      ad->skipcnt = 0;
      ad->latency = 0;
      ad->audio_buffer_offset = 0;
      g_idle_add(do_txrx, (gpointer) rx);
      g_mutex_unlock((GMutex *)(&ad->audio_mutex));
      return;
    }
    if (ad->cwaudio == 5) {
      //
      // Insert silence that amounts target filling
      //
      float buffer[2 * out_buffer_size];
      memset(buffer, 0, 2 * out_buffer_size * sizeof(float));
      for (int i = 0; i < (AUDIO_LAT_TARGET + 10 * out_buffer_size) / (20 * out_buffer_size); i++) {
        int rc = pa_simple_write(ad->pastream, buffer,
                                 2 * out_buffer_size * sizeof(float),
                                 &err);
        if (rc < 0) {
          t_print("%s: ERROR pa_simple_write: %s\n", __func__, pa_strerror(err));
        }
      }
      ad->cwaudio = 0;
    }
    ad->audio_buffer[ad->audio_buffer_offset * 2] = left;
    ad->audio_buffer[(ad->audio_buffer_offset * 2) + 1] = right;
    ad->audio_buffer_offset++;
    if (ad->audio_buffer_offset >= out_buffer_size) {
      ad->latency = pa_simple_get_latency(ad->pastream, &err);
      if (ad->latency > AUDIO_LAT_MAX && ad->skipcnt == 0) {
        //
        // If the radio is running a a slightly too high clock rate, or if
        // the audio hardware clocks slightly below 48 kHz, then the PA audio
        // buffer will fill up. suppress audio data until the latency is below
        // AUDIO_LAT_LOW, or until a pre-calculated maximum number of output
        // buffers has been suppressed.
        //
        ad->skipcnt = (ad->latency - AUDIO_LAT_TARGET) / (20 * out_buffer_size);
        t_print("%s: suppressing audio block\n", __func__);
      }
      if (ad->skipcnt > 0) {
        ad->skipcnt--;
      }
      if (ad->skipcnt == 0 || ad->latency < AUDIO_LAT_TARGET) {
        //
        // Write output buffer. To this end, a C variable length
        // array is allocated to do the conversion from internal (double)
        // to PulseAudio (float) format.
        //
        float buffer[2 * out_buffer_size];
        for (int i = 0; i < 2 * out_buffer_size; i++) {
          buffer[i] = (float) ad->audio_buffer[i];
        }
        int rc = pa_simple_write(ad->pastream, buffer,
                                 2 * out_buffer_size * sizeof(float),
                                 &err);
        if (rc < 0) {
          t_print("%s: ERROR pa_simple_write: %s\n", __func__, pa_strerror(err));
        }
      }
      ad->audio_buffer_offset = 0;
    }
  }
  g_mutex_unlock((GMutex *)(&ad->audio_mutex));
  return;
}
