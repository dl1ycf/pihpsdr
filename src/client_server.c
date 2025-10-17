/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

/*
 * Some general remarks to the client-server model.
 *
 * Server = piHPSDR running on a "local" attached to the radio
 * Client = piHPSDR running on a "remote" computer without a radio
 *
 * The server starts after all data has been initialised and the props file has
 * been read, and then sends out all this data to the client.
 *
 * It then periodically sends audio (INFO_RXAUDIO) and pixel (INFO_SPECTRUM) data,
 * the latter is used both for the panadapter and the waterfall.
 *
 * On the client side, a packet is sent if the user changes the state (e.g. via
 * a menu). Take, for example, the case that a noise reduction setting/parameter
 * is changed.
 *
 * The client never calls WDSP functions, instead in this case it calls send_noise()
 * which sends a CMD_NOISE packet to the server.
 *
 * If the server receives this packet, it stores the noise reduction settings
 * contained therein in its internal data structures and applies them by calling
 * WDSP functions. In addition, it calls send_rx_data() which contains all the
 * receiver data. In some cases, more packets have to be sent back. In case of
 * a mode change, this can be receiver, transmitter, and VFO data.
 *
 * On the client side, such data is simply stored but no action takes place.
 *
 * Most packets are sent from the GTK queue, but audio data is sent directly from
 * the receive thread, so we need a mutex in send_bytes(). It is important that
 * a packet (that is, a bunch of data that belongs together) is sent in a single
 * call to send_bytes().
 */

/*
 * This file contains the functions for sending data, or used both by the client
 * and by the server
 */

#include <gtk/gtk.h>

#include <openssl/sha.h>

#include "band.h"
#include "client_server.h"
#include "filter.h"
#include "message.h"
#include "radio.h"
#include "store.h"
#include "vfo.h"

//
// From a challenge in s, calculate a password hash with "loop and salt".
// The number of bytes in the challenge equals the length of a SHA512
// hash. Note the length of the hash is the "digest length", and the
// maximum length of the string to be hashed is 2*digest_length + 4
// passwords are chopped to a max length of the digest length.
//
// Note: digest length for SHA512 is 64 bytes.
//
void generate_pwd_hash(unsigned char *s, unsigned char *hash, const char *pwd) {
  int pwdlen = strlen(pwd);

  if (pwdlen > SHA512_DIGEST_LENGTH) { pwdlen = SHA512_DIGEST_LENGTH; }

  //
  // Add password to the challenge
  //
  for  (int i = 0; i < pwdlen; i++) {
    s[SHA512_DIGEST_LENGTH + i] = pwd[i];
  }

  //
  // Initial hash
  //
  SHA512(s, SHA512_DIGEST_LENGTH + pwdlen, hash);

  //
  // Looping: repeatedly, generate a new string of the form
  // hash + pwd and re-calculate the hash
  //
  for (int i = 0; i < 99999; i++) {
    for (int j = 0; j < SHA512_DIGEST_LENGTH; j++) {
      s[j] = hash[j];
    }

    for  (int j = 0; j < pwdlen; j++) {
      s[SHA512_DIGEST_LENGTH + j] = pwd[j];
    }

    //
    // New hash
    //
    SHA512(s, SHA512_DIGEST_LENGTH + pwdlen, hash);
  }
}

int recv_bytes(int s, char *buffer, int bytes) {
  int bytes_read = 0;
  int count = 0;

  while (bytes_read != bytes) {
    int rc = recv(s, &buffer[bytes_read], bytes - bytes_read, 0);

    //
    //  On LINUX (not MacOS), if the client suffered sudden death, the recv()
    //  will endlessly return with zero. So make sure that after 10 bunches
    //  we will return
    //
    if (rc < 0 || ++count >= 10) {
      // return -1, so we need not check downstream
      // on incomplete messages received
      t_print("%s: read %d bytes, but expected %d.\n", __FUNCTION__, bytes_read, bytes);
      bytes_read = -1;
      t_perror("recv_bytes");

      if (!radio_is_remote) {
        //
        // This is the server. Note client's death.
        //
        remoteclient.running = FALSE;
      }

      break;
    } else {
      bytes_read += rc;
    }
  }

  return bytes_read;
}

//
// This function is called from within the GTK queue but
// also from the receive thread (via remote_rxaudio).
// To make this bullet proof, we need a mutex here in case a
// remote_rxaudio occurs while sending another packet.
//
int send_bytes(int s, char *buffer, int bytes) {
  static GMutex send_mutex;  // static so correctly initialised
  int bytes_sent = 0;

  if (s < 0) { return -1; }

  g_mutex_lock(&send_mutex);

  while (bytes_sent != bytes) {
    int rc = send(s, &buffer[bytes_sent], bytes - bytes_sent, 0);

    if (rc < 0) {
      // return -1, so we need not check downstream
      // on incomplete messages sent
      t_print("%s: sent %d bytes, but tried %d.\n", __FUNCTION__, bytes_sent, bytes);
      bytes_sent = -1;
      t_perror("send_bytes");

      if (!radio_is_remote) {
        //
        // This is the server. Note client's death.
        //
        remoteclient.running = FALSE;
      }

      break;
    } else {
      bytes_sent += rc;
    }
  }

  g_mutex_unlock(&send_mutex);
  return bytes_sent;
}

void send_start_radio(int sock) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_START_RADIO);
  send_bytes(sock, (char *)&header, sizeof(HEADER));
}

void send_rxmenu(int sock, int id) {
  RXMENU_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(CMD_RXMENU);
  data.id = id;
  data.dither = adc[id].dither;
  data.random = adc[id].random;
  data.preamp = adc[id].preamp;
  data.alex_attenuation = adc[id].alex_attenuation;
  data.adc0_filter_bypass = adc[0].filter_bypass;
  data.adc1_filter_bypass = adc[1].filter_bypass;
  send_bytes(sock, (char *)&data, sizeof(RXMENU_DATA));
}

void send_radiomenu(int sock) {
  RADIOMENU_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(CMD_RADIOMENU);
  data.mic_ptt_tip_bias_ring = mic_ptt_tip_bias_ring;
  data.sat_mode = sat_mode;
  data.mic_input_xlr = mic_input_xlr;
  data.atlas_clock_source_10mhz = atlas_clock_source_10mhz;
  data.atlas_clock_source_128mhz = atlas_clock_source_128mhz;
  data.atlas_mic_source = atlas_mic_source;
  data.atlas_penelope = atlas_penelope;
  data.atlas_janus = atlas_janus;
  data.mic_ptt_enabled = mic_ptt_enabled;
  data.mic_bias_enabled = mic_bias_enabled;
  data.pa_enabled = pa_enabled;
  data.mute_spkr_amp = mute_spkr_amp;
  data.mute_spkr_xmit = mute_spkr_xmit;
  data.hl2_audio_codec = hl2_audio_codec;
  data.soapy_iqswap = soapy_iqswap;
  data.enable_tx_inhibit = enable_tx_inhibit;
  data.enable_auto_tune = enable_auto_tune;
  data.new_pa_board = new_pa_board;
  data.tx_out_of_band_allowed = tx_out_of_band_allowed;
  data.OCtune = OCtune;
  //
  data.rx_gain_calibration = to_16(rx_gain_calibration);
  data.OCfull_tune_time = to_16(OCfull_tune_time);
  data.OCmemory_tune_time = to_16(OCmemory_tune_time);
  //
  data.frequency_calibration = to_64(frequency_calibration);
  send_bytes(sock, (char *)&data, sizeof(RADIOMENU_DATA));
}

void send_memory_data(int sock, int index) {
  MEMORY_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(INFO_MEMORY);
  data.index              = index;
  data.sat_mode           = mem[index].sat_mode;
  data.ctun               = mem[index].ctun;
  data.mode               = mem[index].mode;
  data.filter             = mem[index].filter;
  data.bd                 = mem[index].bd;
  data.alt_ctun           = mem[index].alt_ctun;
  data.alt_mode           = mem[index].alt_mode;
  data.alt_filter         = mem[index].alt_filter;
  data.alt_bd             = mem[index].alt_bd;
  data.ctcss_enabled      = mem[index].ctcss_enabled;
  data.ctcss              = mem[index].ctcss;
  //
  data.deviation          = to_16(mem[index].deviation);
  data.alt_deviation      = to_16(mem[index].alt_deviation);
  //
  data.frequency          = to_64(mem[index].frequency);
  data.ctun_frequency     = to_64(mem[index].ctun_frequency);
  data.alt_frequency      = to_64(mem[index].frequency);
  data.alt_ctun_frequency = to_64(mem[index].ctun_frequency);
  send_bytes(sock, (char *)&data, sizeof(MEMORY_DATA));
}

void send_band_data(int sock, int b) {
  BAND_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(INFO_BAND);
  BAND *band = band_get_band(b);
  snprintf(data.title, sizeof(data.title), "%s", band->title);
  data.band = b;
  data.OCrx = band->OCrx;
  data.OCtx = band->OCtx;
  data.RxAntenna = band->RxAntenna;
  data.TxAntenna = band->TxAntenna;
  data.disablePA = band->disablePA;
  data.current = band->bandstack->current_entry;
  data.gaincalib = to_16(band->gaincalib);
  data.pa_calibration = to_double(band->pa_calibration);
  data.frequencyMin = to_64(band->frequencyMin);
  data.frequencyMax = to_64(band->frequencyMax);
  data.frequencyLO = to_64(band->frequencyLO);
  data.errorLO = to_64(band->errorLO);
  send_bytes(sock, (char *)&data, sizeof(BAND_DATA));
}

void send_xvtr_changed(int sock) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_XVTR);
  send_bytes(sock, (char *)&header, sizeof(HEADER));
}

void send_bandstack_data(int sock, int b, int stack) {
  BANDSTACK_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(INFO_BANDSTACK);
  BAND *band = band_get_band(b);
  BANDSTACK_ENTRY *entry = band->bandstack->entry;
  entry += stack;
  data.band = b;
  data.stack = stack;
  data.mode = entry->mode;
  data.filter = entry->filter;
  data.ctun = entry->ctun;
  data.ctcss_enabled = entry->ctcss_enabled;
  data.ctcss = entry->ctcss;
  data.deviation = to_16(entry->deviation);
  data.frequency = to_64(entry->frequency);
  data.ctun_frequency = to_64(entry->ctun_frequency);
  send_bytes(sock, (char *)&data, sizeof(BANDSTACK_DATA));
}

void send_radio_data(int sock) {
  RADIO_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(INFO_RADIO);
  snprintf(data.name, sizeof(data.name), "%s", radio->name);
  data.locked = locked;
  data.protocol = protocol;
  data.supported_receivers = radio->supported_receivers;
  data.receivers = receivers;
  data.filter_board = filter_board;
  data.enable_auto_tune = enable_auto_tune;
  data.new_pa_board = new_pa_board;
  data.region = region;
  data.atlas_penelope = atlas_penelope;
  data.atlas_clock_source_10mhz = atlas_clock_source_10mhz;
  data.atlas_clock_source_128mhz = atlas_clock_source_128mhz;
  data.atlas_mic_source = atlas_mic_source;
  data.atlas_janus = atlas_janus;
  data.hl2_audio_codec = hl2_audio_codec;
  data.anan10E = anan10E;
  data.tx_out_of_band_allowed = tx_out_of_band_allowed;
  data.pa_enabled = pa_enabled;
  data.mic_boost = mic_boost;
  data.mic_linein = mic_linein;
  data.mic_ptt_enabled = mic_ptt_enabled;
  data.mic_bias_enabled = mic_bias_enabled;
  data.mic_ptt_tip_bias_ring = mic_ptt_tip_bias_ring;
  data.mic_input_xlr = mic_input_xlr;
  data.cw_keyer_sidetone_volume = cw_keyer_sidetone_volume;
  data.OCtune = OCtune;
  data.mute_rx_while_transmitting = mute_rx_while_transmitting;
  data.mute_spkr_amp = mute_spkr_amp;
  data.mute_spkr_xmit = mute_spkr_xmit;
  data.split = split;
  data.sat_mode = sat_mode;
  data.duplex = duplex;
  data.have_rx_gain = have_rx_gain;
  data.have_rx_att = have_rx_att;
  data.have_alex_att = have_alex_att;
  data.have_preamp = have_preamp;
  data.have_dither = have_dither;
  data.have_saturn_xdma = have_saturn_xdma;
  data.rx_stack_horizontal = rx_stack_horizontal;
  data.n_adc = n_adc;
  data.diversity_enabled = diversity_enabled;
  data.soapy_iqswap = soapy_iqswap;
  data.soapy_rx1_antennas = radio->soapy.rx[0].antennas;
  data.soapy_rx2_antennas = radio->soapy.rx[1].antennas;
  data.soapy_tx_antennas = radio->soapy.tx.antennas;
  data.soapy_rx1_gains = radio->soapy.rx[0].gains;
  data.soapy_rx2_gains = radio->soapy.rx[1].gains;
  data.soapy_tx_gains = radio->soapy.tx.gains;
  data.soapy_tx_channels = radio->soapy.tx_channels;
  data.soapy_rx1_has_automatic_gain = radio->soapy.rx[0].has_automatic_gain;
  data.soapy_rx2_has_automatic_gain = radio->soapy.rx[1].has_automatic_gain;
  //
  memcpy(data.soapy_hardware_key, radio->soapy.hardware_key, 64);
  memcpy(data.soapy_driver_key, radio->soapy.driver_key, 64);

  for (int i = 0; i < radio->soapy.rx[0].antennas; i++) {
    memcpy(data.soapy_rx1_antenna[i], radio->soapy.rx[0].antenna[i], 64);
  }

  for (int i = 0; i < radio->soapy.rx[1].antennas; i++) {
    memcpy(data.soapy_rx2_antenna[i], radio->soapy.rx[1].antenna[i], 64);
  }

  for (int i = 0; i < radio->soapy.tx.antennas; i++) {
    memcpy(data.soapy_tx_antenna[i], radio->soapy.tx.antenna[i], 64);
  }

  for (int i = 0; i < radio->soapy.rx[0].gains; i++) {
    memcpy(data.soapy_rx1_gain_elem_name[i], radio->soapy.rx[0].gain_elem_name[i], 64);
  }

  for (int i = 0; i < radio->soapy.rx[1].gains; i++) {
    memcpy(data.soapy_rx2_gain_elem_name[i], radio->soapy.rx[1].gain_elem_name[i], 64);
  }

  for (int i = 0; i < radio->soapy.tx.gains; i++) {
    memcpy(data.soapy_tx_gain_elem_name[i], radio->soapy.tx.gain_elem_name[i], 64);
  }

  //
  data.pa_power = to_16(pa_power);
  data.OCfull_tune_time = to_16(OCfull_tune_time);
  data.OCmemory_tune_time = to_16(OCmemory_tune_time);
  data.cw_keyer_sidetone_frequency = to_16(cw_keyer_sidetone_frequency);
  data.rx_gain_calibration = to_16(rx_gain_calibration);
  data.device = to_16(device);
  //
  data.drive_min = to_double(drive_min);
  data.drive_max = to_double(drive_max);
  data.drive_digi_max = to_double(drive_digi_max);
  data.div_gain = to_double(div_gain);
  data.div_phase = to_double(div_phase);

  for (int i = 0; i < 11; i++) {
    data.pa_trim[i] = to_double(pa_trim[i]);
  }

  data.soapy_rx1_gain_step = to_double(radio->soapy.rx[0].gain_step);
  data.soapy_rx1_gain_min  = to_double(radio->soapy.rx[0].gain_min );
  data.soapy_rx1_gain_max  = to_double(radio->soapy.rx[0].gain_max );
  data.soapy_rx2_gain_step = to_double(radio->soapy.rx[1].gain_step);
  data.soapy_rx2_gain_min  = to_double(radio->soapy.rx[1].gain_min );
  data.soapy_rx2_gain_max  = to_double(radio->soapy.rx[1].gain_max );
  data.soapy_tx_gain_step = to_double(radio->soapy.tx.gain_step);
  data.soapy_tx_gain_min  = to_double(radio->soapy.tx.gain_min );
  data.soapy_tx_gain_max  = to_double(radio->soapy.tx.gain_max );

  for (int i = 0; i < radio->soapy.rx[0].gains; i++) {
    data.soapy_rx1_gain_elem_step[i] = to_double(radio->soapy.rx[0].gain_elem_step[i]);
    data.soapy_rx1_gain_elem_min[i] = to_double(radio->soapy.rx[0].gain_elem_min[i]);
    data.soapy_rx1_gain_elem_max[i] = to_double(radio->soapy.rx[0].gain_elem_max[i]);
  }

  for (int i = 0; i < radio->soapy.rx[1].gains; i++) {
    data.soapy_rx2_gain_elem_step[i] = to_double(radio->soapy.rx[1].gain_elem_step[i]);
    data.soapy_rx2_gain_elem_min[i] = to_double(radio->soapy.rx[1].gain_elem_min[i]);
    data.soapy_rx2_gain_elem_max[i] = to_double(radio->soapy.rx[1].gain_elem_max[i]);
  }

  for (int i = 0; i < radio->soapy.tx.gains; i++) {
    data.soapy_tx_gain_elem_step[i] = to_double(radio->soapy.tx.gain_elem_step[i]);
    data.soapy_tx_gain_elem_min[i] = to_double(radio->soapy.tx.gain_elem_min[i]);
    data.soapy_tx_gain_elem_max[i] = to_double(radio->soapy.tx.gain_elem_max[i]);
  }

  //
  data.frequency_calibration = to_64(frequency_calibration);
  data.soapy_radio_sample_rate = to_32(soapy_radio_sample_rate);
  data.radio_frequency_min = to_64(radio->frequency_min);
  data.radio_frequency_max = to_64(radio->frequency_max);
  send_bytes(sock, (char *)&data, sizeof(RADIO_DATA));
}

void send_adc_data(int sock, int i) {
  ADC_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(INFO_ADC);
  data.adc = i;
  data.preamp = adc[i].preamp;
  data.dither = adc[i].dither;
  data.random = adc[i].random;
  data.antenna = adc[i].antenna;
  data.alex_attenuation = adc[i].alex_attenuation;
  data.filter_bypass = adc[i].filter_bypass;
  data.attenuation = to_16(adc[i].attenuation);
  data.gain = to_double(adc[i].gain);
  data.min_gain = to_double(adc[i].min_gain);
  data.max_gain = to_double(adc[i].max_gain);
  send_bytes(sock, (char *)&data, sizeof(ADC_DATA));
}

void send_tx_data(int sock) {
  if (can_transmit) {
    TRANSMITTER_DATA data;
    const TRANSMITTER *tx = transmitter;
    SYNC(data.header.sync);
    data.header.data_type = to_16(INFO_TRANSMITTER);
    //
    data.id = tx->id;
    data.dac = tx->dac;
    data.display_detector_mode = tx->display_detector_mode;
    data.display_average_mode = tx->display_average_mode;
    data.use_rx_filter = tx->use_rx_filter;
    data.antenna = tx->antenna;
    data.puresignal = tx->puresignal;
    data.feedback = tx->feedback;
    data.auto_on = tx->auto_on;
    data.ps_oneshot = tx->ps_oneshot;
    data.ctcss_enabled = tx->ctcss_enabled;
    data.ctcss = tx->ctcss;
    data.pre_emphasize = tx->pre_emphasize;
    data.drive = tx->drive;
    data.tune_use_drive = tx->tune_use_drive;
    data.tune_drive = tx->tune_drive;
    data.compressor = tx->compressor;
    data.cfc = tx->cfc;
    data.cfc_eq = tx->cfc_eq;
    data.dexp = tx->dexp;
    data.dexp_filter = tx->dexp_filter;
    data.eq_enable = tx->eq_enable;
    data.alcmode = tx->alcmode;
    data.swr_protection = tx->swr_protection;
    //
    data.fps = to_16(tx->fps);
    data.dexp_filter_low = to_16(tx->dexp_filter_low);
    data.dexp_filter_high = to_16(tx->dexp_filter_high);
    data.dexp_trigger = to_16(tx->dexp_trigger);
    data.dexp_exp = to_16(tx->dexp_exp);
    data.filter_low = to_16(tx->filter_low);
    data.filter_high = to_16(tx->filter_high);
    data.deviation = to_16(tx->deviation);
    data.width = to_16(tx->width);
    data.height = to_16(tx->height);
    data.attenuation = to_16(tx->attenuation);
    data.tx_default_filter_low = to_16(tx->default_filter_low);
    data.tx_default_filter_high = to_16(tx->default_filter_high);
    //
    data.fft_size = to_32(tx->fft_size);

    //
    for (int i = 0; i < 11; i++) {
      data.eq_freq[i] =  to_double(tx->eq_freq[i]);
      data.eq_gain[i] =  to_double(tx->eq_gain[i]);
      data.cfc_freq[i] =  to_double(tx->cfc_freq[i]);
      data.cfc_lvl[i] =  to_double(tx->cfc_lvl[i]);
      data.cfc_post[i] =  to_double(tx->cfc_post[i]);
    }

    data.swr_alarm = to_double(tx->swr_alarm);
    data.dexp_tau =  to_double(tx->dexp_tau);
    data.dexp_attack =  to_double(tx->dexp_attack);
    data.dexp_release =  to_double(tx->dexp_release);
    data.dexp_hold =  to_double(tx->dexp_hold);
    data.dexp_hyst =  to_double(tx->dexp_hyst);
    data.mic_gain =  to_double(tx->mic_gain);
    data.compressor_level =  to_double(tx->compressor_level);
    data.am_carrier_level = to_double(tx->am_carrier_level);
    data.display_average_time =  to_double(tx->display_average_time);
    data.ps_ampdelay =  to_double(tx->ps_ampdelay);
    data.ps_moxdelay =  to_double(tx->ps_moxdelay);
    data.ps_loopdelay =  to_double(tx->ps_loopdelay);
    send_bytes(sock, (char *)&data, sizeof(TRANSMITTER_DATA));
  }
}

void send_rx_data(int sock, int id) {
  RECEIVER_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_16(INFO_RECEIVER);
  const RECEIVER *rx = receiver[id];
  data.id                    = rx->id;
  data.adc                   = rx->adc;
  data.agc                   = rx->agc;
  data.nb                    = rx->nb;
  data.nb2_mode              = rx->nb2_mode;
  data.nr                    = rx->nr;
  data.nr_agc                = rx->nr_agc;
  data.nr2_gain_method       = rx->nr2_gain_method;
  data.nr2_npe_method        = rx->nr2_npe_method;
  data.nr2_post              = rx->nr2_post;
  data.nr2_post_taper        = rx->nr2_post_taper;
  data.nr2_post_nlevel       = rx->nr2_post_nlevel;
  data.nr2_post_factor       = rx->nr2_post_factor;
  data.nr2_post_rate         = rx->nr2_post_rate;
  data.anf                   = rx->anf;
  data.snb                   = rx->snb;
  data.display_detector_mode = rx->display_detector_mode;
  data.display_average_mode  = rx->display_average_mode;
  data.zoom                  = rx->zoom;
  data.squelch_enable        = rx->squelch_enable;
  data.binaural              = rx->binaural;
  data.eq_enable             = rx->eq_enable;
  data.smetermode            = rx->smetermode;
  data.low_latency           = rx->low_latency;
  data.pan                   = rx->pan;
  //
  data.fps                   = to_16(rx->fps);
  data.filter_low            = to_16(rx->filter_low);
  data.filter_high           = to_16(rx->filter_high);
  data.deviation             = to_16(rx->deviation);
  data.width                 = to_16(rx->width);
  //
  data.cA                    = to_double(rx->cA);
  data.cB                    = to_double(rx->cB);
  data.cAp                   = to_double(rx->cAp);
  data.cBp                   = to_double(rx->cBp);
  data.squelch               = to_double(rx->squelch);
  data.display_average_time  = to_double(rx->display_average_time);
  data.volume                = to_double(rx->volume);
  data.agc_gain              = to_double(rx->agc_gain);
  data.agc_hang              = to_double(rx->agc_hang);
  data.agc_thresh            = to_double(rx->agc_thresh);
  data.agc_hang_threshold    = to_double(rx->agc_hang_threshold);
  data.nr2_trained_threshold = to_double(rx->nr2_trained_threshold);
  data.nr2_trained_t2        = to_double(rx->nr2_trained_t2);
  data.nb_tau                = to_double(rx->nb_tau);
  data.nb_hang               = to_double(rx->nb_hang);
  data.nb_advtime            = to_double(rx->nb_advtime);
  data.nb_thresh             = to_double(rx->nb_thresh);
#ifdef EXTNR
  data.nr4_reduction_amount  = to_double(rx->nr4_reduction_amount);
  data.nr4_smoothing_factor  = to_double(rx->nr4_smoothing_factor);
  data.nr4_whitening_factor  = to_double(rx->nr4_whitening_factor);
  data.nr4_noise_rescale     = to_double(rx->nr4_noise_rescale);
  data.nr4_post_threshold    = to_double(rx->nr4_post_threshold);
#else
  //
  // If this side is not compiled with EXTNR, fill in default values
  //
  data.nr4_reduction_amount  = to_double(10.0);
  data.nr4_smoothing_factor  = to_double(0.0);
  data.nr4_whitening_factor  = to_double(0.0);
  data.nr4_noise_rescale     = to_double(2.0);
  data.nr4_post_threshold    = to_double(-10.0);
#endif

  for (int i = 0; i < 11; i++) {
    data.eq_freq[i]          = to_double(rx->eq_freq[i]);
    data.eq_gain[i]          = to_double(rx->eq_gain[i]);
  }

  data.fft_size              = to_32(rx->fft_size);
  data.sample_rate           = to_32(rx->sample_rate);
  send_bytes(sock, (char *)&data, sizeof(RECEIVER_DATA));
}

void send_vfo_data(int sock, int v) {
  VFO_DATA vfo_data;
  SYNC(vfo_data.header.sync);
  vfo_data.header.data_type = to_16(INFO_VFO);
  vfo_data.vfo = v;
  vfo_data.band = vfo[v].band;
  vfo_data.bandstack = vfo[v].bandstack;
  vfo_data.mode = vfo[v].mode;
  vfo_data.filter = vfo[v].filter;
  vfo_data.ctun = vfo[v].ctun;
  vfo_data.rit_enabled = vfo[v].rit_enabled;
  vfo_data.xit_enabled = vfo[v].xit_enabled;
  vfo_data.cwAudioPeakFilter = vfo[v].cwAudioPeakFilter;
  //
  vfo_data.rit_step  = to_16(vfo[v].rit_step);
  vfo_data.deviation = to_16(vfo[v].deviation);
  //
  vfo_data.frequency = to_64(vfo[v].frequency);
  vfo_data.ctun_frequency = to_64(vfo[v].ctun_frequency);
  vfo_data.rit = to_64(vfo[v].rit);
  vfo_data.xit = to_64(vfo[v].xit);
  vfo_data.lo = to_64(vfo[v].lo);
  vfo_data.offset = to_64(vfo[v].offset);
  vfo_data.step   = to_64(vfo[v].step);
  send_bytes(sock, (char *)&vfo_data, sizeof(vfo_data));
}


void send_startstop_rxspectrum(int s, int id, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RX_SPECTRUM);
  header.b1 = id;
  header.b2 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_startstop_txspectrum(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TX_SPECTRUM);
  header.b2 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_frequency(int s, int v, long long hz) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_FREQ);
  command.header.b1 = v;
  command.u64 = to_64(hz);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_vfo_move_to(int s, int id, long long hz, int round) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_MOVETO);
  command.header.b1 = id;
  command.header.b2 = round;
  command.u64 = to_64(hz);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_store(int s, int index) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_STORE);
  header.b1 = index;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_restart(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RESTART);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_recall(int s, int index) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RCL);
  header.b1 = index;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_stepsize(int s, int v, int stepsize) {
  U32_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_VFO_STEPSIZE);
  command.header.b1 = v;
  command.u32 = to_32(stepsize);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_vfo_step(int s, int v, int steps) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_STEP);
  header.b1 = v;
  header.s1 = to_16(steps);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_zoom(int s, const RECEIVER *rx) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_ZOOM);
  header.b1 = rx->id;
  header.b2 = rx->zoom;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_meter(int s, int metermode, int alcmode) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_METER);
  header.b1 = metermode;
  header.b2 = alcmode;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_screen(int s, int hstack, int width) {
  //
  // The client sends this, if its screen size or the RX stacking
  // has changed. The server needs to re-init the WDSP analyzers
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_SCREEN);
  header.b1 = hstack;
  header.s1 = to_16(width);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_pan(int s, const RECEIVER *rx) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_PAN);
  header.b1 = rx->id;
  header.b2 = rx->pan;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_drive(int s, double value) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_DRIVE);
  command.dbl = to_double(value);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_micgain(int s, double gain) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_MICGAIN);
  command.dbl = to_double(gain);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_volume(int s, int id, double volume) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_VOLUME);
  command.header.b1 = id;
  command.dbl = to_double(volume);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_diversity(int s, int enabled, double gain, double phase) {
  DIVERSITY_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_DIVERSITY);
  command.diversity_enabled = enabled;
  command.div_gain = to_double(gain);
  command.div_phase =  to_double(phase);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_agc(int s, int id, int agc) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_AGC);
  header.b1 = id;
  header.b2 = agc;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_agc_gain(int s, const RECEIVER *rx) {
  AGC_GAIN_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_AGC_GAIN);
  command.id = rx->id;
  command.gain = to_double(rx->agc_gain);
  command.hang = to_double(rx->agc_hang);
  command.thresh = to_double(rx->agc_thresh);
  command.hang_thresh = to_double(rx->agc_hang_threshold);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_rfgain(int s, int id, double gain) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_RFGAIN);
  command.header.b1 = id;
  command.dbl = to_double(gain);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_attenuation(int s, int id, int attenuation) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_ATTENUATION);
  header.b1 = id;
  header.s1 = to_16(attenuation);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_squelch(int s, int id, int enable, double squelch) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_SQUELCH);
  command.header.b1 = id;
  command.header.b2 = enable;
  command.dbl = to_double(squelch);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_eq(int s, int id) {
  //
  // The client sends this whenever an equaliser is changed
  //
  EQUALIZER_COMMAND command;
  SYNC(command.header.sync);
  command.id     = id;

  if (id < RECEIVERS) {
    const RECEIVER *rx = receiver[id];
    command.header.data_type = to_16(CMD_RX_EQ);
    command.enable = rx->eq_enable;

    for (int i = 0; i < 11; i++) {
      command.freq[i] = to_double(rx->eq_freq[i]);
      command.gain[i] = to_double(rx->eq_gain[i]);
    }
  } else if (id == 8) {
    command.header.data_type = to_16(CMD_TX_EQ);

    if (can_transmit) {
      command.enable = transmitter->eq_enable;

      for (int i = 0; i < 11; i++) {
        command.freq[i] = to_double(transmitter->eq_freq[i]);
        command.gain[i] = to_double(transmitter->eq_gain[i]);
      }
    }
  }

  send_bytes(s, (char *)&command, sizeof(command));
}

void send_noise(int s, const RECEIVER *rx) {
  //
  // The client sends this whenever a noise reduction
  // setting is changed.
  //
  NOISE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_NOISE);
  command.id                        = rx->id;
  command.nb                        = rx->nb;
  command.nr                        = rx->nr;
  command.anf                       = rx->anf;
  command.snb                       = rx->snb;
  command.nb2_mode                  = rx->nb2_mode;
  command.nr_agc                    = rx->nr_agc;
  command.nr2_gain_method           = rx->nr2_gain_method;
  command.nr2_npe_method            = rx->nr2_npe_method;
  command.nr2_post                  = rx->nr2_post;
  command.nr2_post_taper            = rx->nr2_post_taper;
  command.nr2_post_nlevel           = rx->nr2_post_nlevel;
  command.nr2_post_factor           = rx->nr2_post_factor;
  command.nr2_post_rate             = rx->nr2_post_rate;
  command.nb_tau                    = to_double(rx->nb_tau);
  command.nb_hang                   = to_double(rx->nb_hang);
  command.nb_advtime                = to_double(rx->nb_advtime);
  command.nb_thresh                 = to_double(rx->nb_thresh);
  command.nr2_trained_threshold     = to_double(rx->nr2_trained_threshold);
  command.nr2_trained_t2            = to_double(rx->nr2_trained_t2);
#ifdef EXTNR
  command.nr4_reduction_amount      = to_double(rx->nr4_reduction_amount);
  command.nr4_smoothing_factor      = to_double(rx->nr4_smoothing_factor);
  command.nr4_whitening_factor      = to_double(rx->nr4_whitening_factor);
  command.nr4_noise_rescale         = to_double(rx->nr4_noise_rescale);
  command.nr4_post_threshold        = to_double(rx->nr4_post_threshold);
#else
  //
  // If this side is not compiled with EXTNR, fill in default values
  //
  command.nr4_reduction_amount      = to_double(10.0);
  command.nr4_smoothing_factor      = to_double(0.0);
  command.nr4_whitening_factor      = to_double(0.0);
  command.nr4_noise_rescale         = to_double(2.0);
  command.nr4_post_threshold        = to_double(-10.0);
#endif
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_replay(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_REPLAY);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_capture(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_CAPTURE);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_bandstack(int s, int old, int new) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_BANDSTACK);
  header.b1 = old;
  header.b2 = new;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_band(int s, int v, int band) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_BAND_SEL);
  header.b1 = v;
  header.b2 = band;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_twotone(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TWOTONE);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_tune(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TUNE);
  header.b1 = state;
  header.s1 = to_16(full_tune);
  header.s2 = to_16(memory_tune);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_vox(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_VOX);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_toggle_mox(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TOGGLE_MOX);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_toggle_tune(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TOGGLE_TUNE);
  header.s1 = to_16(full_tune);
  header.s2 = to_16(memory_tune);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_mox(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_MOX);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_mode(int s, int v, int mode) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_MODE);
  header.b1 = v;
  header.b2 = mode;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_filter_var(int s, int m, int f) {
  //
  // Change filter edges for filter f for  mode m
  // This is intended for Var1/Var2 and does not do
  // anything else.
  //
  if (f == filterVar1 || f == filterVar2) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_16(CMD_FILTER_VAR);
    header.b1 = m;
    header.b2 = f;
    header.s1 = to_16(filters[m][f].low);
    header.s2 = to_16(filters[m][f].high);
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_rx_filter_cut(int s, int id) {
  //
  // This changes the filter cuts in the receiver
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RX_FILTER_CUT);
  header.b1 = id;

  if (id < receivers) {
    header.s1  =  to_16(receiver[id]->filter_low);
    header.s2  =  to_16(receiver[id]->filter_high);
  }

  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_tx_filter_cut(int s) {
  //
  // This changes the filter cuts in the transmitter
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TX_FILTER_CUT);

  if (can_transmit) {
    header.s1  =  to_16(transmitter->filter_low);
    header.s2  =  to_16(transmitter->filter_high);
  }

  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_filter_sel(int s, int v, int f) {
  //
  // Change filter of VFO v to filter f
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_FILTER_SEL);
  header.b1 = v;
  header.b2 = f;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_sidetone_freq(int s, int f) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_SIDETONEFREQ);
  header.s1 = to_16(f);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_cwpeak(int s, int v, int p) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_CWPEAK);
  header.b1 = v;
  header.b2 = p;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_digidrivemax(int s) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_DIGIMAX);
  command.dbl = to_double(drive_digi_max);
  send_bytes(s, (char *)&command, sizeof(DOUBLE_COMMAND));
}

void send_am_carrier(int s) {
  if (can_transmit) {
    DOUBLE_COMMAND command;
    SYNC(command.header.sync);
    command.header.data_type = to_16(CMD_AMCARRIER);
    command.dbl = to_double(transmitter->am_carrier_level);
    send_bytes(s, (char *)&command, sizeof(DOUBLE_COMMAND));
  }
}

void send_dexp(int s) {
  if (can_transmit) {
    DEXP_DATA command;
    SYNC(command.header.sync);
    command.header.data_type = to_16(CMD_DEXP);
    command.dexp = transmitter->dexp;
    command.dexp_filter = transmitter->dexp_filter;
    command.dexp_trigger = to_16(transmitter->dexp_trigger);
    command.dexp_exp = to_16(transmitter->dexp_exp);
    command.dexp_filter_low = to_16(transmitter->dexp_filter_low);
    command.dexp_filter_high = to_16(transmitter->dexp_filter_high);
    command.dexp_tau = to_double(transmitter->dexp_tau);
    command.dexp_attack = to_double(transmitter->dexp_attack);
    command.dexp_release = to_double(transmitter->dexp_release);
    command.dexp_hold = to_double(transmitter->dexp_hold);
    command.dexp_hyst = to_double(transmitter->dexp_hyst);
    send_bytes(s, (char *)&command, sizeof(DEXP_DATA));
  }
}

void send_tx_compressor(int s) {
  if (can_transmit) {
    COMPRESSOR_DATA command;
    SYNC(command.header.sync);
    command.header.data_type = to_16(CMD_COMPRESSOR);
    command.compressor = transmitter->compressor;
    command.cfc = transmitter->cfc;
    command.cfc_eq = transmitter->cfc_eq;
    command.compressor_level = to_double(transmitter->compressor_level);

    for (int i = 0; i < 11; i++) {
      command.cfc_freq[i] = to_double(transmitter->cfc_freq[i]);
      command.cfc_lvl [i] = to_double(transmitter->cfc_lvl [i]);
      command.cfc_post[i] = to_double(transmitter->cfc_post[i]);
    }

    send_bytes(s, (char *)&command, sizeof(COMPRESSOR_DATA));
  }
}

void send_txmenu(int s) {
  if (can_transmit) {
    TXMENU_DATA command;
    SYNC(command.header.sync);
    command.header.data_type = to_16(CMD_TXMENU);
    command.tune_drive = transmitter->tune_drive;
    command.tune_use_drive = transmitter->tune_use_drive;
    command.swr_protection = transmitter->swr_protection;
    command.mic_boost = mic_boost;
    command.mic_linein = mic_linein;
    command.linein_gain = to_double(linein_gain);
    command.swr_alarm = to_double(transmitter->swr_alarm);
    send_bytes(s, (char *)&command, sizeof(TXMENU_DATA));
  }
}

void send_ctcss(int s) {
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_16(CMD_CTCSS);
    header.b1 = transmitter->ctcss_enabled;
    header.b2 = transmitter->ctcss;
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_txfilter(int s) {
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_16(CMD_TXFILTER);
    header.b1 = transmitter->use_rx_filter;
    header.s1 = to_16(transmitter->default_filter_low);
    header.s2 = to_16(transmitter->default_filter_high);
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_preemp(int s) {
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_16(CMD_PREEMP);
    header.b1 = transmitter->pre_emphasize;
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_psparams(int s, const TRANSMITTER *tx) {
  PS_PARAMS params;
  SYNC(params.header.sync);
  params.header.data_type = to_16(CMD_PSPARAMS);
  params.ps_ptol = tx->ps_ptol;
  params.ps_oneshot = tx->ps_oneshot;
  params.ps_map = tx->ps_map;
  params.ps_setpk = to_double(tx->ps_setpk);
  send_bytes(s, (char *)&params, sizeof(PS_PARAMS));
}

void send_psresume(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_PSRESUME);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_psreset(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_PSRESET);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_psonoff(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_PSONOFF);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_psatt(int s) {
  //
  // This sends TX attenuation, PS auto-att, feedback, and PS ant
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_16(CMD_PSATT);
    header.b1 = transmitter->auto_on;
    header.b2 = transmitter->feedback;
    header.s1 = to_16(transmitter->attenuation);
    header.s2 = to_16(adc[2].antenna);
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_afbinaural(int s, const RECEIVER *rx) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_BINAURAL);
  header.b1 = rx->id;
  header.b2 = rx->binaural;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_rx_fft(int s, const RECEIVER *rx) {
  U32_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_RXFFT);
  command.header.b1 = rx->id;
  command.header.b2 = rx->low_latency;
  command.u32 = to_32(rx->fft_size);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_tx_fft(int s, const TRANSMITTER *tx) {
  U32_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_TXFFT);
  command.header.b1 = tx->id;
  command.u32 = to_32(tx->fft_size);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_patrim(int s) {
  PATRIM_DATA command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_PATRIM);
  command.pa_power = to_16(pa_power);

  for (int i = 0; i < 11; i++) {
    command.pa_trim[i] = to_double(pa_trim[i]);
  }

  send_bytes(s, (char *)&command, sizeof(PATRIM_DATA));
}

void send_deviation(int s, int v, int dev) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_DEVIATION);
  header.b1 = v;
  header.s1 = to_16(dev);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_atob(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_VFO_A_TO_B);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_btoa(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_VFO_B_TO_A);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_swap(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_VFO_SWAP);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_heartbeat(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_HEARTBEAT);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_soapy_rxant(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_SOAPY_RXANT);
  header.b1 = id;
  header.b2 = adc[id].antenna;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_soapy_txant(int s) {
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_16(CMD_SOAPY_TXANT);
    header.b1 = transmitter->antenna;
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_soapy_agc(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_SOAPY_AGC);
  header.b1 = id;
  header.b2 = adc[id].agc;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_split(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_SPLIT);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_cw(int s, int state, int wait) {
  //
  // Send this in one header, encode wait-time in two shorts
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_CW);
  header.b1  = state;
  header.s1 = to_16(wait >> 12);
  header.s2 = to_16(wait & 0xFFF);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_sat(int s, int sat) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_SAT);
  header.b1  = sat;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_duplex(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_DUP);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_display(int s, int id) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.b1 = id;

  if (id < RECEIVERS) {
    command.header.data_type = to_16(CMD_RX_DISPLAY);
    command.header.b2 = receiver[id]->display_detector_mode;
    command.header.s1 = to_16(receiver[id]->display_average_mode);
    command.dbl = to_double(receiver[id]->display_average_time);
  } else if (can_transmit) {
    command.header.data_type = to_16(CMD_TX_DISPLAY);
    command.header.b2 = transmitter->display_detector_mode;
    command.header.s1 = to_16(transmitter->display_average_mode);
    command.dbl = to_double(transmitter->display_average_time);
  }

  send_bytes(s, (char *)&command, sizeof(DOUBLE_COMMAND));
}

void send_rxfps(int s, int id, int fps) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RX_FPS);
  header.b1 = id;
  header.b2 = fps;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_txfps(int s, int fps) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_TX_FPS);
  header.b2 = fps;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_lock(int s, int lock) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_LOCK);
  header.b1 = lock;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_ctun(int s, int vfo, int ctun) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_CTUN);
  header.b1 = vfo;
  header.b2 = ctun;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_rx_select(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RX_SELECT);
  header.b1 = id;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_rit(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RIT);
  header.b1 = id;
  header.b2 = vfo[id].rit_enabled;
  header.s1 = to_16(vfo[id].rit);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_xit(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_XIT);
  header.b1 = id;
  header.b2 = vfo[id].xit_enabled;
  header.s1 = to_16(vfo[id].xit);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_sample_rate(int s, int id, int sample_rate) {
  U32_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_16(CMD_SAMPLE_RATE);
  command.header.b1 = id;
  command.u32 = to_32(sample_rate);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_receivers(int s, int receivers) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RECEIVERS);
  header.b1 = receivers;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_rit_step(int s, int v, int step) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_RIT_STEP);
  header.b1 = v;
  header.s1 = to_16(step);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_filter_board(int s, int filter_board) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_FILTER_BOARD);
  header.b1 = filter_board;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_adc(int s, int id, int adc) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_ADC);
  header.b1 = id;
  header.b2 = adc;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_anan10E(int s, int new) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_ANAN10E);
  header.b1 = new;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_region(int s, int region) {
  HEADER header;
  //
  // prepeare for bandstack reorganisation
  //
  radio_change_region(region);
  SYNC(header.sync);
  header.data_type = to_16(CMD_REGION);
  header.b1 = region;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_mute_rx(int s, int id, int mute) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_16(CMD_MUTE_RX);
  header.b1 = id;
  header.b2 = mute;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

