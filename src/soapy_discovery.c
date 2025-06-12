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
  size_t rx_rates_length, tx_rates_length, rx_gains_length, tx_gains_length, ranges_length, rx_antennas_length,
         tx_antennas_length, rx_bandwidth_length, tx_bandwidth_length;
  SoapySDRKwargs args = {};
  int software_version = 0;
  const char *address = NULL;
  int rtlsdr_val = 0;
  int sdrplay_val = 0;
  char fw_version[32];
  char gw_version[32];
  char hw_version[32];
  char p_version[32];
  t_print("soapy_discovery: get_info: %s\n", driver);
  snprintf(fw_version, sizeof(fw_version), "%s", "");
  snprintf(gw_version, sizeof(gw_version), "%s", "");
  snprintf(hw_version, sizeof(hw_version), "%s", "");
  snprintf( p_version, sizeof( p_version), "%s", "");
  SoapySDRKwargs_set(&args, "driver", driver);

  if (strcmp(driver, "rtlsdr") == 0) {
    char count[16];
    snprintf(count, sizeof(count), "%d", rtlsdr_count);
    SoapySDRKwargs_set(&args, "rtl", count);
    rtlsdr_val = rtlsdr_count;
    rtlsdr_count++;
  } else if (strcmp(driver, "sdrplay") == 0) {
    char label[16];
    snprintf(label, sizeof(label), "SDRplay Dev%d", sdrplay_count);
    SoapySDRKwargs_set(&args, "label", label);
    sdrplay_val = sdrplay_count;
    sdrplay_count++;
  }

  SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
  SoapySDRKwargs_clear(&args);
  software_version = 0;
  char *driverkey = SoapySDRDevice_getDriverKey(sdr);
  t_print("DriverKey=%s\n", driverkey);
  char *hardwarekey = SoapySDRDevice_getHardwareKey(sdr);
  t_print("HardwareKey=%s\n", hardwarekey);

  if (strcmp(driver, "sdrplay") == 0) {
    address = hardwarekey;
  }

  SoapySDRKwargs info = SoapySDRDevice_getHardwareInfo(sdr);

  for (size_t i = 0; i < info.size; i++) {
    t_print("soapy_discovery: hardware info key=%s val=%s\n", info.keys[i], info.vals[i]);

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

  size_t rx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_RX);
  t_print("Rx channels: %ld\n", (long) rx_channels);

  for (size_t i = 0; i < rx_channels; i++) {
    t_print("Rx channel full duplex: channel=%d fullduplex=%d\n", i, SoapySDRDevice_getFullDuplex(sdr, SOAPY_SDR_RX, i));
  }

  size_t tx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_TX);
  t_print("Tx channels: %ld\n", (long) tx_channels);

  for (size_t i = 0; i < tx_channels; i++) {
    t_print("Tx channel full duplex: channel=%d fullduplex=%d\n", i, SoapySDRDevice_getFullDuplex(sdr, SOAPY_SDR_TX, i));
  }

  //
  // IMPORTANT: the sample rate must be a power-of-two multiple of 48k, so the
  //            allowed sample rates are 48k, 96k, 192k, 384k, 768k, 1536k, ...
  //            (higher sample rates are possible too much for little CPUs)
  //
  // This code selects 768k nearly always, with the exception of radioberry (48k) and rtlsdr (15367k),
  // but the list of "exceptions" can be extended.
  //
  int sample_rate = 768000;
  SoapySDRRange *rx_rates = SoapySDRDevice_getSampleRateRange(sdr, SOAPY_SDR_RX, 0, &rx_rates_length);

  for (size_t i = 0; i < rx_rates_length; i++) {
    t_print("RX sample rate available: %20.6f -> %20.6f (%10.6f)\n", rx_rates[i].minimum, rx_rates[i].maximum,
            rx_rates[i].minimum / 48000.0);
  }

  if (strcmp(driver, "rtlsdr") == 0) {
    sample_rate = 1536000;
  } else if (strcmp(driver, "radioberry") == 0) {
    sample_rate = 48000;
  }

  free(rx_rates);
  t_print("sample_rate selected %d\n", sample_rate);

  if (tx_channels > 0) {
    SoapySDRRange *tx_rates = SoapySDRDevice_getSampleRateRange(sdr, SOAPY_SDR_TX, 0, &tx_rates_length);

    for (size_t i = 0; i < tx_rates_length; i++) {
      t_print("TX sample rate available: %20.6f -> %20.6f (%10.6f)\n", tx_rates[i].minimum, tx_rates[i].maximum,
              tx_rates[i].minimum / 48000.0);
    }

    free(tx_rates);
  }

  double *bandwidths = SoapySDRDevice_listBandwidths(sdr, SOAPY_SDR_RX, 0, &rx_bandwidth_length);

  for (size_t i = 0; i < rx_bandwidth_length; i++) {
    t_print("RX bandwidth available: %20.6f\n", bandwidths[i]);
  }

  free(bandwidths);

  if (tx_channels > 0) {
    bandwidths = SoapySDRDevice_listBandwidths(sdr, SOAPY_SDR_TX, 0, &tx_bandwidth_length);

    for (size_t i = 0; i < tx_bandwidth_length; i++) {
      t_print("TX bandwidth available: %20.6f\n", bandwidths[i]);
    }

    free(bandwidths);
  }

  SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, 0, (double) sample_rate);
  double bandwidth = SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_RX, 0);
  t_print("RX1: bandwidth selected: %f\n", bandwidth);

  if (tx_channels > 0) {
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_TX, 0, (double) sample_rate);
    bandwidth = SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_TX, 0);
    t_print("TX0: bandwidth selected: %f\n", bandwidth);
  }

  SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_RX, 0, &ranges_length);

  for (size_t i = 0; i < ranges_length; i++) t_print("RX freq range [%f MHz -> %f MHz step=%f]\n",
      ranges[i].minimum * 1E-6, ranges[i].maximum * 1E-6, ranges[i].step);

  char** rx_antennas = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_RX, 0, &rx_antennas_length);

  for (size_t i = 0; i < rx_antennas_length; i++) { t_print( "RX antenna: %s\n", rx_antennas[i]); }

  char **rx_gains = SoapySDRDevice_listGains(sdr, SOAPY_SDR_RX, 0, &rx_gains_length);
  gboolean has_automatic_gain = SoapySDRDevice_hasGainMode(sdr, SOAPY_SDR_RX, 0);
  t_print("has_automatic_gain=%d\n", has_automatic_gain);
  gboolean has_automatic_dc_offset_correction = SoapySDRDevice_hasDCOffsetMode(sdr, SOAPY_SDR_RX, 0);
  t_print("has_automatic_dc_offset_correction=%d\n", has_automatic_dc_offset_correction);
  size_t formats_length;
  char **formats = SoapySDRDevice_getStreamFormats(sdr, SOAPY_SDR_RX, 0, &formats_length);

  for (size_t i = 0; i < formats_length; i++) { t_print( "RX format available: %s\n", formats[i]); }

  size_t sensors;
  char **sensor = SoapySDRDevice_listSensors(sdr, &sensors);
  gboolean has_temp = FALSE;

  for (size_t i = 0; i < sensors; i++) {
    char *value = SoapySDRDevice_readSensor(sdr, sensor[i]);
    t_print( "Sensor:   %s=%s\n", sensor[i], value);

    if ((strstr(sensor[i], "temp")) != NULL) {
      has_temp = TRUE;
    }
  }

  if (devices < MAX_DEVICES) {
    discovered[devices].device = SOAPYSDR_USB_DEVICE;
    discovered[devices].protocol = SOAPYSDR_PROTOCOL;
    snprintf(discovered[devices].name, sizeof(discovered[devices].name), "%s", driver);
    discovered[devices].supported_receivers = rx_channels;
    discovered[devices].supported_transmitters = tx_channels;
    discovered[devices].adcs = rx_channels;
    discovered[devices].dacs = tx_channels;
    discovered[devices].status = STATE_AVAILABLE;
    discovered[devices].software_version = software_version;
    discovered[devices].frequency_min = ranges[0].minimum;
    discovered[devices].frequency_max = ranges[0].maximum;
    snprintf(discovered[devices].info.soapy.driver_key, sizeof(discovered[devices].info.soapy.driver_key), "%s",
             driverkey);
    snprintf(discovered[devices].info.soapy.hardware_key, sizeof(discovered[devices].info.soapy.hardware_key), "%s",
             hardwarekey);
    discovered[devices].info.soapy.sample_rate = sample_rate;

    if (strcmp(driver, "rtlsdr") == 0) {
      discovered[devices].info.soapy.rtlsdr_count = rtlsdr_val;
      discovered[devices].info.soapy.sdrplay_count = 0;
    } else if (strcmp(driver, "sdrplay") == 0) {
      discovered[devices].info.soapy.rtlsdr_count = 0;
      discovered[devices].info.soapy.sdrplay_count = sdrplay_val;
    } else {
      discovered[devices].info.soapy.rtlsdr_count = 0;
      discovered[devices].info.soapy.sdrplay_count = 0;
    }

    snprintf(discovered[devices].info.soapy.version, sizeof(discovered[devices].info.soapy.version),
             "%s%s%s%s", fw_version, gw_version, hw_version, p_version);

    //
    // Client/Server:
    // The RX/TX gains and antennas are now stored in "radio" in a fixed place,
    // where we have up to 8 strings, each with max length (including trailing zero) of 64
    //
    if (rx_gains_length > 8) { rx_gains_length = 8; }

    if (rx_antennas_length > 8) { rx_antennas_length = 8; }

    discovered[devices].info.soapy.rx_channels = rx_channels;
    discovered[devices].info.soapy.rx_gains = rx_gains_length;
    SoapySDRRange rx_range = SoapySDRDevice_getGainRange(sdr, SOAPY_SDR_RX, 0);
    t_print("RX total gain available: %f -> %f step=%f\n", rx_range.minimum, rx_range.maximum, rx_range.step);
    discovered[devices].info.soapy.rx_gain_step = rx_range.step;
    discovered[devices].info.soapy.rx_gain_min  = rx_range.minimum;
    discovered[devices].info.soapy.rx_gain_max  = rx_range.maximum;

    for (size_t i = 0; i < rx_gains_length; i++) {
      rx_range = SoapySDRDevice_getGainElementRange(sdr, SOAPY_SDR_RX, 0, rx_gains[i]);
      t_print("RX gain element available: %s, %f -> %f step=%f\n", rx_gains[i], rx_range.minimum, rx_range.maximum,
              rx_range.step);
      snprintf(discovered[devices].info.soapy.rx_gain_elem_name[i], 64, "%s", rx_gains[i]);
      discovered[devices].info.soapy.rx_gain_elem_step[i] = rx_range.step;
      discovered[devices].info.soapy.rx_gain_elem_min[i] = rx_range.minimum;
      discovered[devices].info.soapy.rx_gain_elem_max[i] = rx_range.maximum;
    }

    discovered[devices].info.soapy.rx_has_automatic_gain = has_automatic_gain;
    discovered[devices].info.soapy.rx_has_automatic_dc_offset_correction = has_automatic_dc_offset_correction;
    discovered[devices].info.soapy.rx_antennas = rx_antennas_length;
    discovered[devices].info.soapy.tx_channels = tx_channels;

    for (size_t i = 0; i < rx_antennas_length; i++) {
      snprintf(discovered[devices].info.soapy.rx_antenna[i], 64, "%s", rx_antennas[i]);
    }

    if (tx_channels > 0) {
      char **tx_gains = SoapySDRDevice_listGains(sdr, SOAPY_SDR_TX, 0, &tx_gains_length);

      if (tx_gains_length > 8) { tx_gains_length = 8; }

      discovered[devices].info.soapy.tx_gains = tx_gains_length;
      SoapySDRRange tx_range = SoapySDRDevice_getGainRange(sdr, SOAPY_SDR_TX, 0);
      t_print("TX total gain, %f -> %f step=%f\n", tx_range.minimum, tx_range.maximum, tx_range.step);
      discovered[devices].info.soapy.tx_gain_step = tx_range.step;
      discovered[devices].info.soapy.tx_gain_min  = tx_range.minimum;
      discovered[devices].info.soapy.tx_gain_max  = tx_range.maximum;

      for (size_t i = 0; i < tx_gains_length; i++) {
        tx_range = SoapySDRDevice_getGainElementRange(sdr, SOAPY_SDR_TX, 0, tx_gains[i]);
        t_print("TX gain element %s, %f -> %f step=%f\n", tx_gains[i], tx_range.minimum, tx_range.maximum, tx_range.step);
        snprintf(discovered[devices].info.soapy.tx_gain_elem_name[i], 64, "%s", tx_gains[i]);
        discovered[devices].info.soapy.tx_gain_elem_step[i] = tx_range.step;
        discovered[devices].info.soapy.tx_gain_elem_min[i] = tx_range.minimum;
        discovered[devices].info.soapy.tx_gain_elem_max[i] = tx_range.maximum;
      }

      char **tx_antennas = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_TX, 0, &tx_antennas_length);

      if (tx_antennas_length > 8) { tx_antennas_length = 8; }

      discovered[devices].info.soapy.tx_antennas = tx_antennas_length;

      for (size_t i = 0; i < tx_antennas_length; i++) {
        t_print( "TX antenna: %s\n", tx_antennas[i]);
        snprintf(discovered[devices].info.soapy.tx_antenna[i], 64, "%s", tx_antennas[i]);
      }
    }

    discovered[devices].info.soapy.sensors = sensors;
    discovered[devices].info.soapy.sensor = sensor;
    discovered[devices].info.soapy.has_temp = has_temp;

    if (address != NULL) {
      snprintf(discovered[devices].info.soapy.address, sizeof(discovered[devices].info.soapy.address), "%s", address);
    } else {
      snprintf(discovered[devices].info.soapy.address, sizeof(discovered[devices].info.soapy.address), "USB");
    }

    t_print("soapy_discovery: name=%s min=%0.3f MHz max=%0.3f MHz\n", discovered[devices].name,
            discovered[devices].frequency_min * 1E-6,
            discovered[devices].frequency_max * 1E-6);
    devices++;
  }

  SoapySDRDevice_unmake(sdr);
  free(ranges);
}

void soapy_discovery() {
  size_t length;
  SoapySDRKwargs input_args = {};
  t_print("%s\n", __FUNCTION__);
  rtlsdr_count = 0;
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
