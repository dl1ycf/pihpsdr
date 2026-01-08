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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>


#include "discovered.h"
#include "message.h"
#include "soapy_discovery.h"

static int rtlsdr_count = 0;
static int sdrplay_count = 0;

static void get_info(char *driver) {
  int sample_rate;
  SoapySDRKwargs args = {};
  int software_version = 0;
  const char *address = NULL;
  char fw_version[32];
  char gw_version[32];
  char hw_version[32];
  char p_version[32];
  *fw_version = *gw_version = *hw_version = *p_version = 0;

  if (devices >= MAX_DEVICES) {
    t_print("%s: MAX_DEVICES met for driver=%s\n", __FUNCTION__, driver);
    return;
  }

  memset(&discovered[devices], 0, sizeof(DISCOVERED));
  discovered[devices].device = SOAPYSDR_USB_DEVICE;
  discovered[devices].protocol = SOAPYSDR_PROTOCOL;
  snprintf(discovered[devices].name, sizeof(discovered[devices].name), "%s", driver);
  discovered[devices].status = STATE_AVAILABLE;
  SoapySDRKwargs_set(&args, "driver", driver);

  //
  // In case more than one RTLsdr or SDRplay devices are connected,
  // enumerate them
  //
  if (strcmp(driver, "rtlsdr") == 0) {
    char count[16];
    snprintf(count, sizeof(count), "%d", rtlsdr_count);
    SoapySDRKwargs_set(&args, "rtl", count);
    discovered[devices].soapy.rtlsdr_count = rtlsdr_count++;
  } else if (strcmp(driver, "sdrplay") == 0) {
    char label[16];
    snprintf(label, sizeof(label), "SDRplay Dev%d", sdrplay_count);
    SoapySDRKwargs_set(&args, "label", label);
    discovered[devices].soapy.sdrplay_count = sdrplay_count++;
  }

  SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
  SoapySDRKwargs_clear(&args);
  software_version = 0;
  const char *driverkey = SoapySDRDevice_getDriverKey(sdr);
  t_print("%s: DriverKey=%s\n", __FUNCTION__, driverkey);
  snprintf(discovered[devices].soapy.driver_key, sizeof(discovered[devices].soapy.driver_key), "%s",
           driverkey);
  char *hardwarekey = SoapySDRDevice_getHardwareKey(sdr);
  t_print("%s: HardwareKey=%s\n", __FUNCTION__, hardwarekey);
  snprintf(discovered[devices].soapy.hardware_key, sizeof(discovered[devices].soapy.hardware_key), "%s",
           hardwarekey);

  if (strcmp(driver, "sdrplay") == 0) {
    address = hardwarekey;
  }

  SoapySDRKwargs info = SoapySDRDevice_getHardwareInfo(sdr);

  for (size_t i = 0; i < info.size; i++) {
    t_print("%s: hardware info key=%s val=%s\n", __FUNCTION__, info.keys[i], info.vals[i]);

    if (strcmp(info.keys[i], "firmwareVersion") == 0) {
      snprintf(fw_version, sizeof(fw_version), " fw=%s", info.vals[i]);
    }

    if (strcmp(info.keys[i], "gatewareVersion") == 0) {
      snprintf(gw_version, sizeof(gw_version), " gw=%s", info.vals[i]);
      software_version = (int)(atof(info.vals[i]) * 100.0);
    }

    if (strcmp(info.keys[i], "hardwareVersion") == 0) {
      snprintf(hw_version, sizeof(hw_version), " hw=%s", info.vals[i]);
    }

    if (strcmp(info.keys[i], "protocolVersion") == 0) {
      snprintf(p_version, sizeof(p_version), " p=%s", info.vals[i]);
    }
  }

  discovered[devices].software_version = software_version;
  snprintf(discovered[devices].soapy.version, sizeof(discovered[devices].soapy.version),
           "%s%s%s%s", fw_version, gw_version, hw_version, p_version);
  size_t rx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_RX);
  t_print("%s: Rx channels: %ld\n", __FUNCTION__, (long) rx_channels);

  if (rx_channels > 2) {
    t_print("%s: Using only 2 RX channels!", __FUNCTION__);
    rx_channels = 2;
  }

  discovered[devices].soapy.rx_channels = rx_channels;
  discovered[devices].supported_receivers = rx_channels;
  discovered[devices].adcs = rx_channels;
  size_t tx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_TX);
  t_print("%s: Tx channels: %ld\n", __FUNCTION__, (long) tx_channels);

  if (tx_channels > 1) {
    t_print("%s: Using only 1 TX channel!", __FUNCTION__);
    tx_channels = 1;
  }

  discovered[devices].soapy.tx_channels = tx_channels;
  discovered[devices].dacs = tx_channels;
  //
  // IMPORTANT: the sample rate must be a power-of-two multiple of 48k, so the
  //            allowed sample rates are 48k, 96k, 192k, 384k, 768k, 1536k, ...
  //            (higher sample rates are possible too much for little CPUs)
  //
  // This code selects 768k nearly always, with the exception of radioberry (48k) and rtlsdr (15367k),
  // but the list of "exceptions" can be extended.
  //
  sample_rate = 768000;

  if (strcmp(driver, "rtlsdr") == 0) {
    sample_rate = 1536000;
  } else if (strcmp(driver, "radioberry") == 0) {
    sample_rate = 48000;
  }

  t_print("%s: sample_rate selected %d\n", __FUNCTION__, sample_rate);
  discovered[devices].soapy.sample_rate = sample_rate;

  for (size_t id = 0; id < rx_channels; id++) {
    int rc;
    SoapySDRRange range;
    size_t length;
    char **antennas;
    char **gains;
    char **formats;
    SoapySDRRange *ranges;
    double *bandwidths;
    double bw;
    t_print("%s: RX%d full duplex=%d\n", __FUNCTION__,  (int) (id + 1),
            SoapySDRDevice_getFullDuplex(sdr, SOAPY_SDR_RX, id));
    ranges = SoapySDRDevice_getSampleRateRange(sdr, SOAPY_SDR_RX, id, &length);

    for (size_t i = 0; i < length; i++) {
      t_print("%s: RX%d sample rate available: %20.6f -> %20.6f\n", __FUNCTION__,
              (int)(id + 1), ranges[i].minimum, ranges[i].maximum);
    }

    free(ranges);  // allocated within SoapySDR so use free() rather than g_free()
    bandwidths = SoapySDRDevice_listBandwidths(sdr, SOAPY_SDR_RX, id, &length);

    for (size_t i = 0; i < length; i++) {
      t_print("%s: RX%d bandwidth available: %20.6f\n", __FUNCTION__, (int)(id + 1), bandwidths[i]);
    }

    free(bandwidths);  // allocated within SoapySDR so use free() rather than g_free()
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, id, (double) sample_rate);
    bw = SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_RX, id);
    t_print("%s: RX%d: bandwidth selected: %f\n", __FUNCTION__, (int)(id + 1), bw);
    ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_RX, id, &length);

    for (size_t i = 0; i < length; i++) {
      t_print("%s: RX%d freq range [%f MHz -> %f MHz step=%f]\n", __FUNCTION__,
              (int)(id + 1), ranges[i].minimum * 1E-6, ranges[i].maximum * 1E-6, ranges[i].step);
    }

    if (id == 0 && length > 0) {
      //
      // Let the first RX1 frequency range determine the radio frequency range
      //
      discovered[devices].frequency_min = ranges[0].minimum;
      discovered[devices].frequency_max = ranges[0].maximum;
    }

    free(ranges);  // allocated within SoapySDR so use free() rather than g_free()
    antennas = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_RX, id, &length);

    if (length > 8) { length = 8; }

    discovered[devices].soapy.rx[id].antennas = length;

    for (size_t i = 0; i < length; i++) {
      t_print( "%s: RX%d antenna: %s\n", __FUNCTION__, (int)(id + 1), antennas[i]);
      snprintf(discovered[devices].soapy.rx[id].antenna[i], 64, "%s", antennas[i]);
    }

    range = SoapySDRDevice_getGainRange(sdr, SOAPY_SDR_RX, id);
    t_print("%s: RX%d total gain available: %f -> %f step=%f\n", __FUNCTION__,
            (int)(id + 1), range.minimum, range.maximum, range.step);
    discovered[devices].soapy.rx[id].gain_step = range.step;
    discovered[devices].soapy.rx[id].gain_min  = range.minimum;
    discovered[devices].soapy.rx[id].gain_max  = range.maximum;
    gains = SoapySDRDevice_listGains(sdr, SOAPY_SDR_RX, id, &length);

    if (length > 8) { length = 8; }

    discovered[devices].soapy.rx[id].gains = length;

    for (size_t i = 0; i < length; i++) {
      range = SoapySDRDevice_getGainElementRange(sdr, SOAPY_SDR_RX, id, gains[i]);
      t_print("%s: RX%d gain element available: %s, %f -> %f step=%f\n", __FUNCTION__,
              (int)(id + 1), gains[i], range.minimum, range.maximum, range.step);
      snprintf(discovered[devices].soapy.rx[id].gain_elem_name[i], 64, "%s", gains[i]);
      discovered[devices].soapy.rx[id].gain_elem_step[i] = range.step;
      discovered[devices].soapy.rx[id].gain_elem_min[i] = range.minimum;
      discovered[devices].soapy.rx[id].gain_elem_max[i] = range.maximum;
    }

    rc = SoapySDRDevice_hasGainMode(sdr, SOAPY_SDR_RX, id);
    t_print("%s: RX%d has_automatic_gain=%d\n", __FUNCTION__, (int)(id + 1), rc);
    discovered[devices].soapy.rx[id].has_automatic_gain = rc;
    rc = SoapySDRDevice_hasDCOffsetMode(sdr, SOAPY_SDR_RX, id);
    t_print("%s: RX%d has_automatic_dc_offset_correction=%d\n", __FUNCTION__, (int)(id + 1), rc);
    formats = SoapySDRDevice_getStreamFormats(sdr, SOAPY_SDR_RX, id, &length);

    for (size_t i = 0; i < length; i++) {
      t_print( "%s: RX%d format available: %s\n", __FUNCTION__, (int)(id + 1), formats[i]);
    }

    free(formats);  // allocated within SoapySDR so use free() rather than g_free()
  }

  //
  // This code is essentially a duplicate of the RX channel query code
  // and this could be "fused"
  //
  if (tx_channels > 0) {
    SoapySDRRange range;
    size_t length;
    char **antennas;
    char **gains;
    char **formats;
    SoapySDRRange *ranges;
    double *bandwidths;
    double bw;
    t_print("%s: TX full duplex =%d\n", __FUNCTION__,
            SoapySDRDevice_getFullDuplex(sdr, SOAPY_SDR_TX, 0));
    ranges = SoapySDRDevice_getSampleRateRange(sdr, SOAPY_SDR_TX, 0, &length);

    for (size_t i = 0; i < length; i++) {
      t_print("%s: TX sample rate available: %20.6f -> %20.6f\n", __FUNCTION__,
              ranges[i].minimum, ranges[i].maximum);
    }

    free(ranges); // allocated within SoapySDR so use free() rather than g_free()
    bandwidths = SoapySDRDevice_listBandwidths(sdr, SOAPY_SDR_TX, 0, &length);

    for (size_t i = 0; i < length; i++) {
      t_print("%s: TX bandwidth available: %20.6f\n", __FUNCTION__, bandwidths[i]);
    }

    free(bandwidths);  // allocated within SoapySDR so use free() rather than g_free()
    bw = SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_TX, 0);
    t_print("%s: TX bandwidth selected: %f\n", __FUNCTION__, bw);
    ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_TX, 0, &length);

    for (size_t i = 0; i < length; i++) {
      t_print("%s: TX freq range [%f MHz -> %f MHz step=%f]\n", __FUNCTION__,
              ranges[i].minimum * 1E-6, ranges[i].maximum * 1E-6, ranges[i].step);
    }

    antennas = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_TX, 0, &length);

    if (length > 8) { length = 8; }

    discovered[devices].soapy.tx.antennas = length;

    for (size_t i = 0; i < length; i++) {
      t_print( "%s: TX antenna: %s\n", __FUNCTION__, antennas[i]);
      snprintf(discovered[devices].soapy.tx.antenna[i], 64, "%s", antennas[i]);
    }

    range = SoapySDRDevice_getGainRange(sdr, SOAPY_SDR_TX, 0);
    t_print("%s: TX total gain available: %f -> %f step=%f\n", __FUNCTION__, range.minimum, range.maximum, range.step);
    discovered[devices].soapy.tx.gain_step = range.step;
    discovered[devices].soapy.tx.gain_min  = range.minimum;
    discovered[devices].soapy.tx.gain_max  = range.maximum;
    gains = SoapySDRDevice_listGains(sdr, SOAPY_SDR_TX, 0, &length);

    if (length > 8) { length = 8; }

    discovered[devices].soapy.tx.gains = length;

    for (size_t i = 0; i < length; i++) {
      range = SoapySDRDevice_getGainElementRange(sdr, SOAPY_SDR_TX, 0, gains[i]);
      t_print("%s: TX gain element available: %s, %f -> %f step=%f\n", __FUNCTION__,
              gains[i], range.minimum, range.maximum, range.step);
      snprintf(discovered[devices].soapy.tx.gain_elem_name[i], 64, "%s", gains[i]);
      discovered[devices].soapy.tx.gain_elem_step[i] = range.step;
      discovered[devices].soapy.tx.gain_elem_min[i] = range.minimum;
      discovered[devices].soapy.tx.gain_elem_max[i] = range.maximum;
    }

    formats = SoapySDRDevice_getStreamFormats(sdr, SOAPY_SDR_TX, 0, &length);

    for (size_t i = 0; i < length; i++) {
      t_print( "%s: TX format available: %s\n", __FUNCTION__, formats[i]);
    }

    free(formats);  // allocated within SoapySDR so use free() rather than g_free()
  }

  //
  // sensors are there for all channels
  //
  size_t sensors;
  char **sensor = SoapySDRDevice_listSensors(sdr, &sensors);

  for (size_t i = 0; i < sensors; i++) {
    const char *value = SoapySDRDevice_readSensor(sdr, sensor[i]);
    t_print( "%s: Sensor:   %s=%s\n", __FUNCTION__, sensor[i], value);
  }

  if (address != NULL) {
    snprintf(discovered[devices].soapy.address, sizeof(discovered[devices].soapy.address), "%s", address);
  } else {
    snprintf(discovered[devices].soapy.address, sizeof(discovered[devices].soapy.address), "USB");
  }

  t_print("%s: name=%s min=%0.3f MHz max=%0.3f MHz\n", __FUNCTION__, discovered[devices].name,
          discovered[devices].frequency_min * 1E-6,
          discovered[devices].frequency_max * 1E-6);
  devices++;
  SoapySDRDevice_unmake(sdr);
}

void soapy_discovery() {
  size_t length;
  SoapySDRKwargs input_args = {};
  t_print("%s\n", __FUNCTION__);
  rtlsdr_count = 0;
  sdrplay_count = 0;
  SoapySDRKwargs_set(&input_args, "hostname", "pluto.local");
  SoapySDRKwargs *results = SoapySDRDevice_enumerate(&input_args, &length);
  t_print("%s: length=%d\n", __FUNCTION__, (int)length);

  for (size_t i = 0; i < length; i++) {
    for (size_t j = 0; j < results[i].size; j++) {
      if (strcmp(results[i].keys[j], "driver") == 0 && strcmp(results[i].vals[j], "audio") != 0) {
        get_info(results[i].vals[j]);
      }
    }
  }

  SoapySDRKwargsList_clear(results, length);
}
