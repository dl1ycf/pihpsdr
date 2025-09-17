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
 * This file contains stuff exectued only on the client side
 */

#include <gtk/gtk.h>
#include <stdint.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/sha.h>

#include "audio.h"
#include "band.h"
#include "client_server.h"
#include "ext.h"
#include "filter.h"
#include "message.h"
#include "radio.h"
#include "sliders.h"
#include "store.h"
#include "vfo.h"
#include "vox.h"

int client_socket = -1;
int remote_started = 0;

static int client_running = 0;
static GThread *client_thread_id;
static GMutex accumulated_mutex;
static int accumulated_steps[2] = {0, 0};
static long long accumulated_hz[2] = {0LL, 0LL};
static int accumulated_round[2] = {FALSE, FALSE};
guint check_vfo_timer_id = 0;

static void *client_thread(void* arg);

//
// version of connect() which takes a time-out.
// take from monoxid.net
//

static int connect_wait (int sockno, struct sockaddr * addr, size_t addrlen, struct timeval * timeout) {
  int res, opt;

  // get socket flags
  if ((opt = fcntl (sockno, F_GETFL, NULL)) < 0) {
    return -1;
  }

  // set socket non-blocking
  if (fcntl (sockno, F_SETFL, opt | O_NONBLOCK) < 0) {
    return -1;
  }

  // try to connect
  if ((res = connect (sockno, addr, addrlen)) < 0) {
    if (errno == EINPROGRESS) {
      fd_set wait_set;
      // make file descriptor set with socket
      FD_ZERO (&wait_set);
      FD_SET (sockno, &wait_set);
      // wait for socket to be writable; return after given timeout
      res = select (sockno + 1, NULL, &wait_set, NULL, timeout);
    }
  } else {
    // connection was successful immediately
    res = 1;
  }

  // reset socket flags
  if (fcntl (sockno, F_SETFL, opt) < 0) {
    return -1;
  }

  if (res < 0) {
    // an error occured in connect or select
    return -1;
  } else if (res == 0) {
    // time-out
    return -2;
  } else {
    socklen_t len = sizeof (opt);

    // check for errors in socket layer
    if (getsockopt (sockno, SOL_SOCKET, SO_ERROR, &opt, &len) < 0) {
      return -1;
    }

    // there was an error
    if (opt) {
      errno = opt;
      return -1;
    }
  }

  return 0;
}


//
// Return values:
//  0  successfully connected
// -1  general error
// -2  timeout upon connecting
// -3  host name could not be resolved
// -4  wrong version number
// -5  wrong password
//
int radio_connect_remote(char *host, int port, const char *pwd) {
  struct sockaddr_in server_address;
  struct timeval timeout;
  int on = 1;
  int rc;
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  static char server_host[128];

  if (client_socket == -1) {
    t_print("%s: socket creation failed...\n", __FUNCTION__);
    return -1;
  }

  setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(client_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  struct hostent *server = gethostbyname(host);

  if (server == NULL) {
    t_print("%s: no such host: %s\n", __FUNCTION__, host);
    close(client_socket);
    return -3;
  }

  // assign IP, PORT and bind to address
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&server_address.sin_addr.s_addr, server->h_length);
  server_address.sin_port = to_short(port);
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  rc = connect_wait(client_socket, (struct sockaddr *)&server_address, sizeof(server_address), &timeout);

  if (rc != 0) {
    t_perror("ConnectWait");
    close(client_socket);
    return rc;  // -1: general error, -2: timeout
  }

  t_print("%s: socket %d bound to %s:%d\n", __FUNCTION__, client_socket, host, port);
  unsigned char s[2 * SHA512_DIGEST_LENGTH];
  unsigned char sha[SHA512_DIGEST_LENGTH];

  if (recv_bytes(client_socket, (char *)s, 4) < 0) {
    t_print("%s: Could not receive Version number\n", __FUNCTION__);
    close(client_socket);
    return -1;
  }

  if (((CLIENT_SERVER_VERSION >> 24) & 0xFF) != s[0] ||
      ((CLIENT_SERVER_VERSION >> 16) & 0xFF) != s[1] ||
      ((CLIENT_SERVER_VERSION >>  8) & 0xFF) != s[2] ||
      ((CLIENT_SERVER_VERSION      ) & 0xFF) != s[3]) {
    t_print("%s: Wrong Client/Server version number\n", __FUNCTION__);
    close(client_socket);
    return -4;
  }

  if (recv_bytes(client_socket, (char *)s, SHA512_DIGEST_LENGTH) < 0) {
    t_print("%s: Could not receive Challenge\n", __FUNCTION__);
    close(client_socket);
    return -1;
  }

  generate_pwd_hash(s, sha, pwd);
  send_bytes(client_socket, (char *)sha, SHA512_DIGEST_LENGTH);

  if (recv_bytes(client_socket, (char *)s, 1) < 0) {
    t_print("%s: Could not receive Pwd Receipt\n", __FUNCTION__);
    close(client_socket);
    return -1;
  }

  if (*s != 0x7F) {
    t_print("%s: Server did not accept password\n", __FUNCTION__);
    close(client_socket);
    return -5;
  }

  snprintf(server_host, sizeof(server_host), "%s:%d", host, port);
  client_thread_id = g_thread_new("remote_client", client_thread, &server_host);
  return 0;
}

void server_tx_audio(short sample) {
  //
  // This is called in the client and collects data to be
  // sent to the server
  //
  static int txaudio_buffer_index = 0;
  static TXAUDIO_DATA txaudio_data;

  if (!can_transmit) {
    return;
  }

  static short speak = 0;

  if (client_socket < 0) { return; }

  if (sample > speak) { speak = sample; }

  if (-sample > speak) { speak = -sample; }

  txaudio_data.samples[txaudio_buffer_index++] = to_short(sample);

  if (txaudio_buffer_index >= AUDIO_DATA_SIZE) {
    int txmode = vfo_get_tx_mode();

    if (radio_is_transmitting() && txmode != modeCWU && txmode != modeCWL && !transmitter->tune && !transmitter->twotone) {
      //
      // The actual transmission of the mic audio samples only takes  place
      // if we *need* them (note VOX is handled locally)
      //
      SYNC(txaudio_data.header.sync);
      txaudio_data.header.data_type = to_short(INFO_TXAUDIO);
      txaudio_data.numsamples = to_short(txaudio_buffer_index);

      if (send_bytes(client_socket, (char *)&txaudio_data, sizeof(TXAUDIO_DATA)) < 0) {
        t_perror("server_txaudio");
        client_socket = -1;
      }

      txaudio_buffer_index = 0;
    } else {
      //
      // Since we are NOT transmitting, delete first half of the buffer
      // so that if a RX/TX transition occurs, there  is "some" data available
      //
      memcpy(txaudio_data.samples, txaudio_data.samples + (AUDIO_DATA_SIZE / 2), (AUDIO_DATA_SIZE / 2) * sizeof(uint16_t));
      txaudio_buffer_index = (AUDIO_DATA_SIZE / 2);
    }

    vox_update((double)speak * 0.00003051);
    speak = 0;
  }
}

//
// Not all VFO frequency updates generate a packet to be sent by the client.
// Instead, frequency updates are "collected" and sent out  (if necessary)
// every 100 milli seconds.
// We use this periodic job to send a heart beat every 15 sec.
//

void update_vfo_move(int v, long long hz, int round) {
  g_mutex_lock(&accumulated_mutex);
  accumulated_hz[v] += hz;
  accumulated_round[v] = round;
  g_mutex_unlock(&accumulated_mutex);
}

void update_vfo_step(int v, int steps) {
  g_mutex_lock(&accumulated_mutex);
  accumulated_steps[v] += steps;
  g_mutex_unlock(&accumulated_mutex);
}

static void send_vfo_move(int s, int id, long long hz, int round) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_MOVE);
  command.header.b1 = id;
  command.header.b2 = round;
  command.u64 = to_ll(hz);
  send_bytes(s, (char *)&command, sizeof(command));
}

static int check_vfo(void *arg) {
  static int count = 0;

  if (!client_running) { return FALSE; }

  if (count++ >= 150) {
    send_heartbeat(client_socket);
    count = 0;
  }

  g_mutex_lock(&accumulated_mutex);

  for (int v = 0; v < 2; v++) {
    if (accumulated_steps[v] != 0) {
      send_vfo_step(client_socket, v, accumulated_steps[v]);
      accumulated_steps[v] = 0;
    }

    if (accumulated_hz[v] != 0LL || accumulated_round[v]) {
      send_vfo_move(client_socket, v, accumulated_hz[v], accumulated_round[v]);
      accumulated_hz[v] = 0LL;
      accumulated_round[v] = FALSE;
    }
  }

  g_mutex_unlock(&accumulated_mutex);
  return TRUE;
}

//
// Called from remote_start_radio() when it is done
//
void start_vfo_timer() {
  g_mutex_init(&accumulated_mutex);
  check_vfo_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 100, check_vfo, NULL, NULL);
}

////////////////////////////////////////////////////////////////////////////
//
// client_thread is running on the "remote"  computer
// (which communicates with the server on the "local" computer)
//
// It receives a lot of data which is stored to get the proper menus etc.,
// but this data does not affect any radio operation (all of which runs
// on the "local" computer)
//
////////////////////////////////////////////////////////////////////////////

static void *client_thread(void* arg) {
  int bytes_read;
  HEADER header;
  char *server = (char *)arg;
  static char title[256];
  client_running = TRUE;
  //
  // Some settings/allocation must be made HERE
  //
  radio       = g_new(DISCOVERED, 1);
  transmitter = g_new(TRANSMITTER, 1);
  memset(radio,       0, sizeof(DISCOVERED));
  memset(transmitter, 0, sizeof(TRANSMITTER));
  RECEIVERS = 2;
  PS_TX_FEEDBACK = 2;
  PS_RX_FEEDBACK = 3;
  can_transmit = 0;  // will be set when receiving an INFO_TRANSMITTER

  for (int i = 0; i < 2; i++) {
    RECEIVER *rx = receiver[i] = g_new(RECEIVER, 1);
    memset(rx, 0, sizeof(RECEIVER));
    memset(rx, 0, sizeof(RECEIVER));
    g_mutex_init(&rx->display_mutex);
    g_mutex_init(&rx->mutex);
    g_mutex_init(&rx->local_audio_mutex);
    rx->id = i;
    rx->pixel_samples = NULL;
    rx->local_audio_buffer = NULL;
    rx->display_panadapter = 1;
    rx->display_waterfall = 1;
    rx->panadapter_high = -40;
    rx->panadapter_low = -140;
    rx->panadapter_step = 20;
    rx->panadapter_peaks_on = 0;
    rx->panadapter_num_peaks = 3;
    rx->panadapter_ignore_range_divider = 20;
    rx->panadapter_ignore_noise_percentile = 80;
    rx->panadapter_hide_noise_filled = 1;
    rx->panadapter_peaks_in_passband_filled = 0;
    rx->waterfall_high = -40;
    rx->waterfall_low = -140;
    rx->waterfall_automatic = 1;
    rx->waterfall_percent = 25;
    rx->display_filled = 1;
    rx->display_gradient = 1;
    rx->local_audio_buffer = NULL;
    rx->local_audio = 0;
    snprintf(rx->audio_name, sizeof(rx->audio_name), "NO AUDIO");
    rx->mute_when_not_active = 0;
    rx->mute_radio = 0;
    rx->audio_channel = STEREO;
    rx->audio_device = -1;
  }

  active_receiver = receiver[0];
  g_mutex_init(&transmitter->display_mutex);
  transmitter->display_panadapter = 1;
  transmitter->display_waterfall = 0;
  transmitter->panadapter_high = 0;
  transmitter->panadapter_low = -70;
  transmitter->panadapter_step = 10;
  transmitter->panadapter_peaks_on = 0;
  transmitter->panadapter_num_peaks = 4;  // if the typical application is a two-tone test we need four
  transmitter->panadapter_ignore_range_divider = 24;
  transmitter->panadapter_ignore_noise_percentile = 50;
  transmitter->panadapter_hide_noise_filled = 1;
  transmitter->panadapter_peaks_in_passband_filled = 0;
  transmitter->displaying = 0;
  transmitter->dialog_x = -1;
  transmitter->dialog_y = -1;
  transmitter->dialog = NULL;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    RECEIVER *rx = receiver[PS_RX_FEEDBACK] = g_new(RECEIVER, 1);
    memset(rx, 0, sizeof(RECEIVER));
    rx->id = PS_RX_FEEDBACK;
  }

  while (client_running) {
    int type;
    bytes_read = recv_bytes(client_socket, (char *)&header, sizeof(header));

    if (bytes_read <= 0) {
      t_print("%s: ReadErr for HEADER\n", __FUNCTION__);
      return NULL;
    }

    if (memcmp(header.sync, syncbytes, sizeof(syncbytes))  != 0) {
      t_print("%s: header.sync mismatch: %02x %02x %02x %02x\n", __FUNCTION__,
              header.sync[0],
              header.sync[1],
              header.sync[2],
              header.sync[3]);
      int syncs = 0;
      uint8_t c;

      while (syncs != sizeof(syncbytes) && client_running) {
        bytes_read = recv_bytes(client_socket, (char *)&c, 1);

        if (bytes_read <= 0) {
          t_print("%s: ReadErr for HEADER RESYNC\n", __FUNCTION__);
          return NULL;
        }

        if (c == syncbytes[syncs]) {
          syncs++;
        } else {
          syncs = 0;
        }
      }

      if (recv_bytes(client_socket, (char *)&header + sizeof(header.sync), sizeof(header) - sizeof(header.sync)) <= 0) {
        return NULL;
      }

      t_print("%s: Re-SYNC was successful!\n", __FUNCTION__);
    }

    type = from_short(header.data_type);

    switch (type) {
    case INFO_DISPLAY: {
      //
      // This data is needed if one wants warnings, PA currents etc. to be on display
      // (it is transferred every 100 msec)
      //
      DISPLAY_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(data) - sizeof(HEADER)) < 0) { return NULL; }

      adc[0].overload = data.adc0_overload;
      adc[1].overload = data.adc1_overload;
      high_swr_seen = data.high_swr_seen;
      tx_fifo_overrun = data.tx_fifo_overrun;
      tx_fifo_underrun = data.tx_fifo_underrun;
      TxInhibit = data.TxInhibit;
      capture_state = data.capture_state;
      exciter_power = from_short(data.exciter_power);
      ADC0 = from_short(data.ADC0);
      ADC1 = from_short(data.ADC1);
      sequence_errors = from_short(data.sequence_errors);
      capture_record_pointer = from_int(data.capture_record_pointer);
      capture_replay_pointer = from_int(data.capture_replay_pointer);

      //
      // This will only happen after a "SWR protection event" on
      // the server side
      //
      if (can_transmit) {
        if (data.txzero && transmitter->drive > 0.4) {
          radio_set_drive(0.0);
        }
      }
    }
    break;

    case INFO_PS: {
      //
      // This data is needed for the PS menu and to indicate PS status on the TX panadapter
      // (it is transferred every 100 msec)
      //
      if (can_transmit) {
        PS_DATA data;

        if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(data) - sizeof(HEADER)) < 0) { return NULL; }

        for (int i = 0; i < 16; i++) {
          transmitter->psinfo[i] = from_short(data.psinfo[i]);
        }

        transmitter->attenuation = from_short(data.attenuation);
        transmitter->ps_getmx = from_double(data.ps_getmx);
      }
    }
    break;

    case INFO_MEMORY: {
      MEMORY_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(data) - sizeof(HEADER)) < 0) { return NULL; }

      int index = data.index;
      mem[index].sat_mode           = data.sat_mode;
      mem[index].ctun               = data.ctun;
      mem[index].mode               = data.mode;
      mem[index].filter             = data.filter;
      mem[index].bd                 = data.bd;
      mem[index].alt_ctun           = data.alt_ctun;
      mem[index].alt_mode           = data.alt_mode;
      mem[index].alt_filter         = data.alt_filter;
      mem[index].alt_bd             = data.alt_bd;
      mem[index].ctcss_enabled      = data.ctcss_enabled;
      mem[index].ctcss              = data.ctcss;
      mem[index].deviation          = from_short(data.deviation);
      mem[index].alt_deviation      = from_short(data.alt_deviation);
      mem[index].frequency          = from_ll(data.frequency);
      mem[index].ctun_frequency     = from_ll(data.ctun_frequency);
      mem[index].alt_frequency      = from_ll(data.alt_frequency);
      mem[index].alt_ctun_frequency = from_ll(data.alt_ctun_frequency);
    }
    break;

    case INFO_BAND: {
      BAND_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(data) - sizeof(HEADER)) < 0) { return NULL; }

      if (data.band > BANDS + XVTRS) {
        t_print("%s: WARNING: band data received for b=%d, too large.\n", __FUNCTION__, data.band);
        break;
      }

      BAND *band = band_get_band(data.band);

      if (data.current > band->bandstack->entries) {
        t_print("%s: WARNING: band stack too large for b=%d, s=%d.\n", __FUNCTION__, data.band, data.current);
        break;
      }

      snprintf(band->title, sizeof(band->title), "%s", data.title);
      band->OCrx = data.OCrx;
      band->OCtx = data.OCtx;
      band->RxAntenna = data.RxAntenna;
      band->TxAntenna = data.TxAntenna;
      band->disablePA = data.disablePA;
      band->bandstack->current_entry = data.current;
      band->gaincalib = from_short(data.gaincalib);
      band->pa_calibration = from_double(data.pa_calibration);
      band->frequencyMin = from_ll(data.frequencyMin);
      band->frequencyMax = from_ll(data.frequencyMax);
      band->frequencyLO  = from_ll(data.frequencyLO);
      band->errorLO  = from_ll(data.errorLO);
    }
    break;

    case INFO_BANDSTACK: {
      BANDSTACK_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(data) - sizeof(HEADER)) < 0) { return NULL; }

      if (data.band > BANDS + XVTRS) {
        t_print("%s: WARNING: band data received for b=%d, too large.\n", __FUNCTION__, data.band);
        break;
      }

      BAND *band = band_get_band(data.band);

      if (data.stack > band->bandstack->entries) {
        t_print("%s: WARNING: band stack too large for b=%d, s=%d.\n", __FUNCTION__, data.band, data.stack);
        break;
      }

      BANDSTACK_ENTRY *entry = band->bandstack->entry;
      entry += data.stack;
      entry->mode = data.mode;
      entry->filter = data.filter;
      entry->ctun = data.ctun;
      entry->ctcss_enabled = data.ctcss_enabled;
      entry->ctcss = data.ctcss_enabled;
      entry->deviation = from_short(data.deviation);
      entry->frequency =  from_ll(data.frequency);
      entry->ctun_frequency = from_ll(data.ctun_frequency);
    }
    break;

    case INFO_RADIO: {
      RADIO_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(data) - sizeof(HEADER)) < 0) { return NULL; }

      snprintf(radio->name, sizeof(radio->name), "%s", data.name);
      locked = data.locked;
      have_rx_gain = data.have_rx_gain;
      protocol = radio->protocol = data.protocol;
      radio->supported_receivers = from_short(data.supported_receivers);
      receivers = data.receivers;
      filter_board = data.filter_board;
      enable_auto_tune = data.enable_auto_tune;
      new_pa_board = data.new_pa_board;
      region = data.region;
      radio_change_region(region);
      atlas_penelope = data.atlas_penelope;
      atlas_clock_source_10mhz = data.atlas_clock_source_10mhz;
      atlas_clock_source_128mhz = data.atlas_clock_source_128mhz;
      atlas_mic_source = data.atlas_mic_source;
      atlas_janus = data.atlas_janus;
      hl2_audio_codec = data.hl2_audio_codec;
      anan10E = data.anan10E;
      tx_out_of_band_allowed = data.tx_out_of_band_allowed;
      pa_enabled = data.pa_enabled;
      mic_boost = data.mic_boost;
      mic_linein = data.mic_linein;
      mic_ptt_enabled = data.mic_ptt_enabled;
      mic_bias_enabled = data.mic_bias_enabled;
      mic_ptt_tip_bias_ring = data.mic_ptt_tip_bias_ring;
      cw_keyer_sidetone_volume = mic_input_xlr = data.mic_input_xlr;
      OCtune = data.OCtune;
      mute_rx_while_transmitting = data.mute_rx_while_transmitting;
      mute_spkr_amp = data.mute_spkr_amp;
      mute_spkr_xmit = data.mute_spkr_xmit;
      split = data.split;
      sat_mode = data.sat_mode;
      duplex = data.duplex;
      have_rx_gain = data.have_rx_gain;
      have_rx_att = data.have_rx_att;
      have_alex_att = data.have_alex_att;
      have_preamp = data.have_preamp;
      have_dither = data.have_dither;
      have_saturn_xdma = data.have_saturn_xdma;
      rx_stack_horizontal = data.rx_stack_horizontal;
      n_adc = data.n_adc;
      diversity_enabled = data.diversity_enabled;
      soapy_iqswap = data.soapy_iqswap;
      radio->soapy.rx[0].antennas = data.soapy_rx1_antennas;
      radio->soapy.rx[1].antennas = data.soapy_rx2_antennas;
      radio->soapy.tx.antennas = data.soapy_tx_antennas;
      radio->soapy.rx[0].gains = data.soapy_rx1_gains;
      radio->soapy.rx[1].gains = data.soapy_rx2_gains;
      radio->soapy.tx.gains = data.soapy_tx_gains;
      radio->soapy.tx_channels = data.soapy_tx_channels;
      radio->soapy.rx[0].has_automatic_gain = data.soapy_rx1_has_automatic_gain;
      radio->soapy.rx[1].has_automatic_gain = data.soapy_rx2_has_automatic_gain;
      //
      memcpy(radio->soapy.hardware_key, data.soapy_hardware_key, 64);
      memcpy(radio->soapy.driver_key, data.soapy_driver_key, 64);

      for (int i = 0; i < radio->soapy.rx[0].antennas; i++) {
        memcpy(radio->soapy.rx[0].antenna[i], data.soapy_rx1_antenna[i], 64);
      }

      for (int i = 0; i < radio->soapy.rx[1].antennas; i++) {
        memcpy(radio->soapy.rx[1].antenna[i], data.soapy_rx2_antenna[i], 64);
      }

      for (int i = 0; i < radio->soapy.tx.antennas; i++) {
        memcpy(radio->soapy.tx.antenna[i], data.soapy_tx_antenna[i], 64);
      }

      for (int i = 0; i < radio->soapy.rx[0].gains; i++) {
        memcpy(radio->soapy.rx[0].gain_elem_name[i], data.soapy_rx1_gain_elem_name[i], 64);
      }

      for (int i = 0; i < radio->soapy.rx[1].gains; i++) {
        memcpy(radio->soapy.rx[1].gain_elem_name[i], data.soapy_rx2_gain_elem_name[i], 64);
      }

      for (int i = 0; i < radio->soapy.tx.gains; i++) {
        memcpy(radio->soapy.tx.gain_elem_name[i], data.soapy_tx_gain_elem_name[i], 64);
      }

      //
      pa_power = from_short(data.pa_power);
      OCfull_tune_time = from_short(data.OCfull_tune_time);
      OCmemory_tune_time = from_short(data.OCmemory_tune_time);
      cw_keyer_sidetone_frequency = from_short(data.cw_keyer_sidetone_frequency);
      rx_gain_calibration = from_short(data.rx_gain_calibration);
      device = radio->device = from_short(data.device);
      //
      drive_min = from_double(data.drive_min);
      drive_max = from_double(data.drive_max);
      drive_digi_max = from_double(data.drive_digi_max);
      div_gain = from_double(data.div_gain);
      div_phase = from_double(data.div_phase);

      for (int i = 0; i < 11; i++) {
        pa_trim[i] = from_double(data.pa_trim[i]);
      }

      radio->soapy.rx[0].gain_step = from_double(data.soapy_rx1_gain_step);
      radio->soapy.rx[0].gain_min  = from_double(data.soapy_rx1_gain_min );
      radio->soapy.rx[0].gain_max  = from_double(data.soapy_rx1_gain_max );
      radio->soapy.rx[1].gain_step = from_double(data.soapy_rx2_gain_step);
      radio->soapy.rx[1].gain_min  = from_double(data.soapy_rx2_gain_min );
      radio->soapy.rx[1].gain_max  = from_double(data.soapy_rx2_gain_max );
      radio->soapy.tx.gain_step = from_double(data.soapy_tx_gain_step);
      radio->soapy.tx.gain_min  = from_double(data.soapy_tx_gain_min );
      radio->soapy.tx.gain_max  = from_double(data.soapy_tx_gain_max );

      for (int i = 0; i < radio->soapy.rx[0].gains; i++) {
        radio->soapy.rx[0].gain_elem_step[i] = from_double(data.soapy_rx1_gain_elem_step[i]);
        radio->soapy.rx[0].gain_elem_min [i] = from_double(data.soapy_rx1_gain_elem_min [i]);
        radio->soapy.rx[0].gain_elem_max [i] = from_double(data.soapy_rx1_gain_elem_max [i]);
      }

      for (int i = 0; i < radio->soapy.rx[1].gains; i++) {
        radio->soapy.rx[1].gain_elem_step[i] = from_double(data.soapy_rx2_gain_elem_step[i]);
        radio->soapy.rx[1].gain_elem_min [i] = from_double(data.soapy_rx2_gain_elem_min [i]);
        radio->soapy.rx[1].gain_elem_max [i] = from_double(data.soapy_rx2_gain_elem_max [i]);
      }

      for (int i = 0; i < radio->soapy.tx.gains; i++) {
        radio->soapy.tx.gain_elem_step[i] = from_double(data.soapy_tx_gain_elem_step[i]);
        radio->soapy.tx.gain_elem_min [i] = from_double(data.soapy_tx_gain_elem_min [i]);
        radio->soapy.tx.gain_elem_max [i] = from_double(data.soapy_tx_gain_elem_max [i]);
      }

      //
      frequency_calibration = from_ll(data.frequency_calibration);
      soapy_radio_sample_rate = from_ll(data.soapy_radio_sample_rate);
      radio->frequency_min = from_ll(data.radio_frequency_min);
      radio->frequency_max = from_ll(data.radio_frequency_max);

      if (protocol == SOAPYSDR_PROTOCOL) {
        radio->soapy.sample_rate = soapy_radio_sample_rate;
      }

      snprintf(title, sizeof(title), "piHPSDR: %s remote at %s", radio->name, server);
      g_idle_add(ext_set_title, (void *)title);
    }
    break;

    case INFO_ADC: {
      ADC_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(ADC_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      int i = data.adc;
      adc[i].preamp = data.preamp;
      adc[i].dither = data.dither;
      adc[i].random = data.random;
      adc[i].antenna = data.antenna;
      adc[i].alex_attenuation = data.alex_attenuation;
      adc[i].filter_bypass = data.filter_bypass;
      adc[i].antenna = from_short(data.antenna);
      adc[i].attenuation = from_short(data.attenuation);
      adc[i].gain = from_double(data.gain);
      adc[i].min_gain = from_double(data.min_gain);
      adc[i].max_gain = from_double(data.max_gain);

      if (active_receiver->adc == i) {
        g_idle_add(sliders_attenuation, GINT_TO_POINTER(100 + active_receiver->id));
        g_idle_add(sliders_rf_gain, GINT_TO_POINTER(100 + active_receiver->id));
      }
    }
    break;

    case INFO_RECEIVER: {
      RECEIVER_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(RECEIVER_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      int id = data.id;
      RECEIVER *rx = receiver[id];
      rx->id                    = id;
      rx->adc                   = data.adc;
      rx->agc                   = data.agc;
      rx->nb                    = data.nb;
      rx->nb2_mode              = data.nb2_mode;
      rx->nr                    = data.nr;
      rx->nr_agc                = data.nr_agc;
      rx->nr2_ae                = data.nr2_ae;
      rx->nr2_gain_method       = data.nr2_gain_method;
      rx->nr2_npe_method        = data.nr2_npe_method;
      rx->anf                   = data.anf;
      rx->snb                   = data.snb;
      rx->display_detector_mode = data.display_detector_mode;
      rx->display_average_mode  = data.display_average_mode;
      rx->zoom                  = data.zoom;
      rx->squelch_enable        = data.squelch_enable;
      rx->binaural              = data.binaural;
      rx->eq_enable             = data.eq_enable;
      rx->smetermode            = data.smetermode;
      rx->low_latency           = data.low_latency;
      rx->pan                   = data.pan;
      //
      rx->fps                   = from_short(data.fps);
      rx->filter_low            = from_short(data.filter_low);
      rx->filter_high           = from_short(data.filter_high);
      rx->deviation             = from_short(data.deviation);
      rx->width                 = from_short(data.width);
      //
      rx->cA                    = from_double(data.cA);
      rx->cB                    = from_double(data.cB);
      rx->cAp                   = from_double(data.cAp);
      rx->cBp                   = from_double(data.cBp);
      rx->squelch               = from_double(data.squelch);
      rx->display_average_time  = from_double(data.display_average_time);
      rx->volume                = from_double(data.volume);
      rx->agc_gain              = from_double(data.agc_gain);
      rx->agc_hang              = from_double(data.agc_hang);
      rx->agc_thresh            = from_double(data.agc_thresh);
      rx->agc_hang_threshold    = from_double(data.agc_hang_threshold);
      rx->nr2_trained_threshold = from_double(data.nr2_trained_threshold);
      rx->nr2_trained_t2        = from_double(data.nr2_trained_t2);
      rx->nb_tau                = from_double(data.nb_tau);
      rx->nb_hang               = from_double(data.nb_hang);
      rx->nb_advtime            = from_double(data.nb_advtime);
      rx->nb_thresh             = from_double(data.nb_thresh);
#ifdef EXTNR
      rx->nr4_reduction_amount  = from_double(data.nr4_reduction_amount);
      rx->nr4_smoothing_factor  = from_double(data.nr4_smoothing_factor);
      rx->nr4_whitening_factor  = from_double(data.nr4_whitening_factor);
      rx->nr4_noise_rescale     = from_double(data.nr4_noise_rescale);
      rx->nr4_post_threshold    = from_double(data.nr4_post_threshold);
#endif

      for (int i = 0; i < 11; i++) {
        rx->eq_freq[i]          = from_double(data.eq_freq[i]);
        rx->eq_gain[i]          = from_double(data.eq_gain[i]);
      }

      rx->fft_size              = from_ll(data.fft_size);
      rx->sample_rate           = from_ll(data.sample_rate);

      if (protocol == ORIGINAL_PROTOCOL && id == 1) {
        rx->sample_rate = receiver[0]->sample_rate;
      }

      if (id == active_receiver->id) {
        g_idle_add(sliders_af_gain, GINT_TO_POINTER(100 + id));
        g_idle_add(sliders_squelch, GINT_TO_POINTER(100 + id));
        g_idle_add(sliders_zoom, GINT_TO_POINTER(100 + id));
        g_idle_add(sliders_pan, GINT_TO_POINTER(100 + id));
        g_idle_add(sliders_agc_gain, GINT_TO_POINTER(100 + id));
      }
    }
    break;

    case INFO_TRANSMITTER: {
      TRANSMITTER_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(TRANSMITTER_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      //
      // When transmitter data is fully received, we can set can_transmit
      //
      transmitter->id                        = data.id;
      transmitter->dac                       = data.dac;
      transmitter->display_detector_mode     = data.display_detector_mode;
      transmitter->display_average_mode      = data.display_average_mode;
      transmitter->use_rx_filter             = data.use_rx_filter;
      transmitter->antenna                   = data.antenna;
      transmitter->puresignal                = data.puresignal;
      transmitter->feedback                  = data.feedback;
      transmitter->auto_on                   = data.auto_on;
      transmitter->ps_oneshot                = data.ps_oneshot;
      transmitter->ctcss_enabled             = data.ctcss_enabled;
      transmitter->ctcss                     = data.ctcss;
      transmitter->pre_emphasize             = data.pre_emphasize;
      transmitter->drive                     = data.drive;
      transmitter->tune_use_drive            = data.tune_use_drive;
      transmitter->tune_drive                = data.tune_drive;
      transmitter->compressor                = data.compressor;
      transmitter->cfc                       = data.cfc;
      transmitter->cfc_eq                    = data.cfc_eq;
      transmitter->dexp                      = data.dexp;
      transmitter->dexp_filter               = data.dexp_filter;
      transmitter->eq_enable                 = data.eq_enable;
      transmitter->alcmode                   = data.alcmode;
      transmitter->swr_protection            = data.swr_protection;
      //
      transmitter->fps                       = from_short(data.fps);
      transmitter->dexp_filter_low           = from_short(data.dexp_filter_low);
      transmitter->dexp_filter_high          = from_short(data.dexp_filter_high);
      transmitter->dexp_trigger              = from_short(data.dexp_trigger);
      transmitter->dexp_exp                  = from_short(data.dexp_exp);
      transmitter->filter_low                = from_short(data.filter_low);
      transmitter->filter_high               = from_short(data.filter_high);
      transmitter->deviation                 = from_short(data.deviation);
      transmitter->width                     = from_short(data.width);
      transmitter->height                    = from_short(data.height);
      transmitter->attenuation               = from_short(data.attenuation);
      transmitter->default_filter_low        = from_short(data.tx_default_filter_low);
      transmitter->default_filter_high       = from_short(data.tx_default_filter_high);
      //
      transmitter->fft_size                  = from_ll(data.fft_size);
      //
      transmitter->swr_alarm                 = from_double(data.swr_alarm);
      transmitter->dexp_tau                  = from_double(data.dexp_tau);
      transmitter->dexp_attack               = from_double(data.dexp_attack);
      transmitter->dexp_release              = from_double(data.dexp_release);
      transmitter->dexp_hold                 = from_double(data.dexp_hold);
      transmitter->dexp_hyst                 = from_double(data.dexp_hyst);
      transmitter->mic_gain                  = from_double(data.mic_gain);
      transmitter->compressor_level          = from_double(data.compressor_level);
      transmitter->display_average_time      = from_double(data.display_average_time);
      transmitter->am_carrier_level          = from_double(data.am_carrier_level);
      transmitter->ps_ampdelay               = from_double(data.ps_ampdelay);
      transmitter->ps_moxdelay               = from_double(data.ps_moxdelay);
      transmitter->ps_loopdelay              = from_double(data.ps_loopdelay);

      for (int i = 0; i < 11; i++) {
        transmitter->eq_freq[i]                = from_double(data.eq_freq[i]);
        transmitter->eq_gain[i]                = from_double(data.eq_gain[i]);
        transmitter->cfc_freq[i]               = from_double(data.cfc_freq[i]);
        transmitter->cfc_lvl[i]                = from_double(data.cfc_lvl[i]);
        transmitter->cfc_post[i]               = from_double(data.cfc_post[i]);
      }

      can_transmit = 1;
      g_idle_add(sliders_drive, GINT_TO_POINTER(100));
      g_idle_add(sliders_mic_gain, GINT_TO_POINTER(100));
      g_idle_add(sliders_cmpr, GINT_TO_POINTER(100));
    }
    break;

    case INFO_VFO: {
      VFO_DATA vfo_data;

      if (recv_bytes(client_socket, (char *)&vfo_data + sizeof(HEADER), sizeof(VFO_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      int v = vfo_data.vfo;
      vfo[v].band = vfo_data.band;
      vfo[v].bandstack = vfo_data.bandstack;
      vfo[v].mode = vfo_data.mode;
      vfo[v].filter = vfo_data.filter;
      vfo[v].ctun = vfo_data.ctun;
      vfo[v].rit_enabled = vfo_data.rit_enabled;
      vfo[v].xit_enabled = vfo_data.xit_enabled;
      vfo[v].cwAudioPeakFilter = vfo_data.cwAudioPeakFilter;
      //
      vfo[v].rit_step  = from_short(vfo_data.rit_step);
      vfo[v].deviation  = from_short(vfo_data.deviation);
      //
      vfo[v].frequency = from_ll(vfo_data.frequency);
      vfo[v].ctun_frequency = from_ll(vfo_data.ctun_frequency);
      vfo[v].rit = from_ll(vfo_data.rit);
      vfo[v].xit = from_ll(vfo_data.xit);
      vfo[v].lo = from_ll(vfo_data.lo);
      vfo[v].offset = from_ll(vfo_data.offset);
      vfo[v].step   = from_ll(vfo_data.step);
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case INFO_RX_SPECTRUM:
    case INFO_TX_SPECTRUM: {
      SPECTRUM_DATA spectrum_data;
      //
      // The length of the payload is included in the header, only
      // read the number of bytes specified there.
      //
      size_t payload = from_short(header.s1);

      if (recv_bytes(client_socket, (char *)&spectrum_data + sizeof(HEADER), payload) < 0) { return NULL; }

      //
      // We load the current VFO frequencies on top of the spectrum data packets,
      // so we can apply this info *before* drawing the spectrum. Normally the
      // data should not have changed.
      //
      long long frequency_a = from_ll(spectrum_data.vfo_a_freq);
      long long frequency_b = from_ll(spectrum_data.vfo_b_freq);
      long long ctun_frequency_a = from_ll(spectrum_data.vfo_a_ctun_freq);
      long long ctun_frequency_b = from_ll(spectrum_data.vfo_b_ctun_freq);
      long long offset_a = from_ll(spectrum_data.vfo_a_offset);
      long long offset_b = from_ll(spectrum_data.vfo_b_offset);

      if (vfo[VFO_A].frequency != frequency_a || vfo[VFO_B].frequency != frequency_b
          || vfo[VFO_A].ctun_frequency != ctun_frequency_a || vfo[VFO_B].ctun_frequency != ctun_frequency_b
          || vfo[VFO_A].offset != offset_a || vfo[VFO_B].offset != offset_b) {
        vfo[VFO_A].frequency = frequency_a;
        vfo[VFO_B].frequency = frequency_b;
        vfo[VFO_A].ctun_frequency = ctun_frequency_a;
        vfo[VFO_B].ctun_frequency = ctun_frequency_b;
        vfo[VFO_A].offset = offset_a;
        vfo[VFO_B].offset = offset_b;
        g_idle_add(ext_vfo_update, NULL);
      }

      if (type == INFO_RX_SPECTRUM && spectrum_data.id < receivers) {
        RECEIVER *rx = receiver[spectrum_data.id];
        rx->cA = from_double(spectrum_data.cA);
        rx->cB = from_double(spectrum_data.cB);
        rx->cAp = from_double(spectrum_data.cAp);
        rx->cBp = from_double(spectrum_data.cBp);
        rx->meter = from_double(spectrum_data.meter);
        rx->pixels_available = spectrum_data.avail;
        int width = from_short(spectrum_data.width);

        if (width == rx->width) {
          g_mutex_lock(&rx->display_mutex);

          if (rx->pixel_samples == NULL) {
            rx->pixel_samples = g_new(float, (int) rx->width);
          }

          for (int i = 0; i < rx->width; i++) {
            rx->pixel_samples[i] = (float)((int)spectrum_data.sample[i] - 200);
          }

          g_mutex_unlock(&rx->display_mutex);
          g_idle_add(rx_remote_update_display, rx);
        }
      }

      if (type == INFO_TX_SPECTRUM && can_transmit) {
        TRANSMITTER *tx = transmitter;
        tx->alc = from_double(spectrum_data.alc);
        tx->fwd = from_double(spectrum_data.fwd);
        tx->swr = from_double(spectrum_data.swr);
        int width = from_short(spectrum_data.width);

        if (tx->pixel_samples == NULL) {
          tx->pixel_samples = g_new(float, (int) tx->width);
        }

        if (width == tx->width) {
          g_mutex_lock(&tx->display_mutex);

          for (int i = 0; i < tx->width; i++) {
            tx->pixel_samples[i] = (float)((int)spectrum_data.sample[i] - 200);
          }

          g_mutex_unlock(&tx->display_mutex);
          g_idle_add(tx_remote_update_display, tx);
        }
      }
    }
    break;

    case INFO_RXAUDIO: {
      RXAUDIO_DATA rxaudio_data;

      if (recv_bytes(client_socket, (char *)&rxaudio_data + sizeof(HEADER), sizeof(RXAUDIO_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      RECEIVER *rx = receiver[rxaudio_data.rx];
      int numsamples = from_short(rxaudio_data.numsamples);

      //
      // Note CAPTURing is only done on the server side
      //
      for (int i = 0; i < numsamples; i++) {
        short left_sample = from_short(rxaudio_data.samples[(i * 2)]);
        short right_sample = from_short(rxaudio_data.samples[(i * 2) + 1]);

        if (radio_is_transmitting() && (!duplex || mute_rx_while_transmitting)) {
          left_sample = 0.0;
          right_sample = 0.0;
        }

        if (rx->mute_radio || (rx != active_receiver && rx->mute_when_not_active)) {
          left_sample = 0;
          right_sample = 0;
        }

        if (rx->audio_channel == LEFT)  { right_sample = 0; }

        if (rx->audio_channel == RIGHT) { left_sample  = 0; }

        if (rx->local_audio) {
          audio_write(rx, (float)left_sample / 32767.0, (float)right_sample / 32767.0);
        }
      }
    }
    break;

    case CMD_START_RADIO: {
      if (!remote_started) {
        g_idle_add(radio_remote_start, (gpointer)server);
      }

      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_SAMPLE_RATE: {
      U64_COMMAND cmd;

      if (recv_bytes(client_socket, (char *)&cmd + sizeof(HEADER), sizeof(U64_COMMAND) - sizeof(HEADER)) < 0) { return NULL; }

      int id = header.b1;
      long long rate = from_ll(cmd.u64);
      receiver[id]->sample_rate = (int)rate;
    }
    break;

    case CMD_LOCK: {
      locked = header.b1;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_SPLIT: {
      split = header.b1;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_SAT: {
      sat_mode = header.b1;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_DUP: {
      duplex = header.b1;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_RECEIVERS: {
      int r = header.b1;
      g_idle_add(radio_remote_change_receivers, GINT_TO_POINTER(r));
    }
    break;

    case CMD_MODE: {
      int id = header.b1;
      int m = header.b2;
      vfo[id].mode = m;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_FILTER_VAR: {
      int m = header.b1;
      int f = header.b2;
      filters[m][f].low = from_short(header.s1);
      filters[m][f].high = from_short(header.s2);
    }
    break;

    case CMD_RX_FILTER_CUT: {
      //
      // This commands is only used to set the RX filter edges
      // on the client side.
      //
      int id = header.b1;

      if (id < receivers) {
        receiver[id]->filter_low = from_short(header.s1);
        receiver[id]->filter_high = from_short(header.s2);
      }

      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_TX_FILTER_CUT: {
      //
      // This commands is only used to set the TX filter edges
      // on the client side.
      //
      if (can_transmit) {
        transmitter->filter_low = from_short(header.s1);
        transmitter->filter_high = from_short(header.s2);
      }

      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_AGC: {
      int id = header.b1;
      receiver[id]->agc = header.b2;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_ZOOM: {
      int id = header.b1;
      receiver[id]->zoom = header.b2;
      g_idle_add(sliders_zoom, GINT_TO_POINTER(100 + id));
    }
    break;

    case CMD_PAN: {
      int id = header.b1;
      receiver[id]->pan = header.b2;
      g_idle_add(sliders_pan, GINT_TO_POINTER(100 + id));
    }
    break;

    case CMD_VOLUME: {
      DOUBLE_COMMAND cmd;

      if (recv_bytes(client_socket, (char *)&cmd + sizeof(HEADER), sizeof(DOUBLE_COMMAND) - sizeof(HEADER)) < 0) { return NULL; }

      int id = cmd.header.b1;
      double volume = from_double(cmd.dbl);
      receiver[id]->volume = volume;
    }
    break;

    case CMD_AGC_GAIN: {
      //
      // When this command comes back from the server,
      // it has re-calculated "hant" and "thresh", while the other two
      // entries should be exactly those the client has just sent.
      //
      AGC_GAIN_COMMAND agc_gain_cmd;

      if (recv_bytes(client_socket, (char *)&agc_gain_cmd + sizeof(HEADER), sizeof(AGC_GAIN_COMMAND) - sizeof(HEADER)) < 0) { return NULL; }

      int id = agc_gain_cmd.id;
      receiver[id]->agc_gain = from_double(agc_gain_cmd.gain);
      receiver[id]->agc_hang = from_double(agc_gain_cmd.hang);
      receiver[id]->agc_thresh = from_double(agc_gain_cmd.thresh);
      receiver[id]->agc_hang_threshold = from_double(agc_gain_cmd.hang_thresh);
    }
    break;

    case CMD_ATTENUATION: {
      int id = header.b1;
      int att = from_short(header.s1);
      radio_set_attenuation(id, att);
    }
    break;

    case CMD_RFGAIN: {
      DOUBLE_COMMAND command;

      if (recv_bytes(client_socket, (char *)&command + sizeof(HEADER), sizeof(DOUBLE_COMMAND) - sizeof(HEADER)) < 0) { return NULL; }

      int id = command.header.b1;
      double gain = from_double(command.dbl);
      adc[receiver[id]->adc].gain = gain;
    }
    break;

    case CMD_RIT_STEP: {
      int v = header.b1;
      int step = from_short(header.s1);
      vfo_id_set_rit_step(v, step);
    }
    break;

    case CMD_MOX: {
      g_idle_add(radio_remote_set_mox, GINT_TO_POINTER(header.b1));
    }
    break;

    case CMD_VOX: {
      g_idle_add(radio_remote_set_vox, GINT_TO_POINTER(header.b1));
    }
    break;

    case CMD_TUNE: {
      g_idle_add(radio_remote_set_tune, GINT_TO_POINTER(header.b1));
    }
    break;

    case CMD_TWOTONE: {
      g_idle_add(radio_remote_set_twotone, GINT_TO_POINTER(header.b1));
      g_idle_add(radio_remote_set_mox, GINT_TO_POINTER(header.b1));
    }
    break;

    default:
      t_print("%s: Unknown type=%d\n", __FUNCTION__, from_short(header.data_type));
      break;
    }
  }

  return NULL;
}

