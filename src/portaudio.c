/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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

#ifdef PORTAUDIO
//
// Alternate "audio" module using PORTAUDIO instead of ALSA
// (e.g. on MacOS)
//
// If PortAudio is NOT used, this file is empty, and audio.c
// is used instead.
//

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <portaudio.h>

#include "audio.h"
#include "client_server.h"
#include "message.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"

int n_input_devices;
int n_output_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

//
// We now use callback functions to provide the "headphone" audio data,
// and therefore can control the latency.
// RX audio samples are put into a ring buffer and "fetched" therefreom
// by the portaudio "headphone" callback.
//
// We choose a ring buffer of 9600 (stereo) samples that is kept about half-full
// during RX (latency: 0.1 sec) which should be more than enough.
// If the buffer falls below 1800, half a buffer length of silence is
// inserted. This usually only happens after TX/RX transitions
//
// During TX (no duplex), tx_audio_write() is called. If it is called for
// the first time with a non-zero sidetone volume,
// the ring buffer is cleared and only few (stereo) samples of silence
// are put into it. This is probably the minimum amount necessary to avoid
// audio underruns which manifest themselves as ugly cracks in the side ton.
// During the TX phase, the buffer filling is kept between two rather low
// water marks to ensure the small CW sidetone latency is kept.
// If we then go to RX again a "low water mark" condition is detected in the
// first call to audio_write() and half a buffer length of silence is inserted
// again.
// Of course, a small portaudio audio buffer size (128 samples) helps
// keeping the latency small. With the CW low/high water marks of 192/320
// I have achieved a latency of about 15 msec on a Macintosh.
// One can go smaller but this increases the probabilities of audio cracks
// from buffer underruns.
//
// Experiments indicate that we can indeed keep the ring buffer about half filling
// during RX and quite empty during CW-TX.
//
//

#define MY_AUDIO_BUFFER_SIZE  128
#define MY_RING_BUFFER_SIZE  9600
#define MY_RING_LOW_WATER     512
#define MY_RING_HIGH_WATER   9000
#define MY_CW_LOW_WATER       192
#define MY_CW_HIGH_WATER      320

//
// AUDIO_GET_CARDS
//
// This inits PortAudio and looks for suitable input and output channels
//
void audio_get_cards() {
  int numDevices;
  PaStreamParameters inputParameters, outputParameters;
  PaError err;
  err = Pa_Initialize();

  if ( err != paNoError ) {
    t_print("%s: init error %s\n", __FUNCTION__, Pa_GetErrorText(err));
    return;
  }

  numDevices = Pa_GetDeviceCount();

  if ( numDevices < 0 ) { return; }

  n_input_devices = 0;
  n_output_devices = 0;

  for (int  i = 0; i < numDevices; i++ ) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo( i );
    inputParameters.device = i;
    inputParameters.channelCount = 1;  // Microphone samples are mono
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
    inputParameters.hostApiSpecificStreamInfo = NULL;

    if (Pa_IsFormatSupported(&inputParameters, NULL, 48000.0) == paFormatIsSupported) {
      if (n_input_devices < MAX_AUDIO_DEVICES) {
        //
        // probably not necessary with portaudio, but to be on the safe side,
        // we copy the device name to local storage. This is referenced both
        // by the name and description element.
        //
        input_devices[n_input_devices].name = g_strdup(deviceInfo->name);
        input_devices[n_input_devices].description = g_strdup(deviceInfo->name);
        input_devices[n_input_devices].index = i;
        n_input_devices++;
      }

      t_print("%s: INPUT DEVICE, No=%d, Name=%s\n", __FUNCTION__, i, deviceInfo->name);
    }

    outputParameters.device = i;
    outputParameters.channelCount = 2;  // audio output samples are stereo
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
    outputParameters.hostApiSpecificStreamInfo = NULL;

    if (Pa_IsFormatSupported(NULL, &outputParameters, 48000.0) == paFormatIsSupported) {
      if (n_output_devices < MAX_AUDIO_DEVICES) {
        output_devices[n_output_devices].name = g_strdup(deviceInfo->name);
        output_devices[n_output_devices].description = g_strdup(deviceInfo->name);
        output_devices[n_output_devices].index = i;
        n_output_devices++;
      }

      t_print("%s: OUTPUT DEVICE, No=%d, Name=%s\n", __FUNCTION__, i, deviceInfo->name);
    }
  }
}

//
// AUDIO_OPEN_INPUT
//
// open a PA stream that connects to the TX audio input
// The PA callback function then sends the data to the transmitter
//

static int pa_in_cb(const void*, void*, unsigned long, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void*);
static int pa_out_cb(const void*, void*, unsigned long, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void*);

int audio_open_input(TRANSMITTER *tx) {
  PaError err;
  PaStreamParameters inputParameters;
  int i;
  int padev;
  //
  // Look up device name and determine device ID
  //
  padev = -1;

  for (i = 0; i < n_input_devices; i++) {
    if (!strcmp(tx->audio_name, input_devices[i].name)) {
      padev = input_devices[i].index;
      break;
    }
  }

  t_print("%s: TX:%s (dev=%d)\n", __FUNCTION__, tx->audio_name, padev);

  //
  // Device name possibly came from props file and device is no longer there
  //
  if (padev < 0) {
    return -1;
  }

  g_mutex_lock(&tx->audio_mutex);
  bzero(&inputParameters, sizeof(inputParameters)); //not necessary if you are filling in all the fields
  inputParameters.channelCount = 1;   // MONO
  inputParameters.device = padev;
  inputParameters.hostApiSpecificStreamInfo = NULL;
  inputParameters.sampleFormat = paFloat32;
  inputParameters.suggestedLatency = Pa_GetDeviceInfo(padev)->defaultLowInputLatency ;
  inputParameters.hostApiSpecificStreamInfo = NULL; //See you specific host's API docs for info on using this field
  err = Pa_OpenStream(&tx->audio_handle, &inputParameters, NULL, 48000.0, MY_AUDIO_BUFFER_SIZE,
                      paNoFlag, pa_in_cb, tx);

  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __FUNCTION__, Pa_GetErrorText(err));
    tx->audio_handle = NULL;
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  tx->audio_buffer = (float *) g_new(float, MY_RING_BUFFER_SIZE);
  tx->audio_buffer_outpt = tx->audio_buffer_inpt = 0;

  if (tx->audio_buffer == NULL) {
    Pa_CloseStream(tx->audio_handle);
    tx->audio_handle = NULL;
    t_print("%s: alloc buffer failed.\n", __FUNCTION__);
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  err = Pa_StartStream(tx->audio_handle);

  if (err != paNoError) {
    t_print("%s: start stream error %s\n", __FUNCTION__, Pa_GetErrorText(err));
    Pa_CloseStream(tx->audio_handle);
    tx->audio_handle = NULL;
    g_free(tx->audio_buffer);
    tx->audio_buffer = NULL;
    g_mutex_unlock(&tx->audio_mutex);
    return -1;
  }

  //
  // Finished!
  //
  g_mutex_unlock(&tx->audio_mutex);
  return 0;
}

//
// PortAudio call-back function for Audio output
//
static int pa_out_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                     const PaStreamCallbackTimeInfo* timeInfo,
                     PaStreamCallbackFlags statusFlags,
                     void *userdata) {
  float *out = (float *)outputBuffer;
  RECEIVER *rx = (RECEIVER *)userdata;

  if (out == NULL) {
    t_print("%s: bogus audio buffer in callback\n", __FUNCTION__);
    return paContinue;
  }

  g_mutex_lock(&rx->audio_mutex);

  if (rx->audio_buffer != NULL) {
    //
    // Mutex protection: if the buffer is non-NULL it cannot vanish
    // util callback is completed
    //
    int newpt = rx->audio_buffer_outpt;

    for (unsigned int i = 0; i < framesPerBuffer; i++) {
      if (rx->audio_buffer_inpt == newpt) {
        // Ring buffer empty, send zero sample
        *out++ = 0.0;
        *out++ = 0.0;
      } else {
        *out++ = rx->audio_buffer[2 * newpt];
        *out++ = rx->audio_buffer[2 * newpt + 1];
        newpt++;

        if (newpt >= MY_RING_BUFFER_SIZE) { newpt = 0; }

        MEMORY_BARRIER;
        rx->audio_buffer_outpt = newpt;
      }
    }
  }

  g_mutex_unlock(&rx->audio_mutex);
  return paContinue;
}

//
// PortAudio call-back function for Audio input
//
static int pa_in_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void *userdata) {
  const float *in = (float *)inputBuffer;
  TRANSMITTER *tx = (TRANSMITTER *)userdata;

  if (in == NULL) {
    // This should not happen, so we do not send silence etc.
    t_print("%s: bogus audio buffer in callback\n", __FUNCTION__);
    return paContinue;
  }

  //
  // If we are a client, simply collect and transfer data
  // to the server without any buffering
  //
  if (radio_is_remote) {
    for (unsigned int i = 0; i < framesPerBuffer; i++) {
      short sample = in[i] * 32768.0;
      server_tx_audio(sample);
    }

    return paContinue;
  }

  g_mutex_lock(&tx->audio_mutex);

  if (tx->audio_buffer != NULL) {
    //
    // mutex protected: ring buffer cannot vanish
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
    // tx->audio_flag is used to "detect" the TX/RX transition.
    //
    //
    if (!radio_is_transmitting()) {
      if (tx->audio_flag) {
        tx->audio_flag = 0;
        tx->audio_buffer_outpt = 0;
        tx->audio_buffer_inpt  = 960;
        bzero(tx->audio_buffer, 960 * sizeof(float));
      }
    } else {
      tx->audio_flag = 1;
    }

    for (unsigned int i = 0; i < framesPerBuffer; i++) {
      //
      // put sample into ring buffer
      //
      int newpt = tx->audio_buffer_inpt + 1;

      if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }

      if (newpt != tx->audio_buffer_outpt) {
        MEMORY_BARRIER;
        // buffer space available, do the write
        tx->audio_buffer[tx->audio_buffer_inpt] = in[i];
        MEMORY_BARRIER;
        // atomic update of tx->audio_buffer_inpt
        tx->audio_buffer_inpt = newpt;
      }
    }
  }

  g_mutex_unlock(&tx->audio_mutex);
  return paContinue;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample(TRANSMITTER *tx) {
  float sample;
  g_mutex_lock(&tx->audio_mutex);

  //
  // mutex protected (for every single sample!):
  // ring buffer cannot vanish while being processed here
  //
  if ((tx->audio_buffer == NULL) || (tx->audio_buffer_outpt == tx->audio_buffer_inpt)) {
    // no buffer, or nothing in buffer: insert silence
    sample = 0.0;
  } else {
    int newpt = tx->audio_buffer_outpt + 1;

    if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }

    MEMORY_BARRIER;
    sample = tx->audio_buffer[tx->audio_buffer_outpt];
    // atomic update of read pointer
    MEMORY_BARRIER;
    tx->audio_buffer_outpt = newpt;
  }

  g_mutex_unlock(&tx->audio_mutex);
  return sample;
}

//
// AUDIO_OPEN_OUTPUT
//
// open a PA stream for data from one of the RX
//
int audio_open_output(RECEIVER *rx) {
  PaError err;
  PaStreamParameters outputParameters;
  int padev;
  int i;
  //
  // Look up device name and determine device ID
  //
  padev = -1;

  for (i = 0; i < n_output_devices; i++) {
    if (!strcmp(rx->audio_name, output_devices[i].name)) {
      padev = output_devices[i].index;
      break;
    }
  }

  t_print("%s: RX%d:%s (dev=%d)\n", __FUNCTION__, rx->id + 1, rx->audio_name, padev);

  //
  // Device name possibly came from props file and device is no longer there
  //
  if (padev < 0) {
    return -1;
  }

  g_mutex_lock(&rx->audio_mutex);
  bzero(&outputParameters, sizeof(outputParameters)); //not necessary if you are filling in all the fields
  outputParameters.channelCount = 2;   // audio output is stereo
  outputParameters.device = padev;
  outputParameters.hostApiSpecificStreamInfo = NULL;
  outputParameters.sampleFormat = paFloat32;
  // use a zero for the latency to get the minimum value
  outputParameters.suggestedLatency = 0.0; //Pa_GetDeviceInfo(padev)->defaultLowOutputLatency ;
  outputParameters.hostApiSpecificStreamInfo = NULL; //See you specific host's API docs for info on using this field
  err = Pa_OpenStream(&(rx->audio_handle), NULL, &outputParameters, 48000.0, MY_AUDIO_BUFFER_SIZE,
                      paNoFlag, pa_out_cb, rx);

  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __FUNCTION__, Pa_GetErrorText(err));
    rx->audio_handle = NULL;
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  //
  // This is now a ring buffer much larger than a single audio buffer
  //
  rx->audio_buffer = g_new(float, 2 * MY_RING_BUFFER_SIZE);
  rx->audio_buffer_inpt = 0;
  rx->audio_buffer_outpt = 0;

  if (rx->audio_buffer == NULL) {
    t_print("%s: allocate buffer failed\n", __FUNCTION__);
    Pa_CloseStream(rx->audio_handle);
    rx->audio_handle = NULL;
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  err = Pa_StartStream(rx->audio_handle);

  if (err != paNoError) {
    t_print("%s: error starting stream:%s\n", __FUNCTION__, Pa_GetErrorText(err));
    Pa_CloseStream(rx->audio_handle);
    rx->audio_handle = NULL;
    g_free(rx->audio_buffer);
    rx->audio_buffer = NULL;
    g_mutex_unlock(&rx->audio_mutex);
    return -1;
  }

  rx->cwaudio = 0;
  rx->cwcount = 0;
  //
  // Finished!
  //
  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

//
// AUDIO_CLOSE_INPUT
//
// close a TX audio stream
//
void audio_close_input(TRANSMITTER *tx) {
  t_print("%s: TX:%s\n", __FUNCTION__, tx->audio_name);
  g_mutex_lock(&tx->audio_mutex);

  if (tx->audio_handle != NULL) {
    PaError err = Pa_StopStream(tx->audio_handle);

    if (err != paNoError) {
      t_print("%s: error stopping stream: %s\n", __FUNCTION__, Pa_GetErrorText(err));
    }

    err = Pa_CloseStream(tx->audio_handle);

    if (err != paNoError) {
      t_print("%s: %s\n", __FUNCTION__, Pa_GetErrorText(err));
    }

    tx->audio_handle = NULL;
  }

  if (tx->audio_buffer != NULL) {
    g_free(tx->audio_buffer);
    tx->audio_buffer = NULL;
  }

  g_mutex_unlock(&tx->audio_mutex);
}

//
// AUDIO_CLOSE_OUTPUT
//
// shut down the stream connected with audio from one of the RX
//
void audio_close_output(RECEIVER *rx) {
  t_print("%s: RX%d:%s\n", __FUNCTION__, rx->id + 1, rx->audio_name);
  g_mutex_lock(&rx->audio_mutex);

  if (rx->audio_buffer != NULL) {
    g_free(rx->audio_buffer);
    rx->audio_buffer = NULL;
  }

  if (rx->audio_handle != NULL) {
    PaError err = Pa_StopStream(rx->audio_handle);

    if (err != paNoError) {
      t_print("%s: stop stream error %s\n", __FUNCTION__, Pa_GetErrorText(err));
    }

    err = Pa_CloseStream(rx->audio_handle);

    if (err != paNoError) {
      t_print("%s: close stream error %s\n", __FUNCTION__, Pa_GetErrorText(err));
    }

    rx->audio_handle = NULL;
  }

  g_mutex_unlock(&rx->audio_mutex);
}

//
// AUDIO_WRITE
//
// send RX audio data to a PA output stream
// we have to store the data such that the PA callback function
// can access it.
//
// Note that the check on radio_is_transmitting() takes care that "blocking"
// by the mutex can only occur in the moment of a RX/TX transition if
// both audio_write() and tx_audio_write() get a "go".
//
// So mutex locking/unlocking should only cost few CPU cycles in
// normal operation.
//
int audio_write (RECEIVER *rx, float left, float right) {
  float *buffer = rx->audio_buffer;

  //
  // If transmitting without duplex, quickly return
  //
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return 0; }

  g_mutex_lock(&rx->audio_mutex);
  rx->cwaudio = 0;

  if (rx->audio_handle != NULL && buffer != NULL) {
    int avail = rx->audio_buffer_inpt - rx->audio_buffer_outpt;

    if (avail < 0) { avail += MY_RING_BUFFER_SIZE; }

    if (avail <  MY_RING_LOW_WATER) {
      //
      // Running the RX-audio for a very long time
      // and with audio hardware whose "48000 Hz" are a little faster than the "48000 Hz" of
      // the SDR will very slowly drain the buffer. We recover from this by brutally
      // inserting half a buffer's length of silence.
      //
      // This is not always an "error" to be reported and necessarily happens in three cases:
      //  a) we come here for the first time
      //  b) we come from a TX/RX transition in non-CW mode, and no duplex
      //  c) we come from a TX/RX transition in CW mode
      //
      // In case a) and b) the buffer will be empty, in c) the buffer will contain "few" samples
      // because of the "CW audio low latency" strategy.
      //
      int oldpt = rx->audio_buffer_inpt;

      for (int i = 0; i < MY_RING_BUFFER_SIZE / 2 - avail; i++) {
        buffer[2 * oldpt] = 0.0;
        buffer[2 * oldpt + 1] = 0.0;
        oldpt++;

        if (oldpt >= MY_RING_BUFFER_SIZE) { oldpt = 0; }
      }

      MEMORY_BARRIER;
      rx->audio_buffer_inpt = oldpt;
      //t_print("%s: buffer was nearly empty, inserted silence.\n", __FUNCTION__);
    }

    if (avail > MY_RING_HIGH_WATER) {
      //
      // Running the RX-audio for a very long time
      // and with audio hardware whose "48000 Hz" are a little slower than the "48000 Hz" of
      // the SDR will very slowly fill the buffer. This should be the only situation where
      // this "buffer overrun" condition should occur. We recover from this by brutally
      // deleting half a buffer size of audio, such that the next overrun is in the distant
      // future.
      //
      int oldpt = rx->audio_buffer_inpt - avail + MY_RING_BUFFER_SIZE / 2;

      if (oldpt < 0) { oldpt += MY_RING_BUFFER_SIZE; }

      rx->audio_buffer_inpt = oldpt;
      //t_print("%s: buffer was nearly full, deleted audio\n", __FUNCTION__);
    }

    //
    // put sample into ring buffer
    //
    int oldpt = rx->audio_buffer_inpt;
    int newpt = oldpt + 1;

    if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }

    if (newpt != rx->audio_buffer_outpt) {
      //
      // buffer space available
      //
      MEMORY_BARRIER;
      buffer[2 * oldpt] = left;
      buffer[2 * oldpt + 1] = right;
      MEMORY_BARRIER;
      rx->audio_buffer_inpt = newpt;
    }
  }

  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

//
// Since the main use of tx_audio_write() is to produce a CW side tone,
// do active latency (buffer filling) management:
// During CW, between the elements the side tone contain "true" silence.
// We detect a sequence of 16 subsequent zero samples, and insert or delete
// a zero sample depending on the buffer water mark:
// If there are more than two portaudio buffers available, delete one sample,
// if it drops down to less than one portaudio buffer, insert one sample
//
int tx_audio_write(RECEIVER *rx, float sample) {
  g_mutex_lock(&rx->audio_mutex);

  if (rx->audio_handle != NULL && rx->audio_buffer != NULL) {
    int oldpt, newpt;
    int avail = rx->audio_buffer_inpt - rx->audio_buffer_outpt;
    int adjust = 0;

    if (avail < 0) { avail += MY_RING_BUFFER_SIZE; }

    if (rx->cwaudio == 0) {
      //
      // First time producing CW audio after RX/TX transition:
      // keep the oldest samples (until MY_CW_LOW_WATER) in the audio buffer
      // and discard the rest. Apply a down-slew on the samples
      // kept.
      //
      double damp = 1.000;
      newpt = rx->audio_buffer_outpt;
      for (int i = 0; i < MY_CW_LOW_WATER; i++) {
        if (i >= avail) {
          rx->audio_buffer[2 * newpt] = 0;
          rx->audio_buffer[2 * newpt + 1] = 0;
        } else {
          rx->audio_buffer[2 * newpt] *= damp;
          rx->audio_buffer[2 * newpt + 1] *= damp;
          damp = damp * 0.975;
        }
        newpt++;

        MEMORY_BARRIER;
        if (newpt >= MY_RING_BUFFER_SIZE) { newpt = 0; }
      }
      rx->audio_buffer_inpt = newpt;
      MEMORY_BARRIER;
      avail = MY_CW_LOW_WATER;
      rx->cwcount = 0;
      rx->cwaudio = 1;
    }

    if (sample != 0.0) { rx->cwcount = 0; }

    if (++rx->cwcount >= 16) {
      rx->cwcount = 0;

      //
      // We arrive here if we have seen 16 zero samples in a row.
      //
      if (avail > MY_CW_HIGH_WATER) { adjust = 2; } // full: we are above high water mark

      if (avail < MY_CW_LOW_WATER ) { adjust = 1; } // low: we are below low water mark
    }

    switch (adjust) {
    case 0:
      //
      // default case:
      //               put sample into ring buffer.
      //               since the side tone is mono put it into
      //               both the left and right channel with the
      //               same phase.
      //
      oldpt = rx->audio_buffer_inpt;
      newpt = oldpt + 1;

      if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }

      if (newpt != rx->audio_buffer_outpt) {
        //
        // buffer space available
        //
        MEMORY_BARRIER;
        rx->audio_buffer[2 * oldpt] = sample;
        rx->audio_buffer[2 * oldpt + 1] = sample;
        MEMORY_BARRIER;
        rx->audio_buffer_inpt = newpt;
      }

      break;

    case 1:
      //
      // we just saw 16 samples of silence and buffer filling is low:
      // insert one extra silence sample
      //
      oldpt = rx->audio_buffer_inpt;
      rx->audio_buffer[2 * oldpt] = 0.0;
      rx->audio_buffer[2 * oldpt + 1] = 0.0;
      oldpt++;

      if (oldpt == MY_RING_BUFFER_SIZE) { oldpt = 0; }

      rx->audio_buffer[2 * oldpt] = 0.0;
      rx->audio_buffer[2 * oldpt + 1] = 0.0;
      oldpt++;

      if (oldpt == MY_RING_BUFFER_SIZE) { oldpt = 0; }

      MEMORY_BARRIER;
      rx->audio_buffer_inpt = oldpt;
      break;

    case 2:
      //
      // we just saw 16 samples of silence and buffer filling is high:
      // just skip the current "silent" sample, that is, do nothing.
      //
      break;
    }
  }

  g_mutex_unlock(&rx->audio_mutex);
  return 0;
}

#endif
