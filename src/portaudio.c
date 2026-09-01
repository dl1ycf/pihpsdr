/* Copyright (C)
*  2019 - Christoph van Wüllen, DL1YCF
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

#include "atomic.h"
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
// We choose a ring buffer of RING_BUFFER_SIZE (stereo) samples that is kept
// about half-full during RX (target latency: about 0.1 sec) which should be more
// than enough.
// If the buffer falls below AUDIO_LAT_LOW, "silence" is inserted until the
// buffer is again half full. This usually only happens after TX/RX transitions
//
// During TX (no duplex), tx_audio_write() is called. If it is called for
// the first time after a RX/TX transition, most of the pending contents in
// the ring buffer is cleared and only CW_LAT_TARGET (stereo) samples
// are kept. This is probably the minimum amount necessary to avoid
// audio underruns which manifest themselves as ugly cracks in the side ton.
// During the TX phase, each time 16 subsequent zero samples are detected,
// one of these is deleted or an additional one inserted to keep the output
// buffer filling at about CW_LAT_TARGET. This is (only)
// important for the CW side tone and here we have "real silence" between the
// dots/dashes. This keeps the CW sidetone latency near the target value.
//
// If we then go to RX again a "low water mark" condition is detected in the
// first call to audio_write() and half a buffer length of silence is inserted
// again.
// Of course, a small portaudio audio buffer size (128 samples) helps
// keeping the latency small. With the CW low/high water marks of 192/320
// I have achieved a latency of about 15 msec on a Macintosh.
// One can go smaller but this increases the probabilities of audio cracks
// from buffer underruns.
//
//

#define RING_BUFFER_SIZE      16384
#define RING_BUFFER_MASK      16383
#define ST_BUFFER_SIZE         2048
#define ST_BUFFER_MASK         2047
#define MIC_BUFFER_SIZE        8192
#define MIC_BUFFER_MASK        8191

#define AUDIO_LAT_LOW           512
#define AUDIO_LAT_TARGET       8192
#define AUDIO_LAT_HIGH        15872
#define CW_LAT_LOW              320
#define CW_LAT_TARGET           384
#define CW_LAT_HIGH             448

struct audio_data_ {
  double *audio_buffer;                    // ring buffer for main audio
  double *st_buffer;                       // ring buffer for side tone (RX audio only)
  volatile atomic_int audio_buffer_inpt;   // pointer for audio ring buffer
  volatile atomic_int audio_buffer_outpt;
  volatile atomic_int st_buffer_inpt;      // pointer for side tone ring buffer
  volatile atomic_int st_buffer_outpt;
  int audio_channels;                      // 1 for MONO, 2 for STEREO
  double audiodamp;                        // attenuation for blending
  int audio_flag;                          // flag to detect RX/TX transisions in audio in
  int cwaudio;                             // state flag used in RX/TX transitions for audio out
  int cwcount;                             // counter for "silence" in side tone
  PaStream *pastream;                      // PortAudio stream
};

typedef struct audio_data_ audio_data;
//
// AUDIO_GET_CARDS
//
// This inits PortAudio and looks for suitable input and output channels
//
void audio_get_cards(void) {
  int numDevices;
  PaStreamParameters inputParameters, outputParameters;
  PaError err;
  err = Pa_Initialize();
  if ( err != paNoError ) {
    t_print("%s: init error %s\n", __func__, Pa_GetErrorText(err));
    return;
  }
  numDevices = Pa_GetDeviceCount();
  if ( numDevices < 0 ) { return; }
  n_input_devices = 0;
  n_output_devices = 0;
  for (int  i = 0; i < numDevices; i++ ) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo( i );
    if (deviceInfo->maxInputChannels > 0 && n_input_devices < MAX_AUDIO_DEVICES) {
      inputParameters.device = i;
      inputParameters.channelCount = 1;
      inputParameters.sampleFormat = paFloat32;
      inputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
      inputParameters.hostApiSpecificStreamInfo = NULL;
      if (Pa_IsFormatSupported(&inputParameters, NULL, 48000.0) == paFormatIsSupported) {
        //
        // probably not necessary with portaudio, but to be on the safe side,
        // we copy the device name to local storage. This is referenced both
        // by the name and description element.
        //
        input_devices[n_input_devices].name = g_strdup(deviceInfo->name);
        input_devices[n_input_devices].description = g_strdup(deviceInfo->name);
        input_devices[n_input_devices].PortAudioDevice = i;
        input_devices[n_input_devices].channels = 1;
        n_input_devices++;
      }
    }
    if (deviceInfo->maxOutputChannels > 0 && n_output_devices < MAX_AUDIO_DEVICES) {
      outputParameters.device = i;
      outputParameters.channelCount = (deviceInfo->maxOutputChannels > 1) ? 2 : 1;
      outputParameters.sampleFormat = paFloat32;
      outputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
      outputParameters.hostApiSpecificStreamInfo = NULL;
      if (Pa_IsFormatSupported(NULL, &outputParameters, 48000.0) == paFormatIsSupported) {
        output_devices[n_output_devices].name = g_strdup(deviceInfo->name);
        output_devices[n_output_devices].description = g_strdup(deviceInfo->name);
        output_devices[n_output_devices].PortAudioDevice = i;
        output_devices[n_output_devices].channels = outputParameters.channelCount;
        n_output_devices++;
      }
    }
  }
  // log all devices to output
  for (int i = 0; i < n_input_devices; i++) {
    t_print("Audio  Input: No=%d C=%d Name=%s\n", i, input_devices[i].channels, input_devices[i].name);
  }
  for (int i = 0; i < n_output_devices; i++) {
    t_print("Audio Output: No=%d C=%d Name=%s\n", i, output_devices[i].channels, output_devices[i].name);
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
      t_print("%s TX:%s\n", __func__, input_devices[i].description);
      padev = input_devices[i].PortAudioDevice;
      break;
    }
  }
  //
  // Device name not registered upon startup
  //
  if (padev < 0) {
    t_print("%s: not registered: %s\n", __func__, tx->audio_name);
    return -1;
  }

  if (tx->audio_handle != NULL) {
    tx->audio_handle = NULL;
    usleep (50000);
  }

  //
  // Construct audio_data structure. Only when everything succeeds,
  // this pointer is put into the tx data structure
  //
  audio_data *ad = g_new(audio_data, 1);

  if (ad == NULL) {
    t_print("%s: alloc ad error\n", __func__);
    return -1;
  }

  bzero(&inputParameters, sizeof(inputParameters)); //not necessary if you are filling in all the fields
  inputParameters.channelCount = 1;   // MONO
  inputParameters.device = padev;
  inputParameters.hostApiSpecificStreamInfo = NULL;
  inputParameters.sampleFormat = paFloat32;
  inputParameters.suggestedLatency = Pa_GetDeviceInfo(padev)->defaultLowInputLatency ;
  inputParameters.hostApiSpecificStreamInfo = NULL; //See you specific host's API docs for info on using this field
  err = Pa_OpenStream(&ad->pastream, &inputParameters, NULL, 48000.0, 128, paNoFlag, pa_in_cb, tx);
  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __func__, Pa_GetErrorText(err));
    g_free(ad);
    return -1;
  }
  ad->audio_buffer = g_new(double, MIC_BUFFER_SIZE);
  ad->audio_buffer_outpt = ad->audio_buffer_inpt = 0;
  if (ad->audio_buffer == NULL) {
    Pa_CloseStream(ad->pastream);
    g_free(ad);
    t_print("%s: alloc buffer failed.\n", __func__);
    return -1;
  }
  err = Pa_StartStream(ad->pastream);
  if (err != paNoError) {
    t_print("%s: start stream error %s\n", __func__, Pa_GetErrorText(err));
    Pa_CloseStream(ad->pastream);
    g_free(ad->audio_buffer);
    g_free(ad);
    return -1;
  }
  //
  // Finished!
  //
  ad->audio_flag = 1;
  tx->audio_handle = (void *) ad;
  return 0;
}

//
// PortAudio call-back function for Audio output
//
static int pa_out_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                     const PaStreamCallbackTimeInfo* timeInfo,
                     PaStreamCallbackFlags statusFlags,
                     void *userdata) {
  // Assume paFloat32 is represented as "float" in C
  float *out = (float *)outputBuffer;
  if (out == NULL) {
    t_print("%s: bogus audio buffer in callback\n", __func__);
    return paContinue;
  }
  RECEIVER *rx = (RECEIVER *)userdata;
  if (rx == NULL) { return paComplete; }  // This should not happen
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) {
    //
    // This means an audio_close_output() is on the way.
    // deliver zeroes with paContinue and let the stream
    // be stopped elsewhere.
    //
    if (ad->audio_channels == 1) {
      memset(out, 0, framesPerBuffer * sizeof(float));
    } else {
      memset(out, 0, 2 * framesPerBuffer * sizeof(float));
    }
    return paContinue;
  }
  //
  // Since the handle was non-NULL, the existence of all buffers/pointer
  // is guaranteed for 50 milliseconds, more than enough time to copy
  // data from the buffer to "out"
  //
  if (ad->audio_channels == 1) {
    // MONO code
    for (unsigned int i = 0; i < framesPerBuffer; i++) {
      int oldpt;
      double rx_left = 0.0;
      double st_sample = 0.0;
      oldpt = ad->audio_buffer_outpt;
      if (oldpt != ad->audio_buffer_inpt) {
        rx_left = ad->audio_buffer[oldpt];
        if (ad->cwaudio == 3) {
          rx_left *= ad->audiodamp;
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
      *out++ =  (float)(rx_left + st_sample);
    }
  } else {
    // STEREO code
    for (unsigned int i = 0; i < framesPerBuffer; i++) {
      int oldpt;
      double rx_left = 0.0;
      double rx_right = 0.0;
      double st_sample = 0.0;
      oldpt = ad->audio_buffer_outpt;
      if (oldpt != ad->audio_buffer_inpt) {
        rx_left = ad->audio_buffer[2 * oldpt];
        rx_right = ad->audio_buffer[2 * oldpt + 1];
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
      *out++ =  (float)(rx_left + st_sample);
      *out++ =  (float)(rx_right + st_sample);
    }
  }
  return paContinue;
}

//
// PortAudio call-back function for Audio input
//
static int pa_in_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void *userdata) {
  // Assume paFloat32 is represented as "float" in C
  const float *in = (float *)inputBuffer;
  if (in == NULL) {
    // This should not happen, so we do not send silence etc.
    t_print("%s: bogus audio buffer in callback\n", __func__);
    return paContinue;
  }
  TRANSMITTER *tx = (TRANSMITTER *)userdata;
  if (tx == NULL) { return paComplete; }  // This should not happen
  audio_data *ad = (audio_data *) tx->audio_handle;
  if (ad == NULL) {
    // This usually means we are in audio_close_input(), so we
    // just skip the incoming samples. We return paContinue to let
    // the stream be stopped from elsewhere
    return paContinue;
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
  for (unsigned int i = 0; i < framesPerBuffer; i++) {
    //
    // put sample into ring buffer
    //
    int newpt = (ad->audio_buffer_inpt + 1) & MIC_BUFFER_MASK;
    if (newpt != ad->audio_buffer_outpt) {
      // buffer space available, do the write
      ad->audio_buffer[ad->audio_buffer_inpt] = in[i];
      MEMORY_BARRIER;
      // atomic update of ad->audio_buffer_inpt
      ad->audio_buffer_inpt = newpt;
    }
  }
  return paContinue;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
double audio_get_next_mic_sample(TRANSMITTER *tx) {
  if (tx == NULL) { return 0.0; }
  audio_data *ad = (audio_data *) tx->audio_handle;
  if (ad == NULL) { return 0.0; }
  //
  // Now the existence of the buffers is guaranteed for 50 msec
  //
  int newpt = (ad->audio_buffer_outpt + 1) & MIC_BUFFER_MASK;
  double sample = ad->audio_buffer[ad->audio_buffer_outpt];
  MEMORY_BARRIER;
  ad->audio_buffer_outpt = newpt;
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
  int channels;
  int i;
  //
  // Look up device name and determine device ID
  //
  channels = 0;
  padev = -1;
  for (i = 0; i < n_output_devices; i++) {
    if (!strcmp(rx->audio_name, output_devices[i].name)) {
      padev = output_devices[i].PortAudioDevice;
      channels = output_devices[i].channels;
      t_print("%s RX%d:%s (C=%d)\n", __func__, rx->id + 1, output_devices[i].description, channels);
      break;
    }
  }
  //
  // Device name not registered upon startup
  //
  if (padev < 0) {
    t_print("%s: not registered: %s\n", __func__, rx->audio_name);
    return -1;
  }
  if (rx == NULL) { return -1;}
  if (rx->audio_handle != NULL) {
    // we should not come here, so it may be a bogus pointer
    // therefore no clean-up
    rx->audio_handle = NULL;
    usleep(50000);
  }
  audio_data *ad = g_new(audio_data, 1);
  bzero(&outputParameters, sizeof(outputParameters)); //not necessary if you are filling in all the fields
  outputParameters.device = padev;
  outputParameters.hostApiSpecificStreamInfo = NULL;
  outputParameters.sampleFormat = paFloat32;
  // use a zero for the latency to get the minimum value
  outputParameters.suggestedLatency = 0.0; //Pa_GetDeviceInfo(padev)->defaultLowOutputLatency ;
  outputParameters.hostApiSpecificStreamInfo = NULL; //See you specific host's API docs for info on using this field
  outputParameters.channelCount = channels;
  ad->audio_channels = channels;
  err = Pa_OpenStream(&(ad->pastream), NULL, &outputParameters, 48000.0, 128, paNoFlag, pa_out_cb, rx);
  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __func__, Pa_GetErrorText(err));
    g_free(ad);
    return -1;
  }
  //
  // This is now a ring buffer much larger than a single audio buffer
  //
  ad->audio_buffer = g_new(double, ad->audio_channels * RING_BUFFER_SIZE);
  ad->st_buffer = g_new(double, ST_BUFFER_SIZE);
  ad->audio_buffer_inpt = 0;
  ad->audio_buffer_outpt = 0;
  ad->st_buffer_inpt = 0;
  ad->st_buffer_outpt = 0;
  if (ad->audio_buffer == NULL || ad->st_buffer == NULL) {
    t_print("%s: allocate buffer failed\n", __func__);
    Pa_CloseStream(ad->pastream);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  err = Pa_StartStream(ad->pastream);
  if (err != paNoError) {
    t_print("%s: error starting stream:%s\n", __func__, Pa_GetErrorText(err));
    Pa_CloseStream(ad->pastream);
    g_free(ad->audio_buffer);
    g_free(ad->st_buffer);
    g_free(ad);
    return -1;
  }
  ad->cwaudio = 0;
  ad->cwcount = 0;
  //
  // Finished!
  //
  rx->audio_handle = ad;
  return 0;
}

//
// AUDIO_CLOSE_INPUT
//
// close a TX audio stream
//
void audio_close_input(TRANSMITTER *tx) {
  if (tx == NULL) { return; }
  t_print("%s: TX:%s\n", __func__, tx->audio_name);
  audio_data *ad = (audio_data *) tx->audio_handle;
  if (ad == NULL) { return; }
  tx->audio_handle = NULL;
  PaError err = Pa_StopStream(ad->pastream);
  if (err != paNoError) {
    t_print("%s: error stopping stream: %s\n", __func__, Pa_GetErrorText(err));
  }
  err = Pa_CloseStream(ad->pastream);
  if (err != paNoError) {
    t_print("%s: %s\n", __func__, Pa_GetErrorText(err));
  }
  //
  // after setting audio_handle to NULL; wait at least 50 msec before
  // removing buffers
  //
  usleep(50000);
  g_free(ad->audio_buffer);
  g_free(ad);
}

//
// AUDIO_CLOSE_OUTPUT
//
// shut down the stream connected with audio from one of the RX
//
void audio_close_output(RECEIVER *rx) {
  if (rx == NULL) { return; }
  t_print("%s: RX%d:%s\n", __func__, rx->id + 1, rx->audio_name);
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) { return; }
  rx->audio_handle = NULL;
  PaError err = Pa_StopStream(ad->pastream);
  if (err != paNoError) {
    t_print("%s: stop stream error %s\n", __func__, Pa_GetErrorText(err));
  }
  err = Pa_CloseStream(ad->pastream);
  if (err != paNoError) {
    t_print("%s: close stream error %s\n", __func__, Pa_GetErrorText(err));
  }
  //
  // Wait at least 50 msec before freeing any buffers
  //
  usleep(50000);
  g_free(ad->audio_buffer);
  g_free(ad->st_buffer);
  g_free(ad);
}

//
// AUDIO_WRITE
//
// send RX audio data to a PA output stream
// we have to store the data such that the PA callback function
// can access it.
//
void audio_write (RECEIVER *rx, double left, double right) {
  //
  // If transmitting without duplex, quickly return
  //
  if (rx == NULL || rx->audio_handle == NULL) { return; }
  if (rx == active_receiver && radio_is_transmitting() && !duplex) { return; }
  //
  // The existence of the buffers is now guaranteed for 50 msec
  //
  audio_data *ad = (audio_data *)rx->audio_handle;
  double *buffer = ad->audio_buffer;
  ad->cwaudio = 0;
  int avail = (ad->audio_buffer_inpt - ad->audio_buffer_outpt) & RING_BUFFER_MASK;
  if (avail <  AUDIO_LAT_LOW) {
    //
    // Running the RX-audio for a very long time
    // and with audio hardware whose "48000 Hz" are a little faster than the "48000 Hz" of
    // the SDR will very slowly drain the buffer. We recover from this by brutally
    // inserting half a buffer's length of silence.
    //
    // This is not always an "error" to be reported and necessarily happens in three cases:
    //  a) we come here for the first time
    //  b) we come from a TX/RX transition without having a side tone
    //  c) we come from a TX/RX transition with a side tone or a TX monitor
    //
    // In case a) and b) the buffer will be empty, in c) the buffer will contain "few" samples
    // (about CW_LAT_TARGET)
    //
    int oldpt = ad->audio_buffer_inpt;
    if (ad->audio_channels == 1) {
      for (int i = 0; i < AUDIO_LAT_TARGET - avail; i++) {
        buffer[oldpt] = 0.0;
        oldpt = (oldpt + 1) & RING_BUFFER_MASK;
      }
    } else {
      for (int i = 0; i < AUDIO_LAT_TARGET - avail; i++) {
        buffer[2 * oldpt] = 0.0;
        buffer[2 * oldpt + 1] = 0.0;
        oldpt = (oldpt + 1) & RING_BUFFER_MASK;
      }
    }
    MEMORY_BARRIER;
    ad->audio_buffer_inpt = oldpt;
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
  }
  //
  // put sample into ring buffer
  //
  int oldpt = ad->audio_buffer_inpt;
  int newpt = (oldpt + 1) & RING_BUFFER_MASK;
  if (newpt != ad->audio_buffer_outpt) {
    //
    // buffer space available
    //
    if (ad->audio_channels == 1) {
      buffer[oldpt] = 0.5 * (left + right);
    } else {
      buffer[2 * oldpt] = left;
      buffer[2 * oldpt + 1] = right;
    }
    MEMORY_BARRIER;
    ad->audio_buffer_inpt = newpt;
  }
  return;
}

//
// Since the main use of tx_audio_write() is to produce a CW side tone,
// do active latency (buffer filling) management:
// During CW, between the elements the side tone contain "true" silence.
// We detect a sequence of 16 subsequent zero samples, and insert or delete
// a zero sample to keep the buffer filling at CW_LAT_TARGET.
//
void tx_audio_write(RECEIVER *rx, double sample) {
  if (rx == NULL) { return; }
  audio_data *ad = (audio_data *) rx->audio_handle;
  if (ad == NULL) { return; }
  //
  // The existence of the buffers is now guaranteed for 50 msec
  //
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
    //
    // put mono sample into ring buffer
    //
    newpt = (inpt + 1) & ST_BUFFER_MASK;
    if (newpt != ad->st_buffer_outpt) {
      // buffer space available
      ad->st_buffer[inpt] = sample;
      MEMORY_BARRIER;
      ad->st_buffer_inpt = newpt;
    }
    break;
  case 1:
    //
    // we just saw 16 samples of silence and buffer filling is low:
    // insert one extra mono sample
    //
    ad->st_buffer[inpt] = 0.0;
    inpt = (inpt + 1) & ST_BUFFER_MASK;
    ad->st_buffer[inpt] = 0.0;
    inpt = (inpt + 1) & ST_BUFFER_MASK;
    MEMORY_BARRIER;
    ad->st_buffer_inpt = inpt;
    break;
  default:
    //
    // we just saw 16 samples of silence and buffer filling is high:
    // just skip the current "silent" sample, that is, do nothing.
    //
    break;
  }
  return;
}

#endif
