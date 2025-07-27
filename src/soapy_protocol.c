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

static GThread *soapy_receive_thread_id = NULL;
static gpointer soapy_receive_thread(gpointer data);
static gpointer soapy_receive_dual_thread(gpointer data);  // for LIME

static gboolean running;

static int mic_samples = 0;
static int mic_sample_divisor = 1;

static int max_tx_samples;
static float *output_buffer;
static int output_buffer_index;

//
// the large "LIME" bandwidth refers to the bandwith BEFORE decimation
//
const double lime_rx_bw = 12000000.0;
const double lime_tx_bw =   768000.0;

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
    t_print("%s: setting sample rate (%f) failed: %s\n", __FUNCTION__, (double)rx->sample_rate,
            SoapySDR_errToStr(rc));
  }

  d = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc);
  d = SoapySDRDevice_getBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc);
#endif

  //
  // We stick to the hardware sample rate and use the WDSP resampler
  //
  if (rx->sample_rate == soapy_radio_sample_rate) {
    //
    // In this case, we no longer use the resampler
    //
    if (rx->resampler != NULL) {
      destroy_resample(rx->resampler);
      rx->resampler = NULL;
    }

    if (rx->resample_input != NULL) {
      g_free(rx->resample_input);
      rx->resample_input = NULL;
    }

    if (rx->resample_output != NULL) {
      g_free(rx->resample_output);
      rx->resample_output = NULL;
    }
  } else {
    //
    // Destroy previous resampler (if any) and create new one
    //
    if (rx->resampler != NULL) {
      destroy_resample(rx->resampler);
    }
    if (rx->resample_input != NULL) {
      g_free(rx->resample_input);
    }

    if (rx->resample_output != NULL) {
      g_free(rx->resample_output);
    }

    rx->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_input  = g_new(double, 2 * max_samples);
    rx->resample_output = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_samples, rx->resample_input, rx->resample_output, soapy_radio_sample_rate,
                                     rx->sample_rate, 0.0, 0, 1.0);
    rx->resample_count = 0;
  }
}

void soapy_protocol_create_receiver(RECEIVER *rx) {
  //
  // NOTE: this one will be called for LIME if a single RX channel is found
  //       (e.g. for a LIME-mini)
  //
  ASSERT_SERVER();
  int rc;
  mic_sample_divisor = rx->sample_rate / 48000;
  double bandwidth = (double) soapy_radio_sample_rate;

  t_print("%s: setting samplerate=%f adc=%d mic_sample_divisor=%d\n", __FUNCTION__,
          (double)soapy_radio_sample_rate,
          rx->adc, mic_sample_divisor);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: setting sample rate (%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    //
    // LIME: we have to use the large bandwidth (before decimation) here.
    //
    bandwidth = lime_rx_bw;
  }

  t_print("%s: adc=%d setting bandwidth=%f\n", __FUNCTION__, rx->adc, bandwidth);
  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc, bandwidth);

  //
  // A 2-MHz-Bandwidth was used as a constant value in previous versions,
  // we take this if setting bandwidth = sample rate fails
  //
  if (rc != 0) {
    bandwidth = 2000000.0;
    t_print("%s: adc=%d setting bandwidth=%f\n", __FUNCTION__, rx->adc, bandwidth);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc, bandwidth);
  }

  if (rc != 0) {
    t_print("%s: set band width (%f) failed: %s\n", __FUNCTION__, (double)bandwidth, SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    //
    // LIME: use oversampling/decimation
    //
    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: setting oversampling failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }

  size_t channel = rx->adc;
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  t_print("%s: SoapySDRDevice_setupStream(version<0x00080000): channel=%d\n", __FUNCTION__, (int) channel);
  rc = SoapySDRDevice_setupStream(soapy_device, &rx_stream[channel], SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup RX Stream Failed");
  }

#else
  t_print("%s: SoapySDRDevice_setupStream(version>=0x00080000): channel=%d\n", __FUNCTION__, (int) channel);
  rx_stream[channel] = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rx_stream[channel] == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed (rx_stream is NULL)\n", __FUNCTION__);
    g_idle_add(fatal_error, "FATAL: Soapy Setup RX Stream Failed");
  }

#endif
  max_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream[channel]);
  t_print("%s: max_samples=%d\n", __FUNCTION__, max_samples);

  if (max_samples > (2 * rx->fft_size)) { max_samples = 2 * rx->fft_size; }

  if (rx->sample_rate == soapy_radio_sample_rate) {
    rx->resample_input = NULL;
    rx->resample_output = NULL;
    rx->resampler = NULL;
  } else {
    rx->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_input = g_new(double, 2 * max_samples);
    rx->resample_output = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_samples, rx->resample_input, rx->resample_output, soapy_radio_sample_rate,
                                     rx->sample_rate, 0.0, 0, 1.0);
    rx->resample_count = 0;
  }
}

void soapy_protocol_create_dual_receiver(RECEIVER *rx1, RECEIVER *rx2) {
  //
  // Only called in the LIME case
  //
  ASSERT_SERVER();

  int rc;
  if (have_lime) {
    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: setting oversampling failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }

  mic_sample_divisor = rx1->sample_rate / 48000;

  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx1->adc, lime_rx_bw);

  if (rc != 0) {
    t_print("%s: RX1 set band width (%f) failed: %s\n", __FUNCTION__, (double)lime_rx_bw, SoapySDR_errToStr(rc));
  }

  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx1->adc, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: RX1 set sample rate (%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx2->adc, lime_rx_bw);

  if (rc != 0) {
    t_print("%s: RX2 set band width (%f) failed: %s\n", __FUNCTION__, (double)lime_rx_bw, SoapySDR_errToStr(rc));
  }

  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx2->adc, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: RX2 set sample rate (%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  size_t channels[2] = {0, 1};
  rx_stream[0] = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, channels, 2, NULL);

  if (rx_stream[0] == NULL) {
    t_print("%s: setupStream (RX) failed\n", __FUNCTION__);
    g_idle_add(fatal_error, "Soapy Setup RX Stream Failed");
  }

  max_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream[0]);

  if (max_samples > (2 * rx1->fft_size)) { max_samples = 2 * rx1->fft_size; }
  if (max_samples > (2 * rx2->fft_size)) { max_samples = 2 * rx2->fft_size; }

  rx1->resample_input  = rx2->resample_input  = NULL;
  rx1->resample_output = rx2->resample_output = NULL;
  rx1->resampler       = rx2->resampler       = NULL;

  //
  // Initialize resampler for those receivers that do not have the radio sample rate
  // NOTE: a dual receiver requires that the sample rates are the same for both RX
  //

  if (rx1->sample_rate != soapy_radio_sample_rate) {
    rx1->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx1->sample_rate);
    rx1->resample_output = g_new(double, rx1->resample_buffer_size);
    rx1->resample_input = g_new(double, 2 * max_samples);
    rx1->resampler = create_resample(1, max_samples, rx1->resample_input, rx1->resample_output, soapy_radio_sample_rate,
                                         rx1->sample_rate, 0.0, 0, 1.0);
  }

  if (rx2->sample_rate != soapy_radio_sample_rate) {
    rx2->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx2->sample_rate);
    rx2->resample_output = g_new(double, rx2->resample_buffer_size);
    rx2->resample_input = g_new(double, 2 * max_samples);
    rx2->resampler = create_resample(1, max_samples, rx2->resample_input, rx2->resample_output, soapy_radio_sample_rate,
                                         rx2->sample_rate, 0.0, 0, 1.0);
  }
}

void soapy_protocol_start_receiver(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;
  t_print("%s: id=%d\n", __FUNCTION__, rx->id);
  size_t channel = rx->adc;
  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc);
  t_print("%s: rate=%f\n", __FUNCTION__, rate);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream[channel], 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Start RX Stream failed");
  }

  soapy_receive_thread_id = g_thread_new( "soapy_rx", soapy_receive_thread, rx);
}

void soapy_protocol_start_dual_receiver(RECEIVER *rx1, RECEIVER *rx2) {
  //
  // The only difference to the standard
  // case is that a "dual" receive thread is started.
  //
  ASSERT_SERVER();
  int rc;
  double rate1 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx1->adc);
  double rate2 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx2->adc);
  t_print("%s: RX1 rate=%f, RX2 rate=%f\n", __FUNCTION__, rate1, rate2);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream[0], 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Start RX Stream failed");
  }

  RECEIVER **rxpair = g_new(RECEIVER *, 2);
  rxpair[0] = rx1;
  rxpair[1] = rx2;
  soapy_receive_thread_id = g_thread_new( "soapy_rx", soapy_receive_dual_thread, rxpair);
}

void soapy_protocol_create_transmitter(const TRANSMITTER *tx) {
  ASSERT_SERVER();
  int rc;
  double rate = (double) tx->iq_output_rate;

  t_print("%s: setting samplerate=%f\n", __FUNCTION__, rate);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_TX, tx->dac, rate);

  if (rc != 0) {
    t_print("%s: setSampleRate(%f) failed: %s\n", __FUNCTION__, rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    //
    // LIME: use small TX bandwidth, and use oversampling
    //
    t_print("%s: setting TX%d bandwidth=%f\n", __FUNCTION__, tx->dac, lime_tx_bw);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_TX, tx->dac, lime_tx_bw);

    if (rc != 0) {
      t_print("%s: setBandwidth(%f) failed: %s\n", __FUNCTION__, rate, SoapySDR_errToStr(rc));
    }

    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: SoapySDRDevice setting oversampling failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  } else {
    t_print("%s: setting TX%d bandwidth=%f\n", __FUNCTION__, tx->dac, rate);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_TX, tx->dac, rate);

    if (rc != 0) {
      t_print("%s: setBandwidth(%f) failed: %s\n", __FUNCTION__, rate, SoapySDR_errToStr(rc));
    }
  }

  size_t channel = tx->dac;
  t_print("%s: SoapySDRDevice_setupStream: channel=%d\n", __FUNCTION__, (int) channel);
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  rc = SoapySDRDevice_setupStream(soapy_device, &tx_stream, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: setupStream (TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup TX Stream Failed");
  }

#else
  tx_stream = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (tx_stream == NULL) {
    t_print("%s: setupStream (TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
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
  t_print("%s: rate=%f\n", __FUNCTION__, rate);
  rc = SoapySDRDevice_activateStream(soapy_device, tx_stream, 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Start TX Stream Failed");
  }
}

void soapy_protocol_stop_receiver(const RECEIVER *rx) {
  ASSERT_SERVER();
  // argument rx unused
  running = FALSE;

  if (soapy_receive_thread_id) {
    g_thread_join(soapy_receive_thread_id);
    soapy_receive_thread_id = NULL;
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop_transmitter(const TRANSMITTER *tx) {
  ASSERT_SERVER();
  int rc;
  // argument tx unused
  rc = SoapySDRDevice_deactivateStream(soapy_device, tx_stream, 0, 0LL);

  if (rc != 0) {
    t_print("%s: deactivateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
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

static void process_rx_buffer(RECEIVER *rx, const float *rxbuff, int elements, int micflag) {
  double isample, qsample;

  if (rx->resampler != NULL) {
    //
    // When using the resampler, copy all elements of the Soapy buffer into the input
    // buffer of the resampler. When it is full, do the resampling and process the
    // resampler's output buffer.
    // This code now works if the number of elements in the soapy buffer does not match
    // 2 * max_samples.
    // Note the "rxrc" stuff could be done by a good optimizing compiler
    //
    int rxrc = rx->resample_count;
    for (int i = 0; i < elements; i++) {
      rx->resample_input[rxrc++] = (double)rxbuff[i * 2];
      rx->resample_input[rxrc++] = (double)rxbuff[(i * 2) + 1];

      if (rxrc >= 2 * max_samples) {
        int samples = xresample(rx->resampler);

        for (int j = 0; j < samples; j++) {
          isample = rx->resample_output[j * 2];
          qsample = rx->resample_output[(j * 2) + 1];

          if (soapy_iqswap) {
            rx_add_iq_samples(rx, qsample, isample);
          } else {
            rx_add_iq_samples(rx, isample, qsample);
          }

          if (can_transmit && micflag) {
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
        rxrc = 0;
      }
    }
    rx->resample_count = rxrc;
  } else {
    //
    // When *not* using the resampler, convert elements in the Soapy buffer
    // to double and use them on-the-fly
    //
    for (int i = 0; i < elements; i++) {
      isample = (double)rxbuff[i * 2];
      qsample = (double)rxbuff[(i * 2) + 1];

      if (soapy_iqswap) {
        rx_add_iq_samples(rx, qsample, isample);
      } else {
        rx_add_iq_samples(rx, isample, qsample);
      }

      if (can_transmit && micflag) {
        mic_samples++;

        if (mic_samples >= mic_sample_divisor) { // reduce to 48000
          tx_add_mic_sample(transmitter, 0);
          mic_samples = 0;
        }
      }
    }
  }
}

static void *soapy_receive_thread(void *arg) {
  ASSERT_SERVER(NULL);
  //
  //  Since no mic samples arrive in SOAPY, we must use
  //  the incoming RX samples as a "heart beat" for the
  //  transmitter.
  //
  int flags = 0;
  long long timeNs = 0;
  long timeoutUs = 100000L;
  RECEIVER *rx = (RECEIVER *)arg;
  float *rxbuff = g_new(float, max_samples * 2);
  void *buffs[1] = {rxbuff};
  int rxadc = rx->adc;

  running = TRUE;
  t_print("%s started, rxadc=%d\n", __FUNCTION__, rxadc);

  while (running) {
    int elements = SoapySDRDevice_readStream(soapy_device, rx_stream[rxadc], buffs, max_samples, &flags, &timeNs,
                   timeoutUs);

    if (elements <= 0) {
      continue;
    }

    process_rx_buffer(rx, rxbuff, elements, 1);
  }

  t_print("%s: deactivateStream\n", __FUNCTION__);
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream[rxadc], 0, 0LL);
  /*
  t_print("%s: SoapySDRDevice_closeStream\n", __FUNCTION__);
  SoapySDRDevice_closeStream(soapy_device,rx_stream[rxadc]);
  t_print("%s: SoapySDRDevice_unmake\n", __FUNCTION__);
  SoapySDRDevice_unmake(soapy_device);
  */
  g_free(rxbuff);
  return NULL;
}

static gpointer soapy_receive_dual_thread(gpointer data) {
  ASSERT_SERVER(NULL);
  RECEIVER **rxpair = (RECEIVER **)data;
  int flags = 0;
  long long timeNs = 0;
  long timeoutUs = 100000L;
  float *rx1buff = g_new(float, max_samples*2);
  float *rx2buff = g_new(float, max_samples*2);
  void *buffs[2] = {rx1buff, rx2buff};

  running = TRUE;
  t_print("%s started\n", __FUNCTION__);

  while (running) {
    int elements = SoapySDRDevice_readStream(
                     soapy_device, rx_stream[0], buffs, max_samples, &flags, &timeNs, timeoutUs
                   );

    if (elements <= 0) {
      continue;
    }

    //
    // DIVERSITY handling is preferably done before resampling
    // so so not call rx_add_div_iq_samples() but update rx1buff HERE
    //
    if (diversity_enabled && rxpair[0]->sample_rate == rxpair[1]->sample_rate) {
      if (soapy_iqswap) {
        // (Q,I) pairs
        for (int i = 0; i < elements; i++) {
          rx1buff[2 * i + 1] += (div_cos * rx2buff[2 * i + 1] - div_sin * rx2buff[2 * i]);
          rx1buff[2 * i    ] += (div_sin * rx2buff[2 * i + 1] + div_cos * rx2buff[2 * i]);
        }
      } else {
        // (I,Q) pairs
        for (int i = 0; i < elements; i++) {
          rx1buff[2 * i    ] += (div_cos * rx2buff[2 * i] - div_sin * rx2buff[2 * i + 1]);
          rx1buff[2 * i + 1] += (div_sin * rx2buff[2 * i] + div_cos * rx2buff[2 * i + 1]);
        }
      }
    }

    process_rx_buffer(rxpair[0], rx1buff, elements, 1);
    if (receivers > 1) {
      process_rx_buffer(rxpair[1], rx2buff, elements, 0);  // suppress mic sample generation
    }
  }

  t_print("%s: SoapySDRDevice_deactivateStream\n", __FUNCTION__);
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream[0], 0, 0LL);

  g_free(rx1buff);
  g_free(rx2buff);
  g_free(rxpair);

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
        t_print("%s: writeStream returned %d for %d elements\n", __FUNCTION__, elements, max_tx_samples);
      }

      output_buffer_index = 0;
    }
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop() {
  ASSERT_SERVER();
  t_print("%s\n",__FUNCTION__);
  running = FALSE;
}

void soapy_protocol_set_rx_frequency(RECEIVER *rx, int v) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    int rc;
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

        if (diversity_enabled) {
          //
          // If diversity is enabled set both frequencies simultaneously
          //
          rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, 0, "RF", lo_freq, NULL);

          if (rc != 0) {
            t_print("%s: SoapySDRDevice_setFrequency(DIV-RX1) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
          }

          rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, 1, "RF", lo_freq, NULL);

          if (rc != 0) {
            t_print("%s: SoapySDRDevice_setFrequency(DIV-RX2) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
          }

        } else {

          rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, rx->adc, "RF", lo_freq, NULL);

          if (rc != 0) {
            t_print("%s: SoapySDRDevice_setFrequency(RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
          }
        }
      }

      // LO freq is set, determine and set offset

      if (diversity_enabled) {
        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, 0, "BB", fd - lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: SoapySDRDevice_setFrequencyComponent(DIV-RX1) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }

        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, 1, "BB", fd - lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: SoapySDRDevice_setFrequencyComponent(DIV-RX2) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }
      } else {
        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, rx->adc, "BB", fd - lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: SoapySDRDevice_setFrequency(RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }
      }
    } else {
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_RX, rx->adc, (double)f, NULL);

      if (rc != 0) {
        t_print("%s: SoapySDRDevice_setFrequency(RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
      }
    }
  }
}

void soapy_protocol_set_tx_frequency(const TRANSMITTER *tx) {
  ASSERT_SERVER();

  if (can_transmit && soapy_device != NULL) {
    int rc;
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
          t_print("%s: SoapySDRDevice_setFrequency(RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }
      }

      // LO freq is set, determine and set offset
      rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_TX, tx->dac, "BB", fd - lo_freq, NULL);

      if (rc != 0) {
        t_print("%s: SoapySDRDevice_setFrequency(RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
      }
    } else {
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_TX, tx->dac, (double) f, NULL);

      if (rc != 0) {
        t_print("%s: SoapySDRDevice_setFrequency(TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
      }
    }
  }
}

void soapy_protocol_set_rx_antenna(RECEIVER *rx, int ant) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    if (ant >= (int) radio->info.soapy.rx_antennas) { ant = (int) radio->info.soapy.rx_antennas - 1; }

    t_print("%s: set_rx_antenna: %s\n", __FUNCTION__, radio->info.soapy.rx_antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_RX, rx->adc, radio->info.soapy.rx_antenna[ant]);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setAntenna RX failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_tx_antenna(const TRANSMITTER *tx, int ant) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    if (ant >= (int) radio->info.soapy.tx_antennas) { ant = (int) radio->info.soapy.tx_antennas - 1; }

    t_print("%s: set_tx_antenna: %s\n", __FUNCTION__, radio->info.soapy.tx_antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_TX, tx->dac, radio->info.soapy.tx_antenna[ant]);

    if (rc != 0) {
      t_print("%s: SoapySDRDevice_setAntenna TX failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_gain(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;
  //t_print("%s: adc=%d gain=%f\n", __FUNCTION__, rx->adc, gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, rx->adc, adc[rx->adc].gain);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_attenuate(RECEIVER *rx) {
  //
  // Make this receiver temporarily "deaf". This may be useful while TXing
  //
  int rc;
  //t_print("%s: adc=%d gain=%f\n", __FUNCTION__, rx->adc, adc[rx->adc].min_gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, rx->adc, adc[rx->adc].min_gain);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_unattenuate(RECEIVER *rx) {
  //
  // Restore nominal RF gain to recover from having "deaf-ened" it
  // This must not do any harm if receivers have not been "deaf-ened" before
  // (this is the case in DUPLEX).
  //
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
    t_print("%s: SoapySDRDevice_setGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_set_tx_gain_element(TRANSMITTER *tx, char *name, int gain) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_TX, tx->dac, name, (double)gain);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setGainElement %s failed: %s\n", __FUNCTION__, name, SoapySDR_errToStr(rc));
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
    t_print("%s: SoapySDRDevice_getGainMode failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}
