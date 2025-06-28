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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>
#include <SoapySDR/Logger.h>

#include <wdsp.h>   // only needed for the resampler

#include "audio.h"
#include "band.h"
#include "channel.h"
#include "discovered.h"
#include "ext.h"
#include "filter.h"
#include "main.h"
#include "message.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "soapy_protocol.h"
#include "transmitter.h"
#include "vfo.h"

#define MAX_CHANNELS 2
static SoapySDRStream *rx_stream[MAX_CHANNELS];
static SoapySDRStream *tx_stream;
static SoapySDRDevice *soapy_device;
static int max_samples;

static GThread *receive_thread_id = NULL;
static gpointer receive_thread(gpointer data);
static gpointer receive_dual_thread(gpointer data);  // for LIME
static gpointer receive_dupli_thread(gpointer data); // for LIME

static gboolean running;

static int mic_samples = 0;
static int mic_sample_divisor = 1;

static int max_tx_samples;
static float *output_buffer;
static int output_buffer_index;

const double lime_rx_bw = 12000000.0;
// cppcheck-suppress unusedFunction
SoapySDRDevice *get_soapy_device() {
  return soapy_device;
}

void soapy_protocol_set_mic_sample_rate(int rate) {
  ASSERT_SERVER();
  mic_sample_divisor = rate / 48000;
}

void soapy_protocol_change_sample_rate(RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // rx->mutex already locked, so we can call this  only
  // if the radio is stopped -- we cannot change the resampler
  // while the receive thread is stuck in rx_add_iq_samples()
  //
#if 0
  //
  //  sample code to query, set, and query the sample rate
  //  (without using a resampler). However, going to lower
  //  sample rates should also involve going to smaller
  //  band widths, otherwise there will be aliases
  //
  int rc;
  double d;
  t_print("%s: setting samplerate=%f\n", __FUNCTION__, (double)rx->sample_rate);
  d = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc);
  d = SoapySDRDevice_getBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc, (double)rx->sample_rate);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, (double)rx->sample_rate,
            SoapySDR_errToStr(rc));
  }

  d = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc);
  d = SoapySDRDevice_getBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc);
#endif

  //
  // We stick to the hardware sample rate and use the WDSP resampler
  //
  if (rx->sample_rate == soapy_radio_sample_rate) {
    if (rx->resample_buffer != NULL) {
      g_free(rx->resample_buffer);
      rx->resample_buffer = NULL;
      rx->resample_buffer_size = 0;
    }

    if (rx->resampler != NULL) {
      destroy_resample(rx->resampler);
      rx->resampler = NULL;
    }
  } else {
    if (rx->resample_buffer != NULL) {
      g_free(rx->resample_buffer);
      rx->resample_buffer = NULL;
    }

    if (rx->resampler != NULL) {
      destroy_resample(rx->resampler);
      rx->resampler = NULL;
    }

    rx->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_buffer = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_samples, rx->buffer, rx->resample_buffer, soapy_radio_sample_rate,
                                     rx->sample_rate, 0.0, 0, 1.0);
  }
}

void soapy_protocol_create_receiver(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;
  mic_sample_divisor = rx->sample_rate / 48000;
  double bandwidth = (double) soapy_radio_sample_rate;
  t_print("%s: setting samplerate=%f adc=%d mic_sample_divisor=%d\n", __FUNCTION__,
          (double)soapy_radio_sample_rate,
          rx->adc, mic_sample_divisor);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    // LIME: this must be the bandwidth before decimation
    bandwidth = lime_rx_bw;
  }

  t_print("%s: adc=%d setting bandwidth=%f\n", __FUNCTION__, rx->adc, bandwidth);
  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc, bandwidth);

  //
  // A 2-MHz-Bandwidth was used as a constant value in previous versions,
  // we take this if setting the bandwidth failed
  //
  if (rc != 0) {
    bandwidth = 2000000.0;
    t_print("%s: adc=%d setting bandwidth=%f\n", __FUNCTION__, rx->adc, bandwidth);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc, bandwidth);
  }

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setBandwidth(%f) failed: %s\n", __FUNCTION__, (double)bandwidth, SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    // LIME: oversampling is required to shift frequency with NCO
    // Otherwise when LO freq moves, not only TX is tied to the same freq,
    // it should be calibrated on every LO move.
    t_print("Writing RX oversampling setting");
    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: SoapySDRDevice setting oversampling failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }

  size_t channel = rx->adc;
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  t_print("%s: SoapySDRDevice_setupStream(version<0x00080000): channel=%ld\n", __FUNCTION__, channel);
  rc = SoapySDRDevice_setupStream(soapy_device, &rx_stream[channel], SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup RX Stream Failed");
  }

#else
  t_print("%s: SoapySDRDevice_setupStream(version>=0x00080000): channel=%ld\n", __FUNCTION__, channel);
  rx_stream[channel] = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rx_stream[channel] == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed (rx_stream is NULL)\n", __FUNCTION__);
    g_idle_add(fatal_error, "FATAL: Soapy Setup RX Stream Failed");
  }

#endif
  max_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream[channel]);
  t_print("%s: max_samples=%d\n", __FUNCTION__, max_samples);

  if (max_samples > (2 * rx->fft_size)) {
    max_samples = 2 * rx->fft_size;
  }

  rx->buffer = g_new(double, max_samples * 2);

  if (rx->sample_rate == soapy_radio_sample_rate) {
    rx->resample_buffer = NULL;
    rx->resampler = NULL;
    rx->resample_buffer_size = 0;
  } else {
    rx->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_buffer = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_samples, rx->buffer, rx->resample_buffer, soapy_radio_sample_rate,
                                     rx->sample_rate, 0.0, 0, 1.0);
  }

  t_print("%s: max_samples=%d\n", __FUNCTION__, max_samples);
}

//
// Some LIME-specific functions, namely
//
// soapy_protocol_create_receivers(RECEIVER **rx);               // create two LIME receivers as a pair
// soapy_protocol_start_receivers(RECEIVER **rx);                // start the two LIME receivers
// receive_dual_thread(void *data);                              // RX thread for the two LIME receivers
// soapy_protocol_create_duplicate_receivers(RECEIVER **rx);     // create two LIME receivers as a synchronized pair
// soapy_protocol_start_duplicate_receivers(RECEIVER **rx);      // start the synchronized pair
// receive_dupli_thread(void *data);                             // RX thread for the two synchronized LIME receivers
//

void soapy_protocol_create_receivers(RECEIVER *rx[]) {
  if (strcmp(radio->name, "lime") != 0) {
    return;
  }

  int rc;
  rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

  if (rc != 0) {
    t_print("%s: SoapySDRDevice setting oversampling failed: %s\n", __FUNCTION__,
            SoapySDR_errToStr(rc));
  }

  for (int i = 0; i < RECEIVERS; i++) {
    mic_sample_divisor = rx[i]->sample_rate / 48000;
    t_print("%s: device=%p adc=%d setting bandwidth=%f\n", __FUNCTION__, soapy_device, rx[i]->adc, lime_rx_bw);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx[i]->adc, lime_rx_bw);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setBandwidth(%f) failed: %s\n", __FUNCTION__, (double)lime_rx_bw, SoapySDR_errToStr(rc));
    }

    t_print("Writing RX oversampling setting");
    t_print("%s: setting samplerate=%f device=%p adc=%d mic_sample_divisor=%d\n", __FUNCTION__,
            (double)soapy_radio_sample_rate,
            soapy_device, rx[i]->adc, mic_sample_divisor);
    rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx[i]->adc, (double)soapy_radio_sample_rate);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
              SoapySDR_errToStr(rc));
    }
  }

  //size_t channel = rx->adc;
  size_t *channels = malloc(RECEIVERS * sizeof(size_t));

  for (int i = 0; i < RECEIVERS; ++i) {
    channels[i] = i;
  }

  t_print("%s: SoapySDRDevice_setupStream(version>=0x00080000): RECEIVERS=%d\n", __FUNCTION__, RECEIVERS);
  rx_stream[0] = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, channels, RECEIVERS, NULL);

  if (rx_stream[0] == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed (rx_stream is NULL)\n", __FUNCTION__);
    g_idle_add(fatal_error, "Soapy Setup RX Stream Failed");
  }

  free(channels);

  for (int i = 0; i < RECEIVERS; ++i) {
    if (!rx[i]) { continue; }

    max_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream[0]);

    if (max_samples > (2 * rx[i]->fft_size)) {
      max_samples = 2 * rx[i]->fft_size;
    }

    rx[i]->buffer = g_new(double, max_samples * 2);

    if (rx[i]->sample_rate == soapy_radio_sample_rate) {
      rx[i]->resample_buffer = NULL;
      rx[i]->resampler = NULL;
      rx[i]->resample_buffer_size = 0;
    } else {
      rx[i]->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx[i]->sample_rate);
      rx[i]->resample_buffer = g_new(double, rx[i]->resample_buffer_size);
      rx[i]->resampler = create_resample(1, max_samples, rx[i]->buffer, rx[i]->resample_buffer, soapy_radio_sample_rate,
                                         rx[i]->sample_rate, 0.0, 0, 1.0
                                        );
    }

    t_print("%s: id=%d max_samples=%d buffer=%p\n", __FUNCTION__, rx[i]->id, max_samples, rx[i]->buffer);
  }
}

void soapy_protocol_create_duplicate_receivers(RECEIVER *rx[]) {
  if (strcmp(radio->name, "lime") != 0) {
    return;
  }

  int rc;
  rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

  if (rc != 0) {
    t_print("%s: SoapySDRDevice setting oversampling failed: %s\n", __FUNCTION__,
            SoapySDR_errToStr(rc));
  }

  for (int i = 0; i < RECEIVERS; i++) {
    int rc;
    mic_sample_divisor = rx[i]->sample_rate / 48000;
    t_print("%s: device=%p adc=%d setting bandwidth=%f\n", __FUNCTION__, soapy_device, rx[i]->adc, lime_rx_bw);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx[i]->adc, lime_rx_bw);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setBandwidth(%f) failed: %s\n", __FUNCTION__, lime_rx_bw, SoapySDR_errToStr(rc));
    }

    t_print("%s: setting samplerate=%f device=%p adc=%d mic_sample_divisor=%d\n", __FUNCTION__,
            (double)soapy_radio_sample_rate,
            soapy_device, rx[i]->adc, mic_sample_divisor);
    rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx[i]->adc, (double)soapy_radio_sample_rate);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
              SoapySDR_errToStr(rc));
    }
  }

  size_t channel = rx[0]->adc;
  t_print("%s: SoapySDRDevice_setupStream(version>=0x00080000): channel=%ld\n", __FUNCTION__, channel);
  rx_stream[channel] = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rx_stream[channel] == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed (rx_stream is NULL)\n", __FUNCTION__);
    g_idle_add(fatal_error, "Soapy Setup RX Stream Failed");
  }

  for (int i = 0; i < RECEIVERS; ++i) {
    if (!rx[i]) { continue; }

    max_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream[0]);

    if (max_samples > (2 * rx[i]->fft_size)) {
      max_samples = 2 * rx[i]->fft_size;
    }

    rx[i]->buffer = g_new(double, max_samples * 2);

    if (rx[i]->sample_rate == soapy_radio_sample_rate) {
      rx[i]->resample_buffer = NULL;
      rx[i]->resampler = NULL;
      rx[i]->resample_buffer_size = 0;
    } else {
      rx[i]->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx[i]->sample_rate);
      rx[i]->resample_buffer = g_new(double, rx[i]->resample_buffer_size);
      rx[i]->resampler = create_resample(1, max_samples, rx[i]->buffer, rx[i]->resample_buffer, soapy_radio_sample_rate,
                                         rx[i]->sample_rate, 0.0, 0, 1.0
                                        );
    }

    t_print("%s: id=%d max_samples=%d buffer=%p\n", __FUNCTION__, rx[i]->id, max_samples, rx[i]->buffer);
  }
}

void soapy_protocol_start_receivers(RECEIVER *rx[]) {
  if (strcmp(radio->name, "lime") != 0) {
    return;
  }

  int rc;
  t_print("%s: id1=%d, id2=%d soapy_device=%p rx_stream=%p\n", __FUNCTION__, rx[0]->id, rx[1]->id, soapy_device,
          rx_stream);
  double rate1 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx[0]->adc);
  double rate2 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx[1]->adc);
  t_print("%s: rate1=%f\n", __FUNCTION__, rate1);
  t_print("%s: rate1=%f\n", __FUNCTION__, rate2);
  t_print("%s: activate Stream\n", __FUNCTION__);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream[0], 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Start RX Stream failed");
  }

  t_print("%s: create receiver_thread\n", __FUNCTION__);
  receive_thread_id = g_thread_new( "soapy_rx", receive_dual_thread, rx);
  t_print("%s: receiver_thread_id=%p\n", __FUNCTION__, receive_thread_id);
}

void soapy_protocol_start_duplicate_receivers(RECEIVER *rx[]) {
  if (strcmp(radio->name, "lime") != 0) {
    return;
  }

  int rc;
  t_print("%s: id1=%d, id2=%d soapy_device=%p rx_stream=%p\n", __FUNCTION__, rx[0]->id, rx[1]->id, soapy_device,
          rx_stream);
  double rate1 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx[0]->adc);
  double rate2 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx[1]->adc);
  t_print("%s: rate1=%f\n", __FUNCTION__, rate1);
  t_print("%s: rate1=%f\n", __FUNCTION__, rate2);
  t_print("%s: activate Stream\n", __FUNCTION__);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream[0], 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Start RX Stream failed");
  }

  t_print("%s: create receiver_thread\n", __FUNCTION__);
  receive_thread_id = g_thread_new( "soapy_rx", receive_dupli_thread, rx);
  t_print("%s: receiver_thread_id=%p\n", __FUNCTION__, receive_thread_id);
}

static gpointer receive_dual_thread(gpointer data) {
  ASSERT_SERVER(NULL);
  RECEIVER **rx = (RECEIVER **)data;
  int flags = 0;
  long long timeNs = 0;
  long timeoutUs = 100000L;
  int i, j;
  double isample, qsample;
  t_print("soapy_protocol: receive_dual_thread\n");
  float *buffers[RECEIVERS];
  void *buffs[RECEIVERS];

  for (i = 0; i < RECEIVERS; ++i) {
    buffers[i] = g_new(float, max_samples * 2);
    buffs[i] = buffers[i];
  }

  running = TRUE;

  while (running) {
    int elements = SoapySDRDevice_readStream(
                     soapy_device, rx_stream[0], buffs, max_samples, &flags, &timeNs, timeoutUs
                   );

    if (elements < 0) {
      continue;
    }

    // First copy all the data from the buffers to each receiver
    for (i = 0; i < RECEIVERS; ++i) {
      if (!rx[i]) { continue; }

      for (j = 0; j < elements; ++j) {
        rx[i]->buffer[j * 2] = (double)buffers[i][j * 2];
        rx[i]->buffer[j * 2 + 1] = (double)buffers[i][j * 2 + 1];
      }
    }

    // Process each receiver's samples
    for (i = 0; i < RECEIVERS; ++i) {
      if (!rx[i]) { continue; }

      if (rx[i]->resampler != NULL) {
        int samples = xresample(rx[i]->resampler);

        for (j = 0; j < samples; ++j) {
          isample = rx[i]->resample_buffer[j * 2];
          qsample = rx[i]->resample_buffer[j * 2 + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx[i], qsample, isample);
          } else {
            rx_add_iq_samples(rx[i], isample, qsample);
          }

          // Process mic samples in the same loop that processes IQ samples
          if (can_transmit) {
            mic_samples++;

            if (mic_samples >= mic_sample_divisor) {
              // This call sets the heartbeat, not actually using mic samples
              tx_add_mic_sample(transmitter, 0);
              mic_samples = 0;
            }
          }
        }
      } else {
        for (j = 0; j < elements; ++j) {
          isample = rx[i]->buffer[j * 2];
          qsample = rx[i]->buffer[j * 2 + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx[i], qsample, isample);
          } else {
            rx_add_iq_samples(rx[i], isample, qsample);
          }

          // Process mic samples in the same loop that processes IQ samples
          if (can_transmit) {
            mic_samples++;

            if (mic_samples >= mic_sample_divisor) {
              // This call sets the heartbeat, not actually using mic samples
              tx_add_mic_sample(transmitter, 0);
              mic_samples = 0;
            }
          }
        }
      }

      // Only process one receiver's worth of mic samples
      break;
    }
  }

  t_print("soapy_protocol: receive_dual_thread: SoapySDRDevice_deactivateStream\n");
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream[0], 0, 0LL);

  for (i = 0; i < RECEIVERS; ++i) {
    g_free(buffers[i]);
  }

  return NULL;
}

static gpointer receive_dupli_thread(gpointer data) {
  ASSERT_SERVER(NULL);
  RECEIVER **rx = (RECEIVER **)data;
  int flags = 0;
  long long timeNs = 0;
  long timeoutUs = 100000L;
  int i, j;
  double isample, qsample;
  t_print("soapy_protocol: receive_dupli_thread\n");
  float *buffer = g_new(float, max_samples * 2);
  void *buffs[] = { buffer };
  running = TRUE;

  while (running) {
    int elements = SoapySDRDevice_readStream(
                     soapy_device, rx_stream[0], buffs, max_samples, &flags, &timeNs, timeoutUs
                   );

    if (elements < 0) {
      continue;
    }

    // First copy the raw buffer to each receiver's buffer
    for (i = 0; i < elements; ++i) {
      double i_sample = (double)buffer[i * 2];
      double q_sample = (double)buffer[i * 2 + 1];

      for (j = 0; j < RECEIVERS; ++j) {
        if (!rx[j]) { continue; }

        rx[j]->buffer[i * 2] = i_sample;
        rx[j]->buffer[i * 2 + 1] = q_sample;
      }
    }

    // Process only the first receiver's samples for timing purposes
    for (j = 0; j < 1; ++j) {
      if (!rx[j]) { continue; }

      if (rx[j]->resampler != NULL) {
        int samples = xresample(rx[j]->resampler);

        for (i = 0; i < samples; ++i) {
          isample = rx[j]->resample_buffer[i * 2];
          qsample = rx[j]->resample_buffer[i * 2 + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx[j], qsample, isample);
          } else {
            rx_add_iq_samples(rx[j], isample, qsample);
          }

          // Process mic samples in the same loop
          if (can_transmit) {
            mic_samples++;

            if (mic_samples >= mic_sample_divisor) {
              // This call sets the heartbeat, not actually using mic samples
              tx_add_mic_sample(transmitter, 0);
              mic_samples = 0;
            }
          }
        }
      } else {
        for (i = 0; i < elements; ++i) {
          isample = rx[j]->buffer[i * 2];
          qsample = rx[j]->buffer[i * 2 + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx[j], qsample, isample);
          } else {
            rx_add_iq_samples(rx[j], isample, qsample);
          }

          // Process mic samples in the same loop
          if (can_transmit) {
            mic_samples++;

            if (mic_samples >= mic_sample_divisor) {
              // This call sets the heartbeat, not actually using mic samples
              tx_add_mic_sample(transmitter, 0);
              mic_samples = 0;
            }
          }
        }
      }
    }

    // Now process the rest of the receivers without mic sampling
    for (j = 1; j < RECEIVERS; ++j) {
      if (!rx[j]) { continue; }

      if (rx[j]->resampler != NULL) {
        int samples = xresample(rx[j]->resampler);

        for (i = 0; i < samples; ++i) {
          isample = rx[j]->resample_buffer[i * 2];
          qsample = rx[j]->resample_buffer[i * 2 + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx[j], qsample, isample);
          } else {
            rx_add_iq_samples(rx[j], isample, qsample);
          }
        }
      } else {
        for (i = 0; i < elements; ++i) {
          isample = rx[j]->buffer[i * 2];
          qsample = rx[j]->buffer[i * 2 + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx[j], qsample, isample);
          } else {
            rx_add_iq_samples(rx[j], isample, qsample);
          }
        }
      }
    }
  }

  t_print("soapy_protocol: receive_dupli_thread: SoapySDRDevice_deactivateStream\n");
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream[0], 0, 0LL);
  g_free(buffer);
  return NULL;
}

void soapy_protocol_start_receiver(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;
  t_print("%s: id=%d\n", __FUNCTION__, rx->id);
  size_t channel = rx->adc;
  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc);
  t_print("%s: rate=%f\n", __FUNCTION__, rate);
  t_print("%s: activate Stream\n", __FUNCTION__);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream[channel], 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Start RX Stream failed");
  }

  t_print("%s: create receiver_thread\n", __FUNCTION__);
  receive_thread_id = g_thread_new( "soapy_rx", receive_thread, rx);
}

void soapy_protocol_create_transmitter(const TRANSMITTER *tx) {
  ASSERT_SERVER();
  int rc;
  double rate = (double) tx->iq_output_rate;
  t_print("%s: setting samplerate=%f\n", __FUNCTION__, rate);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_TX, tx->dac, rate);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    //
    // LIME: use small TX bandwidth, and use oversampling
    //
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_TX, tx->dac, 48000.0);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setBandwidth(%f) failed: %s\n", __FUNCTION__, rate, SoapySDR_errToStr(rc));
    }

    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: SoapySDRDevice setting oversampling failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  } else {
    t_print("%s: setting TX bandwidth=%f\n", __FUNCTION__, tx->dac, rate);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_TX, tx->dac, rate);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setBandwidth(%f) failed: %s\n", __FUNCTION__, rate, SoapySDR_errToStr(rc));
    }
  }

  size_t channel = tx->dac;
  t_print("%s: SoapySDRDevice_setupStream: channel=%ld\n", __FUNCTION__, channel);
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  rc = SoapySDRDevice_setupStream(soapy_device, &tx_stream, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setupStream (TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup TX Stream Failed");
  }

#else
  tx_stream = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (tx_stream == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup TX Stream Failed");
  }

#endif
  max_tx_samples = SoapySDRDevice_getStreamMTU(soapy_device, tx_stream);

  if (max_tx_samples > (2 * tx->fft_size)) {
    max_tx_samples = 2 * tx->fft_size;
  }

  t_print("%s: max_tx_samples=%d\n", __FUNCTION__, max_tx_samples);
  output_buffer = (float *)malloc(max_tx_samples * sizeof(float) * 2);
}

void soapy_protocol_start_transmitter(const TRANSMITTER *tx) {
  ASSERT_SERVER();
  int rc;
  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_TX, tx->dac);
  t_print("soapy_protocol_start_transmitter: activateStream rate=%f\n", rate);
  rc = SoapySDRDevice_activateStream(soapy_device, tx_stream, 0, 0LL, 0);

  if (rc != 0) {
    t_print("soapy_protocol_start_transmitter: SoapySDRDevice_activateStream failed: %s\n", SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Start TX Stream Failed");
  }
}

void soapy_protocol_stop_receiver(const RECEIVER *rx) {
  ASSERT_SERVER();
  // argument rx unused
  running = FALSE;

  if (receive_thread_id) {
    g_thread_join(receive_thread_id);
    receive_thread_id = NULL;
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop_transmitter(const TRANSMITTER *tx) {
  ASSERT_SERVER();
  int rc;
  t_print("soapy_protocol_stop_transmitter: deactivateStream\n");
  // argument tx unused
  rc = SoapySDRDevice_deactivateStream(soapy_device, tx_stream, 0, 0LL);

  if (rc != 0) {
    t_print("soapy_protocol_stop_transmitter: SoapySDRDevice_deactivateStream failed: %s\n", SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Stop TX Stream Failed");
  }
}

void soapy_protocol_init(gboolean hf) {
  ASSERT_SERVER();
  SoapySDRKwargs args = {};
  char temp[32];
  SoapySDR_setLogLevel(SOAPY_SDR_TRACE);
  t_print("%s: hf=%d driver=%s\n", __FUNCTION__, hf, radio->name);
  // initialise the radio
  SoapySDRKwargs_set(&args, "driver", radio->name);

  if (strcmp(radio->name, "rtlsdr") == 0) {
    snprintf(temp, sizeof(temp), "%d", radio->info.soapy.rtlsdr_count);
    SoapySDRKwargs_set(&args, "rtl", temp);

    if (hf) {
      SoapySDRKwargs_set(&args, "direct_samp", "2");
    } else {
      SoapySDRKwargs_set(&args, "direct_samp", "0");
    }
  } else if (strcmp(radio->name, "sdrplay") == 0) {
    snprintf(temp, sizeof(temp), "SDRplay Dev%d", radio->info.soapy.sdrplay_count);
    t_print("%s: label=%s\n", __FUNCTION__, temp);
    SoapySDRKwargs_set(&args, "label", temp);
  }

  soapy_device = SoapySDRDevice_make(&args);

  if (soapy_device == NULL) {
    t_print("%s: SoapySDRDevice_make failed: %s\n", __FUNCTION__, SoapySDRDevice_lastError());
    g_idle_add(fatal_error, "FATAL: Soapy Make Device Failed");
  }

  SoapySDRKwargs_clear(&args);

  if (can_transmit) {
    if (transmitter->local_microphone) {
      if (audio_open_input() != 0) {
        t_print("%s: audio_open_input failed\n", __FUNCTION__);
        transmitter->local_microphone = 0;
      }
    }
  }
}

static void *receive_thread(void *arg) {
  ASSERT_SERVER(NULL);
  //
  //  Since no mic samples arrive in SOAPY, we must use
  //  the incoming RX samples as a "heart beat" for the
  //  transmitter.
  //
  double isample;
  double qsample;
  int flags = 0;
  long long timeNs = 0;
  long timeoutUs = 100000L;
  int i;
  RECEIVER *rx = (RECEIVER *)arg;
  float *buffer = g_new(float, max_samples * 2);
  void *buffs[] = {buffer};
  running = TRUE;
  t_print("soapy_protocol: receive_thread\n");
  size_t channel = rx->adc;

  while (running) {
    int elements = SoapySDRDevice_readStream(soapy_device, rx_stream[channel], buffs, max_samples, &flags, &timeNs,
                   timeoutUs);

    //t_print("soapy_protocol_receive_thread: SoapySDRDevice_readStream failed: max_samples=%d read=%d\n",max_samples,elements);
    if (elements < 0) {
      continue;
    }

    for (i = 0; i < elements; i++) {
      rx->buffer[i * 2] = (double)buffer[i * 2];
      rx->buffer[(i * 2) + 1] = (double)buffer[(i * 2) + 1];
    }

    if (rx->resampler != NULL) {
      int samples = xresample(rx->resampler);

      for (i = 0; i < samples; i++) {
        isample = rx->resample_buffer[i * 2];
        qsample = rx->resample_buffer[(i * 2) + 1];

        if (soapy_iqswap) {
          rx_add_iq_samples(rx, qsample, isample);
        } else {
          rx_add_iq_samples(rx, isample, qsample);
        }

        if (can_transmit) {
          mic_samples++;

          if (mic_samples >= mic_sample_divisor) { // reduce to 48000
            //
            // We have no mic samples, this call only
            // sets the heart beat
            //
            tx_add_mic_sample(transmitter, 0);
            mic_samples = 0;
          }
        }
      }
    } else {
      for (i = 0; i < elements; i++) {
        isample = rx->buffer[i * 2];
        qsample = rx->buffer[(i * 2) + 1];

        if (soapy_iqswap) {
          rx_add_iq_samples(rx, qsample, isample);
        } else {
          rx_add_iq_samples(rx, isample, qsample);
        }

        if (can_transmit) {
          mic_samples++;

          if (mic_samples >= mic_sample_divisor) { // reduce to 48000
            tx_add_mic_sample(transmitter, 0);
            mic_samples = 0;
          }
        }
      }
    }
  }

  t_print("soapy_protocol: receive_thread: SoapySDRDevice_deactivateStream\n");
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream[channel], 0, 0LL);
  /*
  t_print("soapy_protocol: receive_thread: SoapySDRDevice_closeStream\n");
  SoapySDRDevice_closeStream(soapy_device,rx_stream[channel]);
  t_print("soapy_protocol: receive_thread: SoapySDRDevice_unmake\n");
  SoapySDRDevice_unmake(soapy_device);
  */
  g_free(buffer);
  return NULL;
}

void soapy_protocol_iq_samples(float isample, float qsample) {
  ASSERT_SERVER();
  int flags = 0;

  if (radio_is_transmitting()) {
    //
    // The "iqswap" logic has now been removed  from transmitter.c
    // and moved here, because this is where it is also handled
    // upon RX.
    //
    if (soapy_iqswap) {
      output_buffer[(output_buffer_index * 2)] = qsample;
      output_buffer[(output_buffer_index * 2) + 1] = isample;
    } else {
      output_buffer[(output_buffer_index * 2)] = isample;
      output_buffer[(output_buffer_index * 2) + 1] = qsample;
    }

    output_buffer_index++;

    if (output_buffer_index >= max_tx_samples) {
      const void *tx_buffs[] = {output_buffer};
      long long timeNs = 0;
      long timeoutUs = 100000L;
      int elements = SoapySDRDevice_writeStream(soapy_device, tx_stream, tx_buffs, max_tx_samples, &flags, timeNs, timeoutUs);

      if (elements != max_tx_samples) {
        t_print("soapy_protocol_iq_samples: writeStream returned %d for %d elements\n", elements, max_tx_samples);
      }

      output_buffer_index = 0;
    }
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop() {
  ASSERT_SERVER();
  t_print("soapy_protocol_stop\n");
  running = FALSE;
}

void soapy_protocol_set_rx_frequency(RECEIVER *rx, int v) {
  ASSERT_SERVER();
  int rc;

  if (soapy_device != NULL) {
    int id = rx->id;
    long long f = vfo[v].frequency;

    if (vfo[id].mode == modeCWU) {
      f -= (long long)cw_keyer_sidetone_frequency;
    } else if (vfo[id].mode == modeCWL) {
      f += (long long)cw_keyer_sidetone_frequency;
    }

    f += frequency_calibration - vfo[v].lo;

    if (have_lime) {
      //
      // LIME is *very* special:
      // we should not adjust the LO frequency while moving around
      // within a band, instead, keep the LO freq fixed and vary the offset.
      // To this end, we query the current LO frequency. If our new target freq is in the
      // range LO + 1Mhz ... LO + 5MHz, do not move LO but adjust offset.
      // If our new target frequency is outside, set it to Min(f - 3 MHz, 0)
      //
      double lo_freq = SoapySDRDevice_getFrequencyComponent(soapy_device, SOAPY_SDR_RX, rx->adc, "RF");
      double fd = (double) f;

      if (fd <= lo_freq + 1.0E6 || fd > lo_freq + 5.0E6) {
        //
        // need new LO freq
        //
        lo_freq = fd - 3.0E6;

        if (lo_freq < 1.0E6) { lo_freq = 1.0E6; }

        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, rx->adc, "RF", lo_freq, NULL);

        if (rc != 0) {
          t_print("soapy_protocol: SoapySDRDevice_setFrequency(RX) failed: %s\n", SoapySDR_errToStr(rc));
        }
      }

      // LO freq is set, determine and set offset
      rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, rx->adc, "BB", fd - lo_freq, NULL);

      if (rc != 0) {
        t_print("soapy_protocol: SoapySDRDevice_setFrequency(RX) failed: %s\n", SoapySDR_errToStr(rc));
      }
    } else {
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_RX, rx->adc, (double)f, NULL);

      if (rc != 0) {
        t_print("soapy_protocol: SoapySDRDevice_setFrequency(RX) failed: %s\n", SoapySDR_errToStr(rc));
      }
    }
  }
}

void soapy_protocol_set_tx_frequency(const TRANSMITTER *tx) {
  ASSERT_SERVER();
  int rc;

  if (can_transmit && soapy_device != NULL) {
    int v = vfo_get_tx_vfo();
    long long f;
    f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;

    if (vfo[v].xit_enabled) {
      f += vfo[v].xit;
    }

    f += frequency_calibration - vfo[v].lo;

    if (have_lime) {
      //
      // LIME is *very* special:
      // we should not adjust the LO frequency while moving around
      // within a band, instead, keep the LO freq fixed and vary the offset.
      // To this end, we query the current LO frequency. If our new target freq is in the
      // range LO + 1Mhz ... LO + 5MHz, do not move LO but adjust offset.
      // If our new target frequency is outside, set it to Min(f - 3 MHz, 0)
      //
      double lo_freq = SoapySDRDevice_getFrequencyComponent(soapy_device, SOAPY_SDR_TX, tx->dac, "RF");
      double fd = (double) f;

      if (fd <= lo_freq + 1.0E6 || fd > lo_freq + 5.0E6) {
        //
        // need new LO freq
        //
        lo_freq = fd - 3.0E6;

        if (lo_freq < 1.0E6) { lo_freq = 1.0E6; }

        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_TX, tx->dac, "RF", lo_freq, NULL);

        if (rc != 0) {
          t_print("soapy_protocol: SoapySDRDevice_setFrequency(RX) failed: %s\n", SoapySDR_errToStr(rc));
        }
      }

      // LO freq is set, determine and set offset
      rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_TX, tx->dac, "BB", fd - lo_freq, NULL);

      if (rc != 0) {
        t_print("soapy_protocol: SoapySDRDevice_setFrequency(RX) failed: %s\n", SoapySDR_errToStr(rc));
      }
    } else {
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_TX, tx->dac, (double) f, NULL);

      if (rc != 0) {
        t_print("soapy_protocol: SoapySDRDevice_setFrequency(TX) failed: %s\n", SoapySDR_errToStr(rc));
      }
    }
  }
}

void soapy_protocol_set_rx_antenna(RECEIVER *rx, int ant) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    if (ant >= (int) radio->info.soapy.rx_antennas) { ant = (int) radio->info.soapy.rx_antennas - 1; }

    t_print("soapy_protocol: set_rx_antenna: %s\n", radio->info.soapy.rx_antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_RX, rx->adc, radio->info.soapy.rx_antenna[ant]);

    if (rc != 0) {
      t_print("soapy_protocol: SoapySDRDevice_setAntenna RX failed: %s\n", SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_tx_antenna(const TRANSMITTER *tx, int ant) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    if (ant >= (int) radio->info.soapy.tx_antennas) { ant = (int) radio->info.soapy.tx_antennas - 1; }

    t_print("soapy_protocol: set_tx_antenna: %s\n", radio->info.soapy.tx_antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_TX, tx->dac, radio->info.soapy.tx_antenna[ant]);

    if (rc != 0) {
      t_print("soapy_protocol: SoapySDRDevice_setAntenna TX failed: %s\n", SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_gain(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;
  //t_print("soapy_protocol_set_gain: adc=%d gain=%f\n",gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, rx->adc, adc[rx->adc].gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGain failed: %s\n", SoapySDR_errToStr(rc));
  }
}

//
// attenuate/unattenuate can be used to switch the receiver temporarily
// to min gain (attenuate) or to restore the nominal gain (unattenuate)
//
void soapy_protocol_attenuate(RECEIVER *rx) {
  int rc;
  //t_print("soapy_protocol_set_gain: adc=%d gain=%f\n",gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, rx->adc, adc[rx->adc].min_gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGain failed: %s\n", SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_unattenuate(RECEIVER *rx) {
  soapy_protocol_set_gain(rx);
}

void soapy_protocol_set_gain_element(const RECEIVER *rx, char *name, int gain) {
  ASSERT_SERVER();
  int rc;
  t_print("%s: adc=%d %s=%d\n", __FUNCTION__, rx->adc, name, gain);
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_RX, rx->adc, name, (double)gain);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setGainElement %s failed: %s\n", __FUNCTION__, name, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_set_tx_gain(const TRANSMITTER *tx, int gain) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_TX, tx->dac, (double)gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGain failed: %s\n", SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_set_tx_gain_element(TRANSMITTER *tx, char *name, int gain) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_TX, tx->dac, name, (double)gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGainElement %s failed: %s\n", name, SoapySDR_errToStr(rc));
  }
}

int soapy_protocol_get_gain_element(RECEIVER *rx, char *name) {
  ASSERT_SERVER(0);
  double gain;
  gain = SoapySDRDevice_getGainElement(soapy_device, SOAPY_SDR_RX, rx->adc, name);
  return (int)gain;
}

int soapy_protocol_get_tx_gain_element(TRANSMITTER *tx, char *name) {
  ASSERT_SERVER(0);
  double gain;
  gain = SoapySDRDevice_getGainElement(soapy_device, SOAPY_SDR_TX, tx->dac, name);
  return (int)gain;
}

// cppcheck-suppress unusedFunction
gboolean soapy_protocol_get_automatic_gain(RECEIVER *rx) {
  ASSERT_SERVER(0);
  gboolean mode = SoapySDRDevice_getGainMode(soapy_device, SOAPY_SDR_RX, rx->adc);
  return mode;
}

void soapy_protocol_set_automatic_gain(RECEIVER *rx, gboolean mode) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGainMode(soapy_device, SOAPY_SDR_RX, rx->adc, mode);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_getGainMode failed: %s\n", SoapySDR_errToStr(rc));
  }
}
