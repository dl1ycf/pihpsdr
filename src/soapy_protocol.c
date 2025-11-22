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
#include <math.h>

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

//
// NOTE on streams and channels:
//
// - we have at most 2 streams, one for RX and one for TX
// - the TX stream only uses a single channel, namely channel=0
// - if more than 1 RX channel are "discovered", we use 2 channels
//   for the RX stream
// - therefore we need siblings that implement receiver
//   creation, starting, and the receive thread, namely
//
//   soapy_create_single_receiver     vs.  soapy_create_dual_receiver
//   soapy_start_single_receiver      vs.  soapy_start_dual_receiver
//   soapy_receive_single_thread      vs.  soapy_receive_dual_thread
//

static SoapySDRDevice *soapy_device;
static SoapySDRStream *rx_stream;
static int max_rx_samples;
static SoapySDRStream *tx_stream;
static int max_tx_samples;

static GThread *soapy_receive_thread_id = NULL;
static gpointer soapy_receive_single_thread(gpointer data);  // for 1RX
static gpointer soapy_receive_dual_thread(gpointer data);    // for 2RX

static int running;

static int mic_samples = 0;
static int mic_sample_divisor = 1;

static float *tx_output_buffer;
static int tx_output_buffer_index;

//
// the large "LIME" bandwidth refers to the bandwith BEFORE decimation
//
const double lime_rx_bw = 12000000.0;
const double lime_tx_bw =   768000.0;

//
// LIME: flag for "muting" RX2 if RX1/RX2 freq. difference is larger than 10 MHz
//       (note RX1/RX2 share the LO)
//
static int lime_mute_rx2 = 0;

//
// TO BE REMOVED: any other function that needs this should be
//                moved to this file
//
SoapySDRDevice *get_soapy_device() {
  return soapy_device;
}

void soapy_protocol_change_rx_sample_rate(RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // rx->mutex already locked, so we can call this  only
  // if the radio is stopped -- we cannot change the resampler
  // while the receive thread is stuck in rx_add_iq_samples()
  //
#if 0
  //
  //  sample code to change the sample rate
  //  (without using a resampler). However, going to lower
  //  sample rates should also involve going to smaller
  //  band widths, otherwise there will be aliases,
  //  and most radios do not have small band width filters.
  //
  int rc;
  double d;
  t_print("%s: setting samplerate=%f\n", __FUNCTION__, (double)rx->sample_rate);
  d = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->id);
  d = SoapySDRDevice_getBandwidth(soapy_device, SOAPY_SDR_RX, rx->id);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx->id, (double)rx->sample_rate);

  if (rc != 0) {
    t_print("%s: SetSampleRate (%f) failed: %s\n", __FUNCTION__, (double)rx->sample_rate,
            SoapySDR_errToStr(rc));
  }

  d = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->id);
  d = SoapySDRDevice_getBandwidth(soapy_device, SOAPY_SDR_RX, rx->id);
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

    rx->resample_buffer_size = 2 * max_rx_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_input  = g_new(double, 2 * max_rx_samples);
    rx->resample_output = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_rx_samples, rx->resample_input, rx->resample_output, soapy_radio_sample_rate,
                                     rx->sample_rate, 0.0, 0, 1.0);
    rx->resample_count = 0;
  }

  if (rx->id == 0) {
    mic_sample_divisor = rx->sample_rate / 48000;
  }
}

void soapy_protocol_create_single_receiver(RECEIVER *rx) {
  //
  // NOTE: this one will be called if there is only one receiver,
  //       and will create a rxstream with a single channel=0
  //       So this must be a no-op if rx->id is not zero
  //
  ASSERT_SERVER();
  int rc;

  if (rx->id != 0) {
    t_print("%s:WARNING:SOAPY:create single receiver but id nonzero\n", __FUNCTION__);
    return;
  }

  mic_sample_divisor = rx->sample_rate / 48000;
  double bandwidth = (double) soapy_radio_sample_rate;
  t_print("%s: setting samplerate=%f id=%d mic_sample_divisor=%d\n", __FUNCTION__,
          (double)soapy_radio_sample_rate,
          rx->id, mic_sample_divisor);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx->id, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: SetSampleRate (%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    //
    // LIME: we have to use the large bandwidth (before decimation) here.
    //
    bandwidth = lime_rx_bw;
  }

  t_print("%s: id=%d setting bandwidth=%f\n", __FUNCTION__, rx->id, bandwidth);
  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->id, bandwidth);

  //
  // A 2-MHz-Bandwidth was used as a constant value in previous versions,
  // we take this if setting bandwidth = sample rate fails
  //
  if (rc != 0) {
    bandwidth = 2000000.0;
    t_print("%s: id=%d setting bandwidth=%f\n", __FUNCTION__, rx->id, bandwidth);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->id, bandwidth);
  }

  if (rc != 0) {
    t_print("%s: SetBandWidth (%f) failed: %s\n", __FUNCTION__, (double)bandwidth, SoapySDR_errToStr(rc));
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

  size_t channel = rx->id;
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  t_print("%s: SetupStream(version<0x00080000): channel=%d\n", __FUNCTION__, channel);
  rc = SoapySDRDevice_setupStream(soapy_device, &rx_stream, SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SetupStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup RX Stream Failed");
  }

#else
  t_print("%s: SetupStream(version>=0x00080000): channel=%d\n", __FUNCTION__, (int) channel);
  rx_stream = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rx_stream == NULL) {
    t_print("%s: SetupStream yields NULL\n", __FUNCTION__);
    g_idle_add(fatal_error, "FATAL: Soapy Setup RX Stream Failed");
  }

#endif
  max_rx_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream);
  t_print("%s: max_rx_samples=%d\n", __FUNCTION__, max_rx_samples);

  if (max_rx_samples > (2 * rx->fft_size)) { max_rx_samples = 2 * rx->fft_size; }

  if (rx->sample_rate == soapy_radio_sample_rate) {
    rx->resample_input = NULL;
    rx->resample_output = NULL;
    rx->resampler = NULL;
  } else {
    rx->resample_buffer_size = 2 * max_rx_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_input = g_new(double, 2 * max_rx_samples);
    rx->resample_output = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_rx_samples, rx->resample_input, rx->resample_output, soapy_radio_sample_rate,
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
  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx1->id, lime_rx_bw);

  if (rc != 0) {
    t_print("%s: SetBandWidth RX1 (%f) failed: %s\n", __FUNCTION__, (double)lime_rx_bw, SoapySDR_errToStr(rc));
  }

  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx1->id, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: SetSampleRate RX1 (%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx2->id, lime_rx_bw);

  if (rc != 0) {
    t_print("%s: SetBandWidth RX2 (%f) failed: %s\n", __FUNCTION__, (double)lime_rx_bw, SoapySDR_errToStr(rc));
  }

  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx2->id, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: SetSampleRate RX2 (%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  size_t channels[2] = {0, 1};
  rx_stream = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, channels, 2, NULL);

  if (rx_stream == NULL) {
    t_print("%s: SetupStream RX failed\n", __FUNCTION__);
    g_idle_add(fatal_error, "Soapy Setup RX Stream Failed");
  }

  max_rx_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream);

  if (max_rx_samples > (2 * rx1->fft_size)) { max_rx_samples = 2 * rx1->fft_size; }

  if (max_rx_samples > (2 * rx2->fft_size)) { max_rx_samples = 2 * rx2->fft_size; }

  rx1->resample_input  = rx2->resample_input  = NULL;
  rx1->resample_output = rx2->resample_output = NULL;
  rx1->resampler       = rx2->resampler       = NULL;

  //
  // Initialize resampler for those receivers that do not have the radio sample rate
  // NOTE: a dual receiver requires that the sample rates are the same for both RX
  //

  if (rx1->sample_rate != soapy_radio_sample_rate) {
    rx1->resample_buffer_size = 2 * max_rx_samples / (soapy_radio_sample_rate / rx1->sample_rate);
    rx1->resample_output = g_new(double, rx1->resample_buffer_size);
    rx1->resample_input = g_new(double, 2 * max_rx_samples);
    rx1->resampler = create_resample(1, max_rx_samples, rx1->resample_input, rx1->resample_output, soapy_radio_sample_rate,
                                     rx1->sample_rate, 0.0, 0, 1.0);
  }

  if (rx2->sample_rate != soapy_radio_sample_rate) {
    rx2->resample_buffer_size = 2 * max_rx_samples / (soapy_radio_sample_rate / rx2->sample_rate);
    rx2->resample_output = g_new(double, rx2->resample_buffer_size);
    rx2->resample_input = g_new(double, 2 * max_rx_samples);
    rx2->resampler = create_resample(1, max_rx_samples, rx2->resample_input, rx2->resample_output, soapy_radio_sample_rate,
                                     rx2->sample_rate, 0.0, 0, 1.0);
  }
}

void soapy_protocol_start_single_receiver(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;

  if (rx->id != 0) {
    t_print("%s: WARNING: SOAPY: id nonzero\n", __FUNCTION__);
  }

  size_t channel = rx->id;
  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, channel);
  t_print("%s: rate=%f\n", __FUNCTION__, rate);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream, 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: ActivateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Start RX Stream failed");
  }

  soapy_receive_thread_id = g_thread_new( "soapy_rx", soapy_receive_single_thread, rx);
}

void soapy_protocol_start_dual_receiver(RECEIVER *rx1, RECEIVER *rx2) {
  //
  // Here we start a stream with two channels which MUST be 0 and 1
  //
  ASSERT_SERVER();
  int rc;

  if (rx1->id != 0 || rx2->id != 1) {
    t_print("%s:WARNING:SOAPY: id not 0 and 1\n", __FUNCTION__);
    return;
  }

  double rate1 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx1->id);
  double rate2 = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx2->id);
  t_print("%s: RX1 rate=%f, RX2 rate=%f\n", __FUNCTION__, rate1, rate2);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream, 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: ActivateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
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
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_TX, 0, rate);

  if (rc != 0) {
    t_print("%s: SetSampleRate(%f) failed: %s\n", __FUNCTION__, rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    //
    // LIME: use small TX bandwidth, and use oversampling
    //
    t_print("%s: setting TX%d bandwidth=%f\n", __FUNCTION__, 0, lime_tx_bw);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_TX, 0, lime_tx_bw);

    if (rc != 0) {
      t_print("%s: SetBandwidth(%f) failed: %s\n", __FUNCTION__, rate, SoapySDR_errToStr(rc));
    }

    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: setting oversampling failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  } else {
    t_print("%s: setting TX%d bandwidth=%f\n", __FUNCTION__, 0, rate);
    rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_TX, 0, rate);

    if (rc != 0) {
      t_print("%s: SetBandwidth(%f) failed: %s\n", __FUNCTION__, rate, SoapySDR_errToStr(rc));
    }
  }

  size_t channel = 0;
  t_print("%s: setupStream: channel=%d\n", __FUNCTION__, (int) channel);
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  rc = SoapySDRDevice_setupStream(soapy_device, &tx_stream, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SetupStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup TX Stream Failed");
  }

#else
  tx_stream = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (tx_stream == NULL) {
    t_print("%s: SetupStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Setup TX Stream Failed");
  }

#endif
  max_tx_samples = SoapySDRDevice_getStreamMTU(soapy_device, tx_stream);

  if (max_tx_samples > (2 * tx->fft_size)) {
    max_tx_samples = 2 * tx->fft_size;
  }

  t_print("%s: max_tx_samples=%d\n", __FUNCTION__, max_tx_samples);
  tx_output_buffer = (float *)malloc(max_tx_samples * sizeof(float) * 2);
}

void soapy_protocol_start_transmitter() {
  ASSERT_SERVER();
  int rc;
  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_TX, 0);
  t_print("%s: rate=%f\n", __FUNCTION__, rate);
  rc = SoapySDRDevice_activateStream(soapy_device, tx_stream, 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: ActivateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "FATAL: Soapy Start TX Stream Failed");
  }
}

void soapy_protocol_stop_receivers() {
  ASSERT_SERVER();
  running = FALSE;

  //
  // This terminates the receiver thread, let it be
  // the single or dual receiver thread.
  // The terminating thread will then deactivate the RX stream
  //
  if (soapy_receive_thread_id) {
    g_thread_join(soapy_receive_thread_id);
    soapy_receive_thread_id = NULL;
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop_transmitter() {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_deactivateStream(soapy_device, tx_stream, 0, 0LL);

  if (rc != 0) {
    t_print("%s: DeactivateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
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
    snprintf(temp, sizeof(temp), "%d", radio->soapy.rtlsdr_count);
    SoapySDRKwargs_set(&args, "rtl", temp);

    if (hf) {
      SoapySDRKwargs_set(&args, "direct_samp", "2");
    } else {
      SoapySDRKwargs_set(&args, "direct_samp", "0");
    }
  } else if (strcmp(radio->name, "sdrplay") == 0) {
    snprintf(temp, sizeof(temp), "SDRplay Dev%d", radio->soapy.sdrplay_count);
    SoapySDRKwargs_set(&args, "label", temp);
  }

  soapy_device = SoapySDRDevice_make(&args);

  if (soapy_device == NULL) {
    t_print("%s: MakeDevice failed: %s\n", __FUNCTION__, SoapySDRDevice_lastError());
    g_idle_add(fatal_error, "FATAL: Soapy Make Device Failed");
  }

  SoapySDRKwargs_clear(&args);
}

static void process_rx_buffer(RECEIVER *rx, const float *rxbuff, int elements, int micflag) {
  double isample, qsample;

  if (rx->resampler != NULL) {
    //
    // When using the resampler, copy all elements of the Soapy buffer into the input
    // buffer of the resampler. When it is full, do the resampling and process the
    // resampler's output buffer.
    // This code now works if the number of elements in the soapy buffer does not match
    // 2 * max_rx_samples.
    // Note the "rxrc" stuff could be done by a good optimizing compiler
    //
    int rxrc = rx->resample_count;

    for (int i = 0; i < elements; i++) {
      rx->resample_input[rxrc++] = (double)rxbuff[i * 2];
      rx->resample_input[rxrc++] = (double)rxbuff[(i * 2) + 1];

      if (rxrc >= 2 * max_rx_samples) {
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

static void *soapy_receive_single_thread(void *arg) {
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
  float *rxbuff = g_new(float, max_rx_samples * 2);
  void *buffs[1] = {rxbuff};
  int id = rx->id;
  running = TRUE;
  t_print("%s started, id=%d\n", __FUNCTION__, id);

  while (running) {
    int elements = SoapySDRDevice_readStream(soapy_device, rx_stream, buffs, max_rx_samples, &flags, &timeNs,
                   timeoutUs);

    if (elements <= 0) {
      continue;
    }

    process_rx_buffer(rx, rxbuff, elements, 1);
  }

  t_print("%s: deactivateStream\n", __FUNCTION__);
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream, 0, 0LL);
  /*
  t_print("%s: SoapySDRDevice_closeStream\n", __FUNCTION__);
  SoapySDRDevice_closeStream(soapy_device,rx_stream);
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
  float *rx1buff = g_new(float, max_rx_samples * 2);
  float *rx2buff = g_new(float, max_rx_samples * 2);
  void *buffs[2] = {rx1buff, rx2buff};
  running = TRUE;
  t_print("%s started\n", __FUNCTION__);

  while (running) {
    int elements = SoapySDRDevice_readStream(
                     soapy_device, rx_stream, buffs, max_rx_samples, &flags, &timeNs, timeoutUs
                   );

    if (elements <= 0) {
      continue;
    }

    process_rx_buffer(rxpair[0], rx1buff, elements, 1);

    if (receivers > 1) {
      //
      // Do not just stop delivering samples to RX2 if it is muted
      // (this may happen if the RX1/RX2 frequency difference is too large)
      // but rather feed zero samples
      //
      if (lime_mute_rx2) { memset(rx2buff, 0, 2 * elements * sizeof(float)); }

      process_rx_buffer(rxpair[1], rx2buff, elements, 0);  // suppress mic sample generation
    }
  }

  t_print("%s: SoapySDRDevice_deactivateStream\n", __FUNCTION__);
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream, 0, 0LL);
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
      tx_output_buffer[(tx_output_buffer_index * 2)] = qsample;
      tx_output_buffer[(tx_output_buffer_index * 2) + 1] = isample;
    } else {
      tx_output_buffer[(tx_output_buffer_index * 2)] = isample;
      tx_output_buffer[(tx_output_buffer_index * 2) + 1] = qsample;
    }

    tx_output_buffer_index++;

    if (tx_output_buffer_index >= max_tx_samples) {
      const void *tx_buffs[] = {tx_output_buffer};
      long long timeNs = 0;
      long timeoutUs = 100000L;
      int elements = SoapySDRDevice_writeStream(soapy_device, tx_stream, tx_buffs, max_tx_samples, &flags, timeNs, timeoutUs);

      if (elements != max_tx_samples) {
        t_print("%s: writeStream returned %d for %d elements\n", __FUNCTION__, elements, max_tx_samples);
      }

      tx_output_buffer_index = 0;
    }
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop() {
  ASSERT_SERVER();
  t_print("%s\n", __FUNCTION__);
  running = FALSE;
}

void soapy_protocol_set_rx_frequency(int id) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    int rc;
    long long f = vfo[id].frequency - vfo[id].lo;
    f = (f * (10000000LL+ frequency_calibration)) / 10000000LL;

    if (have_lime) {
      //
      // LIME is *very* special:
      // we should not adjust the LO frequency while moving around
      // within a band, instead, keep the LO freq fixed and vary the offset.
      // To this end, we query the current LO frequency. If our new target freq
      // leads to and offset whose absolute value is in the range 1-5 MHz,
      // do not move LO but adjust offset.
      // If the LO freq is not compatible with this, choose a new one and change both LO and offset.
      //
      double lo_freq = SoapySDRDevice_getFrequencyComponent(soapy_device, SOAPY_SDR_RX, id, "RF");
      double fd = (double) f;
      int lo_ok = 1;
      double diff;
      diff = fabs(fd - lo_freq);

      if (diff < 1.0E6 || diff > 5.0E6) { lo_ok = 0; }

      double fd2 = fd;
      int sid = (id == 0) ? 1 : 0; // ID of the "other" receiver (2RX case)
      lime_mute_rx2 = 0;

      if (RECEIVERS > 1) {
        //
        // A further complication arises because RX1 and RX2 share the LO, so the LO freq must
        // be compatible with both RX. If this is not possible (RX1 and RX2 frequency more
        // than 10 MHz apart) mute RX2.
        //
        long long f2 = vfo[sid].frequency - vfo[sid].lo;
        f2 = (f2 * (10000000LL+frequency_calibration)) / 10000000LL;
        fd2 = (double) f2;

        if (fabs(fd - fd2) > 10.0e6) {
          lime_mute_rx2 = 1;

          if (id == 1) { lo_ok = 1; } // No need to change LO if RX2 moves astray

          fd2 = fd;
        } else {
          diff = fabs(fd2 - lo_freq);

          if (diff < 1.0E6 || diff > 5.0E6) { lo_ok = 0; }
        }
      }

      if (!lo_ok) {
        if (RECEIVERS < 2 || lime_mute_rx2) {
          // 1RX case, or RX2 freq out of range
          lo_freq = fd - 3.0e6;
        } else {
          // 2RX case
          lo_freq = 0.5 * (fd + fd2);

          if (fabs(fd - fd2) < 2.0e6) {
            lo_freq -= 3.0e6;
          }
        }

        //t_print("%s: New LIME LO RX1/RX2 freq=%f\n", __FUNCTION__, lo_freq);
        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, 1, "RF", lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: RX2-LO failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }

        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, 0, "RF", lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: RX1-LO failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }
      }

      // LO freq is set, determine and set offset
      //t_print("%s: New LIME RX%d offset=%f\n", __FUNCTION__, id, fd - lo_freq);
      rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, id, "BB", fd - lo_freq, NULL);

      if (rc != 0) {
        t_print("%s: RX%d-BB failed: %s\n", __FUNCTION__, id + 1, SoapySDR_errToStr(rc));
      }

      if (!lo_ok && RECEIVERS > 1 && !lime_mute_rx2) {
        //
        // For the 2RX case and the LO freq has been changed, we need to re-calculate the offset of
        // the "other" receiver
        //
        t_print("%s: New LIME RX%d offset=%f\n", __FUNCTION__, sid, fd2 - lo_freq);
        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_RX, sid, "BB", fd2 - lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: RX%d-BB failed: %s\n", __FUNCTION__, sid + 1, SoapySDR_errToStr(rc));
        }
      }
    } else {
      //
      // if not LIME, we simply use setFrequency instead of setFrequencyComponent
      //
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_RX, id, (double)f, NULL);

      if (rc != 0) {
        t_print("%s: RX%d failed: %s\n", __FUNCTION__, id + 1, SoapySDR_errToStr(rc));
      }
    }
  }
}

void soapy_protocol_set_tx_frequency() {
  ASSERT_SERVER();

  if (can_transmit && soapy_device != NULL) {
    int rc;
    int v = vfo_get_tx_vfo();
    long long f;
    f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;

    if (vfo[v].xit_enabled) {
      f += vfo[v].xit;
    }

    f -= vfo[v].lo;
    f = (f * (10000000LL + frequency_calibration)) / 10000000LL;

    if (have_lime) {
      //
      // LIME is *very* special:
      // we should not adjust the LO frequency while moving around
      // within a band, instead, keep the LO freq fixed and vary the offset.
      // To this end, we query the current LO frequency. If our new target freq is in the
      // range LO + 1Mhz ... LO + 5MHz, do not move LO but adjust offset.
      // If our new target frequency is outside, set it to Min(f - 3 MHz, 0)
      //
      double lo_freq = SoapySDRDevice_getFrequencyComponent(soapy_device, SOAPY_SDR_TX, 0, "RF");
      double fd = (double) f;

      if (fd <= lo_freq + 1.0E6 || fd > lo_freq + 5.0E6) {
        //
        // need new LO freq
        //
        lo_freq = fd - 3.0E6;
        rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_TX, 0, "RF", lo_freq, NULL);

        if (rc != 0) {
          t_print("%s: TX-LO failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
        }
      }

      // LO freq is set, determine and set offset
      rc = SoapySDRDevice_setFrequencyComponent(soapy_device, SOAPY_SDR_TX, 0, "BB", fd - lo_freq, NULL);

      if (rc != 0) {
        t_print("%s: TX-BB failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
      }
    } else {
      //
      // if not LIME, we simply use setFrequency instead of setFrequencyComponent
      //
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_TX, 0, (double) f, NULL);

      if (rc != 0) {
        t_print("%s: TX: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
      }
    }
  }
}

void soapy_protocol_set_rx_antenna(int id, int ant) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    char *antname;

    if (ant > radio->soapy.rx[id].antennas - 1) { ant = radio->soapy.rx[id].antennas - 1; }

    antname = radio->soapy.rx[id].antenna[ant];
    t_print("%s: set_rx_antenna: id=%d ant=%s\n", __FUNCTION__, id, antname);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_RX, id, antname);

    if (rc != 0) {
      t_print("%s: SetAntenna failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_tx_antenna(int ant) {
  ASSERT_SERVER();

  if (soapy_device != NULL) {
    if (ant >= (int) radio->soapy.tx.antennas) { ant = (int) radio->soapy.tx.antennas - 1; }

    t_print("%s: set_tx_antenna: %s\n", __FUNCTION__, radio->soapy.tx.antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_TX, 0, radio->soapy.tx.antenna[ant]);

    if (rc != 0) {
      t_print("%s: SetAntenna failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_rx_gain(int id) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, id, adc[id].gain);

  if (rc != 0) {
    t_print("%s: SetGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_rx_attenuate(int id) {
  //
  // Make this receiver temporarily "deaf". This may be useful while TXing
  //
  int rc;
  //t_print("%s: id=%d gain=%f\n", __FUNCTION__, id, adc[id].min_gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, id, adc[id].min_gain);

  if (rc != 0) {
    t_print("%s: SetGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_rx_unattenuate(int id) {
  //
  // Restore nominal RF gain to recover from having "deaf-ened" it
  // This must not do any harm if receivers have not been "deaf-ened" before
  // (this is the case in DUPLEX).
  //
  soapy_protocol_set_rx_gain(id);
}

void soapy_protocol_set_rx_gain_element(int id, char *name, double gain) {
  ASSERT_SERVER();
  int rc;
  t_print("%s: id=%d %s=%f\n", __FUNCTION__, id, name, gain);
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_RX, id, name, gain);

  if (rc != 0) {
    t_print("%s: SetGainElement %s failed: %s\n", __FUNCTION__, name, SoapySDR_errToStr(rc));
  }

  //
  // The overall gain has now changed. So we need to query it and set the gain
  //
  adc[id].gain = SoapySDRDevice_getGain(soapy_device, SOAPY_SDR_RX, id);
}

void soapy_protocol_set_tx_gain(double gain) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_TX, 0, gain);

  if (rc != 0) {
    t_print("%s: SetGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_set_tx_gain_element(char *name, double gain) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_TX, 0, name, gain);

  if (rc != 0) {
    t_print("%s: SetGainElement %s failed: %s\n", __FUNCTION__, name, SoapySDR_errToStr(rc));
  }

  //
  // The overall gain has now changed. So we need to query it and set the gain
  //
  if (can_transmit) {
    transmitter->drive = (int)  SoapySDRDevice_getGain(soapy_device, SOAPY_SDR_TX, 0);
  }
}

double soapy_protocol_get_rx_gain_element(int id, char *name) {
  ASSERT_SERVER(0);
  double gain;
  gain = SoapySDRDevice_getGainElement(soapy_device, SOAPY_SDR_RX, id, name);
  return gain;
}

double soapy_protocol_get_tx_gain_element(char *name) {
  ASSERT_SERVER(0);
  double gain;
  gain = SoapySDRDevice_getGainElement(soapy_device, SOAPY_SDR_TX, 0, name);
  return gain;
}

// cppcheck-suppress unusedFunction
gboolean soapy_protocol_get_automatic_gain(int id) {
  ASSERT_SERVER(0);
  gboolean mode = SoapySDRDevice_getGainMode(soapy_device, SOAPY_SDR_RX, id);
  return mode;
}

void soapy_protocol_set_automatic_gain(int id, gboolean mode) {
  ASSERT_SERVER();
  int rc;
  rc = SoapySDRDevice_setGainMode(soapy_device, SOAPY_SDR_RX, id, mode);

  if (rc != 0) {
    t_print("%s: SetGainMode failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}
