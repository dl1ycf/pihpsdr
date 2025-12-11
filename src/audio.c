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

#include <gtk/gtk.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>

#include <alsa/asoundlib.h>

#include "audio.h"
#include "client_server.h"
#include "message.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "vfo.h"

//
// Some important parameters
// Note that we keep the rx audio buffers at half-filling so
// we can use a larger latency there.
//
//
#define inp_latency  125000
#define out_latency  200000

//
// ALSA loopback devices, when connected to digimode programs, sometimes
// deliver audio in large chungs, so we need a large ring buffer as well
//
#define MICRINGLEN 6000

static const int inp_buffer_size = 256;
static const int out_buffer_size = 256;

static const int out_buflen = 48 * (out_latency / 1000);   // Length of ALSA buffer (200 msec) in samples
static const int out_maxlen = 44 * (out_latency / 1000);   // High-Water (183 msec) in samples

static const int cw_low_water  =  816;                     // low water mark for CW (17 msec)
static const int cw_mid_water  =  960;                     // target water mark for CW (20 msec)
static const int cw_high_water = 1104;                     // high water mark for CW (23 msec)

//
// TODO: include SND_PCM_FORMAT_IEC958_SUBFRAME_LE, such that ALSA
//       can directly play on HDMI monitors. Implementation is not
//       super-easy since this case must then also be considered in
//       audio_write.
//
#define FORMATS 3
static snd_pcm_format_t formats[4] = {
  SND_PCM_FORMAT_FLOAT_LE,
  SND_PCM_FORMAT_S32_LE,
  SND_PCM_FORMAT_S16_LE,
  SND_PCM_FORMAT_UNKNOWN
};

static void *tx_audio_thread(void *arg);

int n_input_devices;
int n_output_devices;

AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

int audio_open_output(RECEIVER *rx) {
  unsigned int rate = 48000;
  unsigned int channels = 2;
  int soft_resample = 1;
  char hw[128];
  t_print("%s: RX%d:%s\n", __FUNCTION__, rx->id + 1, rx->audio_name);

  for (int i = 0; ; i++) {
    if (i == 127 || rx->audio_name[i] == ' ') {
      hw[i] = '\0';
      break;
    }

    hw[i] = rx->audio_name[i];
  }

  g_mutex_lock(&rx->audio_mutex);
  rx->audio_format = SND_PCM_FORMAT_UNKNOWN;
  //
  // Upon unsuccessful return, these variables must be NULL
  // such that audio_close_output() can safely be called
  //
  rx->audio_handle = NULL;
  rx->audio_buffer = NULL;

  for (int i = 0; i < FORMATS; i++) {
    int err;

    if ((err = snd_pcm_open (&rx->audio_handle, hw, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
      t_print("%s: cannot open audio device %s (%s)\n", __FUNCTION__,
              hw,
              snd_strerror (err));
      break;
    }

    if ((err = snd_pcm_set_params (rx->audio_handle, formats[i], SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate,
                                   soft_resample, out_latency)) < 0) {
      t_print("%s: could not set params for %s\n", __FUNCTION__, snd_pcm_format_name(formats[i]));
      snd_pcm_close(rx->audio_handle);
      continue;
    }

    t_print("%s: using format %s (%s)\n", __FUNCTION__, snd_pcm_format_name(formats[i]),
            snd_pcm_format_description(formats[i]));
    rx->audio_format = formats[i];
    break;
  }

  if (rx->audio_format == SND_PCM_FORMAT_UNKNOWN) {
    t_print("%s: Device cannot be used\n", __FUNCTION__);
    rx->audio_handle = NULL;
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  rx->audio_buffer_offset = 0;

  switch (rx->audio_format) {
  case SND_PCM_FORMAT_S16_LE:
    rx->audio_buffer = g_new(int16_t, 2 * out_buffer_size);
    break;

  case SND_PCM_FORMAT_S32_LE:
    rx->audio_buffer = g_new(int32_t, 2 * out_buffer_size);
    break;

  case SND_PCM_FORMAT_FLOAT_LE:
    rx->audio_buffer = g_new(float, 2 * out_buffer_size);
    break;

  default:
    t_print("%s: unknown sound format\n", __FUNCTION__);
    rx->audio_buffer = NULL;
    break;
  }

  if (rx->audio_buffer == NULL) {
    snd_pcm_close(rx->audio_handle);
    rx->audio_handle = NULL;
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  rx->cwaudio = 0;
  rx->cwcount = 0;
  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

int audio_open_input(TRANSMITTER *tx) {
  unsigned int rate = 48000;
  unsigned int channels = 1;
  int soft_resample = 1;
  char hw[128];
  t_print("%s: TX:%s\n", __FUNCTION__, tx->audio_name);

  for (int i = 0; ; i++) {
    if (i == 127 || tx->audio_name[i] == ' ') {
      hw[i] = '\0';
      break;
    }

    hw[i] = tx->audio_name[i];
  }

  tx->audio_format = SND_PCM_FORMAT_UNKNOWN;
  //
  // It must be guaranteed that in case of failure, these three
  // variables are NULL such that audio_close_input() can safely
  // be called.
  //
  tx->audio_buffer = NULL;
  tx->audio_thread_id = NULL;
  tx->audio_handle = NULL;
  g_mutex_lock(&tx->audio_mutex);

  for (int i = 0; i < FORMATS; i++) {
    int err;

    if ((err = snd_pcm_open (&tx->audio_handle, hw, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC)) < 0) {
      t_print("%s: cannot open audio device %s (%s)\n", __FUNCTION__,
              hw,
              snd_strerror (err));
      break;
    }

    if ((err = snd_pcm_set_params (tx->audio_handle, formats[i], SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate,
                                   soft_resample,
                                   inp_latency)) < 0) {
      t_print("%s: could not set params for %s\n", __FUNCTION__, snd_pcm_format_name(formats[i]));
      snd_pcm_close(tx->audio_handle);
      continue;
    }

    t_print("%s: using format %s (%s)\n", __FUNCTION__, snd_pcm_format_name(formats[i]),
            snd_pcm_format_description(formats[i]));
    tx->audio_format = formats[i];
    break;
  }

  if (tx->audio_format == SND_PCM_FORMAT_UNKNOWN) {
    t_print("%s: device cannot be used\n", __FUNCTION__);
    tx->audio_handle = NULL;
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  t_print("%s: format=%d\n", __FUNCTION__, tx->audio_format);
  t_print("%s: allocating ring buffer\n", __FUNCTION__);
  tx->audio_buffer = (float *) g_new(float, MICRINGLEN);
  tx->audio_buffer_outpt = tx->audio_buffer_inpt = 0;

  if (tx->audio_buffer == NULL) {
    snd_pcm_close(tx->audio_handle);
    tx->audio_handle = NULL;
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  GError *error;
  tx->audio_thread_id = g_thread_try_new("TxAudioIn", tx_audio_thread, tx, &error);

  if (!tx->audio_thread_id ) {
    t_print("%s: g_thread_new failed on TxAudioIn: %s\n", __FUNCTION__, error->message);
    snd_pcm_close(tx->audio_handle);
    tx->audio_handle = NULL;
    g_free(tx->audio_buffer);
    tx->audio_buffer = NULL;
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  g_mutex_unlock(&tx->audio_mutex);
  return 0;
}

void audio_close_output(RECEIVER *rx) {
  t_print("%s: RX%d:%s\n", __FUNCTION__, rx->id + 1, rx->audio_name);
  g_mutex_lock(&rx->audio_mutex);

  if (rx->audio_handle != NULL) {
    snd_pcm_close (rx->audio_handle);
    rx->audio_handle = NULL;
  }

  if (rx->audio_buffer != NULL) {
    g_free(rx->audio_buffer);
    rx->audio_buffer = NULL;
  }

  g_mutex_unlock(&rx->audio_mutex);
}

void audio_close_input(TRANSMITTER *tx) {
  t_print("%s: TX:%s\n", __FUNCTION__, tx->audio_name);
  tx->audio_running = FALSE;
  g_mutex_lock(&tx->audio_mutex);

  if (tx->audio_thread_id != NULL) {
    g_thread_join(tx->audio_thread_id);
    tx->audio_thread_id = NULL;
  }

  if (tx->audio_handle != NULL) {
    snd_pcm_close (tx->audio_handle);
    tx->audio_handle = NULL;
  }

  if (tx->audio_buffer != NULL) {
    g_free(tx->audio_buffer);
  }

  g_mutex_unlock(&tx->audio_mutex);
}

//
// tx_audio_write() is called from the transmitter thread
// when transmitting and not doing duplex.
// Its main use is the CW side tone, so to minimize
// sidetone latency, hold the ALSA buffer
// at low filling, between cw_low_water and cw_high_water.
//
// Note that when sending the buffer, delay "jumps" by the buffer size
//

int tx_audio_write(RECEIVER *rx, float sample) {
  snd_pcm_sframes_t delay;
  g_mutex_lock(&rx->audio_mutex);

  if (rx->audio_handle != NULL && rx->audio_buffer != NULL) {
    if (rx->cwaudio == 0) {
      //
      // This happens when we come here for the first time after a
      // RX/TX transision. Rewind output buffer, that is, discard
      // the most recent output samples.
      // In principle, we should apply a fade-out on the samples
      // still remaining in the output buffer. In the portaudio
      // module this is easy because we do the buffering and
      // use callbacks to actually deliver the audio data.
      // Callbacks with ALSA are not considered "Safe" so
      // we use snd_pcm_writei() and (AFAIK) after that we
      // cannot modify the audio samples already sent.
      //
      // Bottom line: this snd_pcm_rewind() most likely leads
      // to a small audio crack upon each RX/TX transition, since
      // the pending RX audio samples come to a sudden end
      // without any down-slew.
      //
      if (snd_pcm_delay(rx->audio_handle, &delay) == 0) {
        snd_pcm_rewind(rx->audio_handle, delay - cw_mid_water);
      }

      rx->cwcount = 0;
      rx->cwaudio = 1;
    }

    int adjust = 1;

    if (sample != 0.0) { rx->cwcount = 0; } // count upwards during silence

    if (++rx->cwcount >= 16) {
      rx->cwcount = 0;

      //
      // We have just seen 16 zero samples, so this is the right place
      // to adjust the buffer filling.
      // If buffer gets too full   ==> skip the sample
      // If buffer gets too empty ==> insert zero sample
      //

      if (snd_pcm_delay(rx->audio_handle, &delay) == 0) {
        if (delay > cw_high_water) { adjust = 0; }  // above high-water

        if (delay < cw_low_water)  { adjust = 2; }  // below low-water
      }
    }

    //
    // adjust == 1: put sample into buffer (default case)
    // adjust == 2: put sample into buffer twice (if space permits)
    // adjust == 0: skip sample
    //
    if (adjust) {
      //
      // default case: put sample into buffer and that's it
      //
      switch (rx->audio_format) {
      case SND_PCM_FORMAT_S16_LE: {
        int16_t *short_buffer = (int16_t *)rx->audio_buffer;
        int16_t shortsample = (int16_t) (sample * 32767.0F);
        short_buffer[rx->audio_buffer_offset * 2] = shortsample;
        short_buffer[(rx->audio_buffer_offset * 2) + 1] = shortsample;
        rx->audio_buffer_offset++;

        if (adjust == 2 && rx->audio_buffer_offset <  out_buffer_size) {
          short_buffer[rx->audio_buffer_offset * 2] = shortsample;
          short_buffer[(rx->audio_buffer_offset * 2) + 1] = shortsample;
          rx->audio_buffer_offset++;
        }
      }
      break;

      case SND_PCM_FORMAT_S32_LE: {
        int32_t *long_buffer = (int32_t *)rx->audio_buffer;
        int32_t longsample = (int32_t)(sample *  2147483647.0F);
        long_buffer[rx->audio_buffer_offset * 2] = longsample;
        long_buffer[(rx->audio_buffer_offset * 2) + 1] = longsample;
        rx->audio_buffer_offset++;

        if (adjust == 2 && rx->audio_buffer_offset <  out_buffer_size) {
          long_buffer[rx->audio_buffer_offset * 2] = longsample;
          long_buffer[(rx->audio_buffer_offset * 2) + 1] = longsample;
          rx->audio_buffer_offset++;
        }
      }
      break;

      case SND_PCM_FORMAT_FLOAT_LE: {
        float *float_buffer = (float *)rx->audio_buffer;
        float_buffer[rx->audio_buffer_offset * 2] = sample;
        float_buffer[(rx->audio_buffer_offset * 2) + 1] =  sample;
        rx->audio_buffer_offset++;

        if (adjust == 2 && rx->audio_buffer_offset <  out_buffer_size) {
          float_buffer[rx->audio_buffer_offset * 2] = sample;
          float_buffer[(rx->audio_buffer_offset * 2) + 1] = sample;
          rx->audio_buffer_offset++;
        }
      }
      break;

      default:
        t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
        break;
      }
    }

    if (rx->audio_buffer_offset >= out_buffer_size) {
      long rc;

      if ((rc = snd_pcm_writei (rx->audio_handle, rx->audio_buffer, out_buffer_size)) != out_buffer_size) {
        if (rc < 0) {
          switch (rc) {
          case -EPIPE:
            if ((rc = snd_pcm_prepare (rx->audio_handle)) < 0) {
              t_print("%s: cannot prepare audio interface for use %ld (%s)\n", __FUNCTION__, rc, snd_strerror (rc));
              rx->audio_buffer_offset = 0;
              g_mutex_unlock(&rx->audio_mutex);
              return rc;
            }

            break;

          default:
            t_print("%s:  write error: %s\n", __FUNCTION__, snd_strerror(rc));
            break;
          }
        } else {
          t_print("%s: short write lost=%d\n", __FUNCTION__, out_buffer_size - (int) rc);
        }
      }

      rx->audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

//
// if rx == active_receiver and while transmitting, DO NOTHING
// since tx_audio_write may be active
//

int audio_write(RECEIVER *rx, float left_sample, float right_sample) {
  snd_pcm_sframes_t delay;

  //
  // When transmitting while not doing duplex, quickly return
  //
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return 0; }

  // lock AFTER checking the "quick return" condition but BEFORE checking the pointers
  g_mutex_lock(&rx->audio_mutex);

  if (rx->audio_handle != NULL && rx->audio_buffer != NULL) {
    switch (rx->audio_format) {
    case SND_PCM_FORMAT_S16_LE: {
      int16_t *short_buffer = (int16_t *)rx->audio_buffer;
      short_buffer[rx->audio_buffer_offset * 2] = (int16_t)(left_sample * 32767.0F);
      short_buffer[(rx->audio_buffer_offset * 2) + 1] = (int16_t)(right_sample * 32767.0F);
    }
    break;

    case SND_PCM_FORMAT_S32_LE: {
      int32_t *long_buffer = (int32_t *)rx->audio_buffer;
      long_buffer[rx->audio_buffer_offset * 2] = (int32_t)(left_sample * 4294967295.0F);
      long_buffer[(rx->audio_buffer_offset * 2) + 1] = (int32_t)(right_sample * 4294967295.0F);
    }
    break;

    case SND_PCM_FORMAT_FLOAT_LE: {
      float *float_buffer = (float *)rx->audio_buffer;
      float_buffer[rx->audio_buffer_offset * 2] = left_sample;
      float_buffer[(rx->audio_buffer_offset * 2) + 1] = right_sample;
    }
    break;

    default:
      t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
      break;
    }

    rx->audio_buffer_offset++;

    if (rx->audio_buffer_offset >= out_buffer_size) {
      snd_pcm_sframes_t rc;

      if (snd_pcm_delay(rx->audio_handle, &delay) != 0) {
        delay = 0;
      }

      if (rx->cwaudio == 1 || delay < 512) {
        //
        // This happens when we come here for the first time, or after a
        // TX/RX transision. We have to fill the output buffer (otherwise
        // sound will not resume) and can then rewind to half-filling.
        // (We may also arrive here if the output buffer is nearly drained)
        //
        //
        void *silence = NULL;
        size_t len;
        int num = (out_buflen - delay);

        switch (rx->audio_format) {
        case SND_PCM_FORMAT_S16_LE:
          silence = g_new(int16_t, 2 * num);
          len = 2 * num * sizeof(int16_t);
          break;

        case SND_PCM_FORMAT_S32_LE:
          silence = g_new(int32_t, 2 * num);
          len = 2 * num * sizeof(int32_t);
          break;

        case SND_PCM_FORMAT_FLOAT_LE:
          silence = g_new(float, 2 * num);
          len = 2 * num * sizeof(float);
          break;

        default:
          t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
          silence = NULL;
          len = 0;
          break;
        }

        if (silence) {
          memset(silence, 0, len);
          snd_pcm_writei (rx->audio_handle, silence, num);
          snd_pcm_rewind (rx->audio_handle, out_buflen / 2);
          delay = out_buflen / 2;
          g_free(silence);
        }

        rx->cwaudio = 0;
      }

      if (delay > out_maxlen) {
        // output buffer is filling up, rewind until it is half filled
        snd_pcm_rewind(rx->audio_handle, out_buflen / 2);
      }

      if ((rc = snd_pcm_writei (rx->audio_handle, rx->audio_buffer, out_buffer_size)) != out_buffer_size) {
        if (rc < 0) {
          switch (rc) {
          case -EPIPE:
            if ((rc = snd_pcm_prepare (rx->audio_handle)) < 0) {
              t_print("%s: cannot prepare audio interface for use %ld (%s)\n", __FUNCTION__, rc, snd_strerror (rc));
              rx->audio_buffer_offset = 0;
              g_mutex_unlock(&rx->audio_mutex);
              return rc;
            }

            break;

          default:
            t_print("%s:  write error: %s\n", __FUNCTION__, snd_strerror(rc));
            break;
          }
        } else {
          t_print("%s: short write lost=%d\n", __FUNCTION__, out_buffer_size - (int) rc);
        }
      }

      rx->audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

static void *tx_audio_thread(gpointer arg) {
  TRANSMITTER *tx = (TRANSMITTER *)arg;
  int rc;

  if ((rc = snd_pcm_start (tx->audio_handle)) < 0) {
    t_print("%s: cannot start audio interface for use (%s)\n", __FUNCTION__,
            snd_strerror (rc));
    return NULL;
  }

  //
  // Allocate buffer such that it fits for all
  //
  void *buffer = malloc(inp_buffer_size * sizeof(float));

  if (!buffer) {
    t_print("%s: unknown sound format or malloc error\n");
    return NULL;
  }

  const int16_t *short_buffer =  (int16_t *) buffer;
  const int32_t *long_buffer =  (int32_t *) buffer;
  const float *float_buffer =  (float *) buffer;
  tx->audio_running = TRUE;

  while (tx->audio_running) {
    if ((rc = snd_pcm_readi (tx->audio_handle, buffer, inp_buffer_size)) != inp_buffer_size) {
      if (tx->audio_running) {
        if (rc < 0) {
          t_print("%s: read from audio interface failed (%s)\n", __FUNCTION__,
                  snd_strerror (rc));
        } else {
          t_print("%s: read %d\n", __FUNCTION__, rc);
        }
      }
    } else {
      // process the mic input
      for (int i = 0; i < inp_buffer_size; i++) {
        float sample;

        switch (tx->audio_format) {
        case SND_PCM_FORMAT_S16_LE:
          sample = (float)short_buffer[i] / 32767.0f;
          break;

        case SND_PCM_FORMAT_S32_LE:
          sample = (float)long_buffer[i] / 4294967295.0f;
          break;

        case SND_PCM_FORMAT_FLOAT_LE:
          sample = float_buffer[i];
          break;

        default:
          sample = 0.0;
          break;
        }

        //
        // If we are a client, simply collect and transfer data
        // to the server without any buffering
        //
        if (radio_is_remote) {
          short s = sample * 32767.0;
          server_tx_audio(s);
          continue;
        }

        //
        // put sample into ring buffer
        // Note check on the mic ring buffer is not necessary
        // since audio_close_input() waits for this thread to
        // complete.
        //
        if (tx->audio_buffer != NULL) {
          // do not increase tx->audio_buffer_inpt *here* since it must
          // assume an illegal value at any time
          int newpt = tx->audio_buffer_inpt + 1;

          if (newpt == MICRINGLEN) { newpt = 0; }

          if (newpt != tx->audio_buffer_outpt) {
            // buffer space available, do the write
            tx->audio_buffer[tx->audio_buffer_inpt] = sample;
            // atomic update of tx->audio_buffer_outpt
            tx->audio_buffer_inpt = newpt;
          }
        }
      }
    }
  }

  free(buffer);
  t_print("%s: exiting\n", __FUNCTION__);
  return NULL;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample(TRANSMITTER *tx) {
  float sample;
  g_mutex_lock(&tx->audio_mutex);

  if ((tx->audio_buffer == NULL) || (tx->audio_buffer_inpt == tx->audio_buffer_outpt)) {
    // no buffer, or nothing in buffer: insert silence
    sample = 0.0;
  } else {
    int newpt = tx->audio_buffer_outpt + 1;

    if (newpt == MICRINGLEN) { newpt = 0; }

    sample = tx->audio_buffer[tx->audio_buffer_outpt];
    // atomic update of read pointer
    tx->audio_buffer_outpt = newpt;
  }

  g_mutex_unlock(&tx->audio_mutex);
  return sample;
}

void audio_get_cards() {
  snd_ctl_card_info_t *info;
  snd_pcm_info_t *pcminfo;
  char text[256];
  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);
  int card = -1;
  n_input_devices = 0;
  n_output_devices = 0;
  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);

  while (snd_card_next(&card) >= 0 && card >= 0) {
    snd_ctl_t *handle;
    char name[20];
    snprintf(name, sizeof(name), "hw:%d", card);

    if (snd_ctl_open(&handle, name, 0) < 0) {
      continue;
    }

    if (snd_ctl_card_info(handle, info) < 0) {
      snd_ctl_close(handle);
      continue;
    }

    int dev = -1;

    while (snd_ctl_pcm_next_device(handle, &dev) >= 0 && dev >= 0) {
      snd_pcm_info_set_device(pcminfo, dev);
      snd_pcm_info_set_subdevice(pcminfo, 0);
      // input devices
      snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);

      if (snd_ctl_pcm_info(handle, pcminfo) == 0) {
        char device_id[128];
        snprintf(device_id, sizeof(device_id), "plughw:%d,%d %s", card, dev, snd_ctl_card_info_get_name(info));

        if (n_input_devices < MAX_AUDIO_DEVICES) {
          // the two allocated strings will never be free'd
          input_devices[n_input_devices].name = strdup(device_id);
          input_devices[n_input_devices].description = strdup(device_id);
          input_devices[n_input_devices].index = 0; // not used
          n_input_devices++;
          t_print("%s: input_device: %s\n", device_id, __FUNCTION__);
        }
      }

      // ouput devices
      snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);

      if (snd_ctl_pcm_info(handle, pcminfo) == 0) {
        char device_id[128];
        snprintf(device_id, sizeof(device_id), "plughw:%d,%d %s", card, dev, snd_ctl_card_info_get_name(info));

        if (n_output_devices < MAX_AUDIO_DEVICES) {
          // the two allocated strings will never be free'd
          output_devices[n_output_devices].name = strdup(device_id);
          output_devices[n_output_devices].description = strdup(device_id);
          output_devices[n_output_devices].index = 0; // not used
          n_output_devices++;
          t_print("%s: output_device: %s\n", __FUNCTION__, device_id);
        }
      }
    }

    snd_ctl_close(handle);
  }

  //
  // look for dmix and dsnoop
  // We can get a very long list of names here, so only watch out
  // for those starting with dmix: or dsnoop:
  // Furthermore, truncate the description at the first newline
  //
  void **hints, **n;
  char *name, *descr, *io;

  if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
    return;
  }

  n = hints;

  while (*n != NULL) {
    name = snd_device_name_get_hint(*n, "NAME");
    descr = snd_device_name_get_hint(*n, "DESC");
    io = snd_device_name_get_hint(*n, "IOID");

    if (strncmp("dmix:", name, 5) == 0) {
      if (n_output_devices < MAX_AUDIO_DEVICES) {
        output_devices[n_output_devices].name = g_strdup(name);
        snprintf(text, sizeof(text), "(MIX) %s", descr);

        for (unsigned int i = 0; i < strlen(text); i++) {
          if (text[i] == '\n') {
            text[i] = '\0';
            break;
          }
        }

        output_devices[n_output_devices].description = g_strdup(text);
        output_devices[n_output_devices].index = 0; // not used
        n_output_devices++;
        t_print("%s: output_device: name=%s descr=%s\n", __FUNCTION__, name, descr);
      }
    }

#if 0

    //
    // (Temporarily) deactivated "dsnoop" devices. Opening them in MONO always
    // fails on my RaspPi (channels == 1 not supported)
    //
    if (strncmp("dsnoop:", name, 6) == 0) {
      if (n_input_devices < MAX_AUDIO_DEVICES) {
        input_devices[n_input_devices].name = g_strdup(name);
        snprintf(text, sizeof(text), "(SNOOP) %s", descr);

        for (unsigned int i = 0; i < strlen(text); i++) {
          if (text[i] == '\n') {
            text[i] = '\0';
            break;
          }
        }

        input_devices[n_input_devices].description = g_strdup(text);
        input_devices[n_input_devices].index = 0; // not used
        n_input_devices++;
        t_print("%s: input_device: name=%s descr=%s\n", __FUNCTION__, name, descr);
      }
    }

#endif

    //
    //  For these three items, use free() instead of g_free(),
    //  since these have been allocated by ALSA via
    //  snd_device_name_get_hint()
    //
    if (name != NULL) {
      free(name);
    }

    if (descr != NULL) {
      free(descr);
    }

    if (io != NULL) {
      free(io);
    }

    n++;
  }

  snd_device_name_free_hint(hints);
}
