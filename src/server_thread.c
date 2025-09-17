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
 * the receive thread, so we need a mutex in send_bytes. It is important that
 * a packet (that is, a bunch of data that belongs together) is sent in a single
 * call to send_bytes.
 */

#include <gtk/gtk.h>

#include <arpa/inet.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "actions.h"
#include "band.h"
#include "client_server.h"
#include "ext.h"
#include "filter.h"
#include "iambic.h"
#include "main.h"
#include "message.h"
#include "new_protocol.h"
#include "radio.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "store.h"
#include "vfo.h"

char hpsdr_pwd[HPSDR_PWD_LEN];
int  hpsdr_server = 0;
int  listen_port = 50000;
int  server_stops_protocol = 0;
REMOTE_CLIENT remoteclient = { 0 };

//
// Audio
//
#define MIC_RING_BUFFER_SIZE 9600
#define MIC_RING_LOW         3000

static short *mic_ring_buffer;
static volatile short  mic_ring_outpt = 0;
static volatile short  mic_ring_inpt = 0;

static GThread *listen_thread_id;
static int server_running = 0;
static int listen_socket = -1;

static int server_command(void * data);

static int send_periodic_data(gpointer arg) {
  //
  // Use this periodic function to update PS and display info
  //
  if (!remoteclient.running) {
    return TRUE;
  }

  if (can_transmit) {
    if (transmitter->puresignal) {
      PS_DATA ps_data;
      SYNC(ps_data.header.sync);
      ps_data.header.data_type = to_short(INFO_PS);
      tx_ps_getinfo(transmitter);

      for (int i = 0; i < 16; i++) {
        ps_data.psinfo[i] = to_short(transmitter->psinfo[i]);
      }

      ps_data.attenuation = to_short(transmitter->attenuation);
      tx_ps_getmx(transmitter);
      ps_data.ps_getmx = to_double(transmitter->ps_getmx);
      send_bytes(remoteclient.socket, (char *)&ps_data, sizeof(PS_DATA));
    }
  }

  //
  // Use this to periodically transfer data that is usually displayed
  // as "warning" message on the panadapter. The "txzero" is included
  // here to inform the client when the server has moved the drive slider
  // to zero (SWR protection measure)
  //
  DISPLAY_DATA disp_data;
  SYNC(disp_data.header.sync);
  disp_data.header.data_type = to_short(INFO_DISPLAY);
  disp_data.adc0_overload = adc[0].overload;
  disp_data.adc1_overload = adc[1].overload;
  disp_data.high_swr_seen = high_swr_seen;
  disp_data.tx_fifo_overrun = tx_fifo_overrun;
  disp_data.tx_fifo_underrun = tx_fifo_underrun;
  disp_data.TxInhibit = TxInhibit;
  disp_data.txzero = can_transmit ? (transmitter->drive < 0.5) : 0;
  disp_data.capture_state = capture_state;
  disp_data.exciter_power = to_short(exciter_power);
  disp_data.ADC0 = to_short(ADC0);
  disp_data.ADC1 = to_short(ADC1);
  disp_data.sequence_errors = to_short(sequence_errors);
  disp_data.capture_record_pointer = to_int(capture_record_pointer);
  disp_data.capture_replay_pointer = to_int(capture_replay_pointer);
  send_bytes(remoteclient.socket, (char *)&disp_data, sizeof(DISPLAY_DATA));
  //
  // if sending the data failed due to an interrupted connection,
  // server_loop() will terminate and this source ID be removed
  // elsewhere.
  //
  return TRUE;
}

//
// Note that this is now only called when
// - display mutex is locked
// - displaying is set and a pixel_samples contain valid data
//
void send_rxspectrum(int id) {
  const float *samples;
  SPECTRUM_DATA spectrum_data;
  int numsamples = 0;

  if (!remoteclient.send_rx_spectrum[id] || id >= receivers || !remoteclient.running) {
    return;
  }

  SYNC(spectrum_data.header.sync);
  spectrum_data.header.data_type = to_short(INFO_RX_SPECTRUM);
  spectrum_data.vfo_a_freq = to_ll(vfo[VFO_A].frequency);
  spectrum_data.vfo_b_freq = to_ll(vfo[VFO_B].frequency);
  spectrum_data.vfo_a_ctun_freq = to_ll(vfo[VFO_A].ctun_frequency);
  spectrum_data.vfo_b_ctun_freq = to_ll(vfo[VFO_B].ctun_frequency);
  spectrum_data.vfo_a_offset = to_ll(vfo[VFO_A].offset);
  spectrum_data.vfo_b_offset = to_ll(vfo[VFO_B].offset);
  //
  spectrum_data.id = id;
  const RECEIVER *rx = receiver[id];
  spectrum_data.avail = rx->pixels_available;
  spectrum_data.cA = to_double(rx->cA);
  spectrum_data.cB = to_double(rx->cB);
  spectrum_data.cAp = to_double(rx->cAp);
  spectrum_data.cBp = to_double(rx->cBp);
  spectrum_data.meter = to_double(rx->meter);
  spectrum_data.width = to_short(rx->width);
  samples = rx->pixel_samples;
  numsamples = rx->width;

  if (numsamples > SPECTRUM_DATA_SIZE) { numsamples = SPECTRUM_DATA_SIZE; }

  for (int i = 0; i < numsamples; i++) {
    int s = ((int) samples[i]) + 200;  // -200dBm ... 55dBm maps to 0 ... 55

    if (s < 0) { s = 0; }

    if (s > 255) { s = 255; }

    spectrum_data.sample[i] = (uint8_t) s;
  }

  if (numsamples > 0) {
    //
    // spectrum commands have a variable length, since this depends on the
    // width of the screen. To this end, calculate the total number of bytes
    // in THIS command (xferlen) and the length  of the payload.
    //
    int xferlen = sizeof(spectrum_data) - (SPECTRUM_DATA_SIZE - numsamples) * sizeof(uint8_t);
    int payload = xferlen - sizeof(HEADER);

    //cppcheck-suppress knownConditionTrueFalse
    if (payload > 32000) { fatal_error("FATAL: Spectrum payload too large"); }

    spectrum_data.header.s1 = to_short(payload);
    send_bytes(remoteclient.socket, (char *)&spectrum_data, xferlen);
  }
}

void send_txspectrum() {
  const float *samples;
  SPECTRUM_DATA spectrum_data;
  int numsamples = 0;

  if (!remoteclient.send_tx_spectrum || !can_transmit || !remoteclient.running) {
    return;
  }

  SYNC(spectrum_data.header.sync);
  spectrum_data.header.data_type = to_short(INFO_TX_SPECTRUM);
  spectrum_data.vfo_a_freq = to_ll(vfo[VFO_A].frequency);
  spectrum_data.vfo_b_freq = to_ll(vfo[VFO_B].frequency);
  spectrum_data.vfo_a_ctun_freq = to_ll(vfo[VFO_A].ctun_frequency);
  spectrum_data.vfo_b_ctun_freq = to_ll(vfo[VFO_B].ctun_frequency);
  spectrum_data.vfo_a_offset = to_ll(vfo[VFO_A].offset);
  spectrum_data.vfo_b_offset = to_ll(vfo[VFO_B].offset);
  //
  const TRANSMITTER *tx = transmitter;
  spectrum_data.alc   = to_double(tx->alc);
  spectrum_data.fwd   = to_double(tx->fwd);
  spectrum_data.swr   = to_double(tx->swr);
  spectrum_data.width = to_short(tx->width);
  samples = tx->pixel_samples;
  numsamples = tx->width;

  if (numsamples > SPECTRUM_DATA_SIZE) { numsamples = SPECTRUM_DATA_SIZE; }

  //
  // When running duplex, tx->pixels > tx->width, so transfer only central part
  //
  int offset = (tx->pixels - tx->width) / 2;

  for (int i = 0; i < numsamples; i++) {
    int s = ((int) samples[i + offset]) + 200;  // -200dBm ... 55dBm maps to 0 ... 55

    if (s < 0) { s = 0; }

    if (s > 255) { s = 255; }

    spectrum_data.sample[i] = (uint8_t) s;
  }

  if (numsamples > 0) {
    //
    // spectrum commands have a variable length, since this depends on the
    // width of the screen. To this end, calculate the total number of bytes
    // in THIS command (xferlen) and the length  of the payload.
    //
    int xferlen = sizeof(spectrum_data) - (SPECTRUM_DATA_SIZE - numsamples) * sizeof(uint8_t);
    int payload = xferlen - sizeof(HEADER);

    //cppcheck-suppress knownConditionTrueFalse
    if (payload > 32000) { fatal_error("FATAL: Spectrum payload too large"); }

    spectrum_data.header.s1 = to_short(payload);
    send_bytes(remoteclient.socket, (char *)&spectrum_data, xferlen);
  }
}

void remote_rxaudio(const RECEIVER *rx, short left_sample, short right_sample) {
  static int rxaudio_buffer_index[2] = { 0, 0};
  static RXAUDIO_DATA rxaudio_data[2];  // for up to 2 receivers
  int id = rx->id;
  int i = rxaudio_buffer_index[id] * 2;

  if (!remoteclient.running) {
    return;
  }

  rxaudio_data[id].samples[i] = to_short(left_sample);
  rxaudio_data[id].samples[i + 1] = to_short(right_sample);
  rxaudio_buffer_index[id]++;

  if (rxaudio_buffer_index[id] >= AUDIO_DATA_SIZE) {
    SYNC(rxaudio_data[id].header.sync);
    rxaudio_data[id].header.data_type = to_short(INFO_RXAUDIO);
    rxaudio_data[id].rx = id;
    rxaudio_data[id].numsamples = to_short(rxaudio_buffer_index[id]);
    send_bytes(remoteclient.socket, (char *)&rxaudio_data[id], sizeof(RXAUDIO_DATA));
    rxaudio_buffer_index[id] = 0;
  }
}

short remote_get_mic_sample() {
  //
  // return one sample from the  microphone audio ring buffer
  // If it is empty, return a zero, and continue to return
  // zero until it is at least filled with  MIC_RING_LOW samples
  //
  short sample;
  static int is_empty = 1;
  int numsamples = mic_ring_outpt - mic_ring_inpt;

  if (numsamples < 0) { numsamples += MIC_RING_BUFFER_SIZE; }

  if (numsamples <= 0) { is_empty = 1; }

  if (is_empty && numsamples < MIC_RING_LOW) {
    return 0;
  }

  is_empty = 0;
  int newpt = mic_ring_outpt + 1;

  if (newpt == MIC_RING_BUFFER_SIZE) { newpt = 0; }

  MEMORY_BARRIER;
  sample = mic_ring_buffer[mic_ring_outpt];
  // atomic update of read pointer
  MEMORY_BARRIER;
  mic_ring_outpt = newpt;
  return sample;
}

//
// server_loop is running on the "local" computer
// (with direct cable connection to the radio hardware)
//
static void server_loop() {
  HEADER header;
  t_print("%s: Client connected on port %d\n", __FUNCTION__, remoteclient.address.sin_port);
  //
  // Allocate ring buffer for TX mic data
  //
  mic_ring_buffer = g_new(short, MIC_RING_BUFFER_SIZE);
  mic_ring_outpt = 0;
  mic_ring_inpt = 0;
  //
  // The server starts with sending  a lot of data to initialise
  // the data on the client side.
  //
  //
  // Send global variables
  //
  send_radio_data(remoteclient.socket);
  //
  // send ADC data structure
  //
  send_adc_data(remoteclient.socket, 0);
  send_adc_data(remoteclient.socket, 1);

  //
  // Send filter edges of the Var1 and Var2 filters
  //
  for (int m = 0; m < MODES;  m++) {
    send_filter_var(remoteclient.socket, m, filterVar1);
    send_filter_var(remoteclient.socket, m, filterVar2);
  }

  //
  // Send receiver data. For HPSDR, this includes the PS RX feedback
  // receiver since it has a setting (antenna used for feedpack) that
  // can be changed through the GUI
  //
  for (int i = 0; i < RECEIVERS; i++) {
    send_rx_data(remoteclient.socket, i);
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    send_rx_data(remoteclient.socket, PS_RX_FEEDBACK);
  }

  //
  // Send VFO data
  //
  send_vfo_data(remoteclient.socket, VFO_A);    // send INFO_VFO packet
  send_vfo_data(remoteclient.socket, VFO_B);    // send INFO_VFO packet

  //
  // Send Band and Bandstack data
  //
  for (int b = 0; b < BANDS + XVTRS; b++) {
    send_band_data(remoteclient.socket, b);
    const BAND *band = band_get_band(b);

    for (int s = 0; s < band->bandstack->entries; s++) {
      send_bandstack_data(remoteclient.socket, b, s);
    }
  }

  //
  // Send memory slots
  //
  for (int i = 0; i < NUM_MEMORIES; i++) {
    send_memory_data(remoteclient.socket, i);
  }

  //
  // Send transmitter data
  //
  send_tx_data(remoteclient.socket);
  //
  // If everything has been sent, start the radio
  //
  send_start_radio(remoteclient.socket);

  //
  // Now, enter an "inifinte" loop, get and parse commands from the client.
  // This loop is (only) left if there is an I/O error.
  // If a complete command has been received, put a "server_command()" with that
  // command into the GTK idle queue.
  //
  while (remoteclient.running) {
    //
    // Getting out-of-sync data is a very rare event with TCP
    // (I am not sure whether this can happen unless there is a program error)
    // so try first to read a complete header in one shot, and if this files,
    // do a re-sync
    //
    int bytes_read = recv_bytes(remoteclient.socket, (char *)&header, sizeof(HEADER));

    if (bytes_read <= 0) {
      t_print("%s: ReadErr for HEADER SYNC\n", __FUNCTION__);
      remoteclient.running = FALSE;
      continue;
    }

    if (memcmp(header.sync, syncbytes, sizeof(syncbytes))  != 0) {
      t_print("%s: header.sync mismatch: %02x %02x %02x %02x\n", __FUNCTION__,
              header.sync[0],
              header.sync[1],
              header.sync[2],
              header.sync[3]);
      int syncs = 0;
      uint8_t c;

      while (syncs != sizeof(syncbytes) && remoteclient.running) {
        bytes_read = recv_bytes(remoteclient.socket, (char *)&c, 1);

        if (bytes_read <= 0) {
          t_print("%s: ReadErr for HEADER RESYNC\n", __FUNCTION__);
          remoteclient.running = FALSE;
          break;
        }

        if (c == syncbytes[syncs]) {
          syncs++;
        } else {
          syncs = 0;
        }
      }

      if (recv_bytes(remoteclient.socket, (char *)&header + sizeof(header.sync), sizeof(header) - sizeof(header.sync)) <= 0) {
        remoteclient.running = FALSE;
      }

      if (remoteclient.running) {
        t_print("%s: Re-SYNC was successful!\n", __FUNCTION__);
      } else {
        t_print("%s: Re-SYNC failed.\n", __FUNCTION__);
      }
    }

    if (!remoteclient.running) { break; }

    //
    // Now we have a valid header
    //
    int data_type = from_short(header.data_type);
    //t_print("%s: received header: type=%d\n", __FUNCTION__, data_type);

    switch (data_type) {
    case CMD_HEARTBEAT:
      // periodically sent to  keep  connection alive
      break;

    case INFO_TXAUDIO: {
      //
      // TX audio will be IMMEDIATELY
      // (not through the GTK queue) put  to the ring buffer
      //
      TXAUDIO_DATA txaudio_data;

      if (recv_bytes(remoteclient.socket, (char *)&txaudio_data + sizeof(HEADER),
                     sizeof(TXAUDIO_DATA) - sizeof(HEADER)) > 0) {
        unsigned int numsamples = from_short(txaudio_data.numsamples);

        for (unsigned int i = 0; i < numsamples; i++) {
          int newpt = mic_ring_inpt + 1;

          if (newpt == MIC_RING_BUFFER_SIZE) { newpt = 0; }

          if (newpt != mic_ring_outpt) {
            MEMORY_BARRIER;
            // buffer space available, do the write
            mic_ring_buffer[mic_ring_inpt] = from_short(txaudio_data.samples[i]);
            MEMORY_BARRIER;
            // atomic update of mic_ring_inpt
            mic_ring_inpt = newpt;
          }
        }
      }
    }
    break;

    case INFO_BAND: {
      BAND_DATA *command = g_new(BAND_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(BAND_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case INFO_BANDSTACK: {
      BANDSTACK_DATA *command = g_new(BANDSTACK_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(BANDSTACK_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case INFO_ADC: {
      //
      // Sending ADC data from the client to the server has a very limited scope:
      // - antenna (for SoapySDR)
      //
      ADC_DATA *command = g_new(ADC_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(ADC_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_RX_SPECTRUM: {
      int id = header.b1;
      int state = header.b2;
      remoteclient.send_rx_spectrum[id] = state;
    }
    break;

    case CMD_TX_SPECTRUM: {
      int state = header.b2;
      remoteclient.send_tx_spectrum = state;
    }
    break;

    case CMD_AGC_GAIN: {
      AGC_GAIN_COMMAND *command = g_new(AGC_GAIN_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(AGC_GAIN_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_NOISE: {
      NOISE_COMMAND *command = g_new(NOISE_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(NOISE_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_RX_EQ:
    case CMD_TX_EQ: {
      EQUALIZER_COMMAND *command = g_new(EQUALIZER_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(EQUALIZER_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_RADIOMENU: {
      RADIOMENU_DATA *command = g_new(RADIOMENU_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(RADIOMENU_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_RXMENU: {
      RXMENU_DATA *command = g_new(RXMENU_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(RXMENU_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_DIVERSITY: {
      DIVERSITY_COMMAND *command = g_new(DIVERSITY_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(DIVERSITY_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_DEXP: {
      DEXP_DATA *command = g_new(DEXP_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(DEXP_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_COMPRESSOR: {
      COMPRESSOR_DATA *command = g_new(COMPRESSOR_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(COMPRESSOR_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_TXMENU: {
      TXMENU_DATA *command = g_new(TXMENU_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(TXMENU_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_PSPARAMS: {
      PS_PARAMS *command = g_new(PS_PARAMS, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(PS_PARAMS) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    case CMD_PATRIM: {
      PATRIM_DATA *command = g_new(PATRIM_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(PATRIM_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    //
    // All commands with a single  "double" in the body
    //
    case CMD_DRIVE:
    case CMD_DIGIMAX:
    case CMD_SQUELCH:
    case CMD_MICGAIN:
    case CMD_RFGAIN:
    case CMD_VOLUME:
    case CMD_RX_DISPLAY:
    case CMD_AMCARRIER:
    case CMD_TX_DISPLAY: {
      DOUBLE_COMMAND *command = g_new(DOUBLE_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(DOUBLE_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    //
    // All commands with a single uint64_t in the command  body
    //
    case CMD_SAMPLE_RATE:
    case CMD_VFO_STEPSIZE:
    case CMD_MOVETO:
    case CMD_RXFFT:
    case CMD_TXFFT:
    case CMD_FREQ:
    case CMD_MOVE: {
      U64_COMMAND *command = g_new(U64_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(U64_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(server_command, command);
      }
    }
    break;

    //
    // All "header-only" commands simply make a copy of the header and
    // submit that copy  to server_command().
    //
    case CMD_ADC:
    case CMD_AGC:
    case CMD_ANAN10E:
    case CMD_ATTENUATION:
    case CMD_BANDSTACK:
    case CMD_BAND_SEL:
    case CMD_BINAURAL:
    case CMD_CAPTURE:
    case CMD_CTCSS:
    case CMD_CTUN:
    case CMD_CW:
    case CMD_CWPEAK:
    case CMD_DEVIATION:
    case CMD_DUP:
    case CMD_FILTER_BOARD:
    case CMD_RX_FILTER_CUT:
    case CMD_TX_FILTER_CUT:
    case CMD_FILTER_SEL:
    case CMD_FILTER_VAR:
    case CMD_LOCK:
    case CMD_METER:
    case CMD_MODE:
    case CMD_MOX:
    case CMD_MUTE_RX:
    case CMD_PAN:
    case CMD_PREEMP:
    case CMD_PSATT:
    case CMD_PSONOFF:
    case CMD_PSRESET:
    case CMD_PSRESUME:
    case CMD_RCL:
    case CMD_RECEIVERS:
    case CMD_REGION:
    case CMD_RESTART:
    case CMD_RIT:
    case CMD_RIT_STEP:
    case CMD_RX_FPS:
    case CMD_RX_SELECT:
    case CMD_SAT:
    case CMD_SCREEN:
    case CMD_SIDETONEFREQ:
    case CMD_SOAPY_AGC:
    case CMD_SOAPY_RXANT:
    case CMD_SOAPY_TXANT:
    case CMD_SPLIT:
    case CMD_STEP:
    case CMD_STORE:
    case CMD_TOGGLE_MOX:
    case CMD_TOGGLE_TUNE:
    case CMD_TUNE:
    case CMD_TWOTONE:
    case CMD_TX_FPS:
    case CMD_TXFILTER:
    case CMD_VFO_A_TO_B:
    case CMD_VFO_B_TO_A:
    case CMD_VFO_SWAP:
    case CMD_VOX:
    case CMD_XIT:
    case CMD_XVTR:
    case CMD_ZOOM: {
      HEADER *command = g_new(HEADER, 1);
      *command = header;
      g_idle_add(server_command, command);
    }
    break;

    default:
      t_print("%s: UNKNOWN command: %d\n", __FUNCTION__, from_short(header.data_type));
      remoteclient.running = FALSE;
      break;
    }
  }

  t_print("%s: Terminating\n", __FUNCTION__);
}

//
// listen_thread runs on the server side, waits for connections,
// and starts the server loop
//
static void *listen_thread(void *arg) {
  struct sockaddr_in address;
  struct timeval timeout;
  int on = 1;
  t_print("%s: listening on port %d\n", __FUNCTION__, listen_port);

  if (server_stops_protocol) {
    g_idle_add(radio_remote_protocol_stop, NULL);
  }

  while (server_running) {
    if (listen_socket >= 0) { close(listen_socket); }

    // create TCP socket to listen on
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_socket < 0) {
      t_print("%s: socket() failed\n", __FUNCTION__);
      break;
    }

    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    // bind to listening port
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = to_short(listen_port);

    if (bind(listen_socket, (struct sockaddr * )&address, sizeof(address)) < 0) {
      t_print("%s: bind() failed\n", __FUNCTION__);
      break;
    }

    // listen for connections
    if (listen(listen_socket, 5) < 0) {
      t_print("%s: listen() failed\n", __FUNCTION__);
      break;
    }

    remoteclient.address_length = sizeof(remoteclient.address);
    t_print("%s: accepting connections...\n", __FUNCTION__);

    if ((remoteclient.socket = accept(listen_socket, (struct sockaddr * )&remoteclient.address,
                                      &remoteclient.address_length)) < 0) {
      //
      // We arrive here if either the internet connection failed, or destroy_hpsdr_server()
      // has been invoked which closes the listen socket
      //
      t_print("%s: accept() failed\n", __FUNCTION__);
      break;
    }

    //
    // Set a time-out of 30 seconds. The client is supposed to send a heart-beat packet at least
    // every 15 sec
    //
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(remoteclient.socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeout.tv_sec =  1;
    setsockopt(remoteclient.socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    unsigned char s[2 * SHA512_DIGEST_LENGTH];
    unsigned char sha[SHA512_DIGEST_LENGTH];
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&remoteclient.address)->sin_addr), (char *)s, 2 * SHA512_DIGEST_LENGTH);
    t_print("%s: client connected from %s\n", __FUNCTION__, s);
    //
    // send version number to the client
    //
    s[0] = (CLIENT_SERVER_VERSION >> 24) & 0xFF;
    s[1] = (CLIENT_SERVER_VERSION >> 16) & 0xFF;
    s[2] = (CLIENT_SERVER_VERSION >>  8) & 0xFF;
    s[3] = (CLIENT_SERVER_VERSION      ) & 0xFF;
    send_bytes(remoteclient.socket, (char *)s, 4);

    if (RAND_bytes(s, SHA512_DIGEST_LENGTH) != 1) {
      remoteclient.running = FALSE;
    }

    send_bytes(remoteclient.socket, (char *)s, SHA512_DIGEST_LENGTH);
    generate_pwd_hash(s, sha, hpsdr_pwd);

    if (recv_bytes(remoteclient.socket, (char *)s, SHA512_DIGEST_LENGTH) < 0) {
      t_print("%s: could not receive Passwd Response\n", __FUNCTION__);
      remoteclient.running = FALSE;
    }

    //
    // Handle too-short server passwords as if the passwords did not match
    //
    if (memcmp(sha, s, SHA512_DIGEST_LENGTH)  != 0 || strlen(hpsdr_pwd) < 5) {
      t_print("%s: ATTENTION: Wrong Password from Client.\n", __FUNCTION__);
      sleep(1);
      *s = 0xF7;
    } else {
      *s = 0x7F;
    }

    send_bytes(remoteclient.socket, (char *)s, 1);

    if (*s == 0x7F) {
      //
      // If the protocol is not running, start it!
      // A non-running protocol results when a client disconnects.
      //
      g_idle_add(radio_remote_protocol_run, NULL);
      // Setting this has to be post-poned until HERE, since now
      // the RX thread starts to send audio data. If we were TXing
      // when the client successfully connects, go RX.
      //
      g_idle_add(ext_radio_set_mox, GINT_TO_POINTER(0));
      remoteclient.running = TRUE;
      //
      // In order to be prepeared for varying screen dimensions,
      // we switch the display to "custom" geometry.
      //
      display_width[1] = display_width[display_size];
      display_height[1] = display_height[display_size];
      display_size = 1;
      rx_stack_horizontal = 0;
      radio_reconfigure_screen_done = 0;
      g_idle_add(ext_radio_reconfigure_screen, NULL);

      while (!radio_reconfigure_screen_done) { usleep(100000); }

      for (int id = 0; id < RECEIVERS; id++) {
        remoteclient.send_rx_spectrum[id] = FALSE;
      }

      remoteclient.send_tx_spectrum = FALSE;
      //
      // Send PS and on-display data periodically
      //
      remoteclient.timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 150, send_periodic_data, NULL, NULL);
      //
      // We disable "CW handled in Radio" since this makes no sense
      // for remote operation.
      //
      int old_cwi = cw_keyer_internal;
      cw_keyer_internal = 1;
      keyer_update();  // shut down iambic keyer
      cw_keyer_internal = 0;
      schedule_transmit_specific();
      server_loop();
      cw_keyer_internal = old_cwi;
      keyer_update();  // possibly restart iambic keyer
      schedule_transmit_specific();
      //
      // If the connection breaks while transmitting, go RX
      //
      g_idle_add(ext_radio_set_mox, GINT_TO_POINTER(0));

      //
      // Stop sending periodic data
      //
      if (remoteclient.timer_id != 0) {
        g_source_remove(remoteclient.timer_id);
        remoteclient.timer_id = 0;
      }

      if (server_stops_protocol) {
        g_idle_add(radio_remote_protocol_stop, NULL);
      }
    }

    if (remoteclient.socket != -1) {
      close(remoteclient.socket);
      remoteclient.socket = -1;
    }
  }

  //
  // If the server stops and the protocol is halted,
  // restart it.
  //
  t_print("Terminating %s.\n", __FUNCTION__);
  g_idle_add(radio_remote_protocol_run, NULL);
  return NULL;
}

int create_hpsdr_server() {
  server_running = TRUE;
  listen_thread_id = g_thread_new( "HPSDR_listen", listen_thread, NULL);
  return 0;
}

int destroy_hpsdr_server() {
  server_running = FALSE;
  remoteclient.running = FALSE;

  if (listen_socket >= 0) {
    close(listen_socket);
    listen_socket = -1;
  }

  return 0;
}

//
// Execute a remote command through the GTK idle queue
// and (possibly) send a response.
// Because of the response required, we cannot just
// delegate to actions.c
//
// This is executed on the server side only.
//
//
static int server_command(void *data) {
  HEADER *header = (HEADER *)data;
  int type = from_short(header->data_type);

  switch (type) {
  case INFO_BAND: {
    //
    // Band data is sent from the XVTR, ANT, and PA menu on the client side
    //
    const BAND_DATA *band_data = (BAND_DATA *)data;

    if (band_data->band > BANDS + XVTRS) {
      t_print("%s: WARNING: band data received for b=%d, too large.\n", __FUNCTION__, band_data->band);
      break;
    }

    BAND *band = band_get_band(band_data->band);

    if (band_data->current > band->bandstack->entries) {
      t_print("%s: WARNING: band stack too large for b=%d, s=%d.\n", __FUNCTION__, band_data->band, band_data->current);
      break;
    }

    snprintf(band->title, sizeof(band->title), "%s", band_data->title);
    band->OCrx = band_data->OCrx;
    band->OCtx = band_data->OCtx;
    band->RxAntenna = band_data->RxAntenna;
    band->TxAntenna = band_data->TxAntenna;
    band->disablePA = band_data->disablePA;
    band->bandstack->current_entry = band_data->current;
    band->gaincalib = from_short(band_data->gaincalib);
    band->pa_calibration = from_double(band_data->pa_calibration);
    band->frequencyMin = from_ll(band_data->frequencyMin);
    band->frequencyMax = from_ll(band_data->frequencyMax);
    band->frequencyLO  = from_ll(band_data->frequencyLO);
    band->errorLO  = from_ll(band_data->errorLO);
    //
    // make some changes effective
    //
    radio_apply_band_settings(0, 0);
    radio_calc_drive_level();
    schedule_high_priority();
  }
  break;

  case INFO_BANDSTACK: {
    //
    // Bandstack data for the XVTR bands are sent back from the XVTR menu on the client side
    //
    const BANDSTACK_DATA *bandstack_data = (BANDSTACK_DATA *)data;

    if (bandstack_data->band > BANDS + XVTRS) {
      t_print("%s: WARNING: band data received for b=%d, too large.\n", __FUNCTION__, bandstack_data->band);
      break;
    }

    BAND *band = band_get_band(bandstack_data->band);

    if (bandstack_data->stack > band->bandstack->entries) {
      t_print("%s: WARNING: band stack too large for b=%d, s=%d.\n", __FUNCTION__,
              bandstack_data->band, bandstack_data->stack);
      break;
    }

    BANDSTACK_ENTRY *entry = band->bandstack->entry;
    entry += bandstack_data->stack;
    entry->mode = bandstack_data->mode;
    entry->filter = bandstack_data->filter;
    entry->ctun = bandstack_data->ctun;
    entry->ctcss_enabled = bandstack_data->ctcss_enabled;
    entry->ctcss = bandstack_data->ctcss_enabled;
    entry->deviation = from_short(bandstack_data->deviation);
    entry->frequency =  from_ll(bandstack_data->frequency);
    entry->ctun_frequency = from_ll(bandstack_data->ctun_frequency);
  }
  break;

  case INFO_ADC: {
    const ADC_DATA *command = (ADC_DATA *)data;
    int id = command->adc;
    adc[id].preamp = command->preamp;
    adc[id].dither = command->dither;
    adc[id].random = command->random;
    adc[id].antenna = command->antenna;
    adc[id].alex_attenuation = command->alex_attenuation;
    adc[id].filter_bypass = command->filter_bypass;
    adc[id].antenna = from_short(command->antenna);
#ifdef SOAPYSDR

    if (device == SOAPYSDR_USB_DEVICE) {
      soapy_protocol_set_rx_antenna(id, adc[id].antenna);
    }

#endif
  }
  break;

  case CMD_FREQ: {
    const U64_COMMAND *command = (U64_COMMAND *)data;
    int pan = active_receiver->pan;
    int v = command->header.b1;
    long long f = from_ll(command->u64);
    vfo_id_set_frequency(v, f);
    vfo_update();
    send_vfo_data(remoteclient.socket, VFO_A);  // need both in case of SAT/RSAT
    send_vfo_data(remoteclient.socket, VFO_B);  // need both in case of SAT/RSAT

    if (pan != active_receiver->pan) {
      send_pan(remoteclient.socket, active_receiver);
    }
  }
  break;

  case CMD_STEP: {
    int id = header->b1;
    int steps = from_short(header->s1);
    vfo_id_step(id, steps);
    send_rx_data(remoteclient.socket, id);
    send_vfo_data(remoteclient.socket, VFO_A);  // need both in case of SAT/RSAT
    send_vfo_data(remoteclient.socket, VFO_B);  // need both in case of SAT/RSAT
  }
  break;

  case CMD_MOVE: {
    const U64_COMMAND *command = (U64_COMMAND *)data;
    int pan = active_receiver->pan;
    long long hz = from_ll(command->u64);
    vfo_id_move(command->header.b1, hz, command->header.b2);
    send_vfo_data(remoteclient.socket, VFO_A);  // need both in case of SAT/RSAT
    send_vfo_data(remoteclient.socket, VFO_B);  // need both in case of SAT/RSAT

    if (pan != active_receiver->pan) {
      send_pan(remoteclient.socket, active_receiver);
    }
  }
  break;

  case CMD_MOVETO: {
    const  U64_COMMAND *command = (U64_COMMAND *)data;
    int pan = active_receiver->pan;
    long long hz = from_ll(command->u64);
    vfo_id_move_to(command->header.b1, hz, command->header.b2);
    send_vfo_data(remoteclient.socket, VFO_A);  // need both in case of SAT/RSAT
    send_vfo_data(remoteclient.socket, VFO_B);  // need both in case of SAT/RSAT

    if (pan != active_receiver->pan) {
      send_pan(remoteclient.socket, active_receiver);
    }
  }
  break;

  case CMD_ZOOM: {
    int id = header->b1;
    int zoom = header->b2;
    radio_set_zoom(id, zoom);
    send_pan (remoteclient.socket, receiver[id]);
  }
  break;

  case CMD_METER: {
    active_receiver->smetermode = header->b1;

    if (can_transmit) {
      transmitter->alcmode = header->b2;
    }
  }
  break;

  case CMD_XVTR: {
    vfo_xvtr_changed();
  }
  break;

  case CMD_VFO_STEPSIZE: {
    const  U64_COMMAND *command = (U64_COMMAND *)data;
    int id = command->header.b1;
    int step = from_ll(command->u64);
    vfo[id].step = step;
    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case CMD_STORE: {
    int index = header->b1;
    store_memory_slot(index);
    send_memory_data(remoteclient.socket, index);
  }
  break;

  case CMD_RESTART: {
    radio_protocol_restart();
  }
  break;

  case CMD_RCL: {
    int index = header->b1;
    int id = active_receiver->id;
    recall_memory_slot(index);
    send_vfo_data(remoteclient.socket, id);
    send_rx_data(remoteclient.socket, id);
    send_tx_data(remoteclient.socket);
  }
  break;

  case CMD_SCREEN: {
    rx_stack_horizontal = header->b1;
    display_size = 1;
    display_width[1] = from_short(header->s1);
    radio_reconfigure_screen();
  }
  break;

  case CMD_PAN: {
    int id = header->b1;
    int pan = header->b2;
    radio_set_pan(id, pan);
  }
  break;

  case CMD_VOLUME: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    suppress_popup_sliders++;
    radio_set_af_gain(command->header.b1, from_double(command->dbl));
    suppress_popup_sliders--;
  }
  break;

  case CMD_MICGAIN: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    suppress_popup_sliders++;
    radio_set_mic_gain(from_double(command->dbl));
    suppress_popup_sliders--;
  }
  break;

  case CMD_DRIVE: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    suppress_popup_sliders++;
    radio_set_drive(from_double(command->dbl));
    suppress_popup_sliders--;
  }
  break;

  case CMD_AGC: {
    int id = header->b1;
    int agc = header->b2;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->agc = agc;
      rx_set_agc(rx);
      g_idle_add(ext_vfo_update, NULL);
    }
  }
  break;

  case CMD_TOGGLE_TUNE: {
    if (can_transmit) {
      full_tune = from_short(header->s1);
      memory_tune = from_short(header->s2);
      radio_toggle_tune();
      g_idle_add(ext_vfo_update, NULL);
      send_tune(remoteclient.socket, transmitter->tune);
    }
  }
  break;

  case CMD_TOGGLE_MOX: {
    radio_toggle_mox();
    g_idle_add(ext_vfo_update, NULL);
    send_mox(remoteclient.socket, mox);
  }
  break;

  case CMD_MOX: {
    radio_set_mox(header->b1);
    g_idle_add(ext_vfo_update, NULL);
    send_mox(remoteclient.socket, mox);
  }
  break;

  case CMD_VOX: {
    //
    // Vox is handled in the client, so do a  mox update
    // but report back properly
    //
    radio_set_mox(header->b1);
    g_idle_add(ext_vfo_update, NULL);

    if (mox != header->b1) {
      send_vox(remoteclient.socket, mox);
    }
  }
  break;

  case CMD_TUNE: {
    if (can_transmit) {
      full_tune = from_short(header->s1);
      memory_tune = from_short(header->s2);
      radio_set_tune(header->b1);
      g_idle_add(ext_vfo_update, NULL);

      if (transmitter->tune != header->b1) {
        send_tune(remoteclient.socket, transmitter->tune);
      }
    }
  }
  break;

  case CMD_TWOTONE: {
    if (can_transmit) {
      radio_set_twotone(transmitter, header->b1);
      g_idle_add(ext_vfo_update, NULL);
      send_twotone(remoteclient.socket, transmitter->twotone);
    }
  }
  break;

  case CMD_AGC_GAIN: {
    //
    // The client sends gain and hang_threshold
    //
    const AGC_GAIN_COMMAND *agc_gain_command = (AGC_GAIN_COMMAND *)data;
    int id = agc_gain_command->id;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->agc_hang_threshold = from_double(agc_gain_command->hang_thresh);
      suppress_popup_sliders++;
      radio_set_agc_gain(id, from_double(agc_gain_command->gain));
      suppress_popup_sliders--;
      rx_set_agc(rx);
      //
      // Now hang and thresh have been calculated and need be sent back
      //
      send_agc_gain(remoteclient.socket, rx);
    }
  }
  break;

  case CMD_RFGAIN: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *) data;
    suppress_popup_sliders++;
    radio_set_rf_gain(command->header.b1, from_double(command->dbl));
    suppress_popup_sliders--;
  }
  break;

  case CMD_ATTENUATION: {
    int id = header->b1;
    int att = (double) from_short(header->s1);
    suppress_popup_sliders++;
    radio_set_attenuation(id, att);
    suppress_popup_sliders--;
  }
  break;

  case CMD_SQUELCH: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    int id = command->header.b1;
    int en = command->header.b2;
    double val = from_double(command->dbl);
    suppress_popup_sliders++;
    radio_set_squelch(id, val);
    radio_set_squelch_enable(id, en);
    suppress_popup_sliders--;
  }
  break;

  case CMD_NOISE: {
    const NOISE_COMMAND *command = (NOISE_COMMAND *)data;
    int id = command->id;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->nb                     = command->nb;
      rx->nr                     = command->nr;
      rx->anf                    = command->anf;
      rx->snb                    = command->snb;
      rx->nb2_mode               = command->nb2_mode;
      rx->nr_agc                 = command->nr_agc;
      rx->nr2_gain_method        = command->nr2_gain_method;
      rx->nr2_npe_method         = command->nr2_npe_method;
      rx->nr2_ae                 = command->nr2_ae;
      rx->nb_tau                 = from_double(command->nb_tau);
      rx->nb_hang                = from_double(command->nb_hang);
      rx->nb_advtime             = from_double(command->nb_advtime);
      rx->nb_thresh              = from_double(command->nb_thresh);
      rx->nr2_trained_threshold  = from_double(command->nr2_trained_threshold);
      rx->nr2_trained_t2         = from_double(command->nr2_trained_t2);
#ifdef EXTNR
      rx->nr4_reduction_amount   = from_double(command->nr4_reduction_amount);
      rx->nr4_smoothing_factor   = from_double(command->nr4_smoothing_factor);
      rx->nr4_whitening_factor   = from_double(command->nr4_whitening_factor);
      rx->nr4_noise_rescale      = from_double(command->nr4_noise_rescale);
      rx->nr4_post_threshold     = from_double(command->nr4_post_threshold);
#endif
      rx_set_noise(rx);
      send_rx_data(remoteclient.socket, id);
    }
  }
  break;

  case CMD_ADC: {
    int id = header->b1;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->adc = header->b2;
      rx_change_adc(rx);
    }
  }
  break;

  case CMD_BANDSTACK: {
    int old = header->b1;
    int new = header->b2;
    int id = active_receiver->id;
    int b = vfo[id].band;
    vfo_bandstack_changed(new);
    //
    // The "old" bandstack may have changed.
    // The mode, and thus all mode settings, may have changed
    //
    send_bandstack_data(remoteclient.socket, b, old);
    send_vfo_data(remoteclient.socket, id);
    send_rx_data(remoteclient.socket, id);
    send_tx_data(client_socket);
  }
  break;

  case CMD_BAND_SEL: {
    int v = header->b1;
    int b = header->b2;
    int oldband = vfo[v].band;
    vfo_id_band_changed(v, b);
    //
    // Update bandstack data of "old" band
    //
    const BAND *band = band_get_band(oldband);

    for (int s = 0; s < band->bandstack->entries; s++) {
      send_bandstack_data(remoteclient.socket, oldband, s);
    }

    //
    // A band change may come with a mode change, and this
    // then changes a lot of settings. So send all receiver,
    // transmitter, and VFO data
    //
    for (int id = 0; id < RECEIVERS; id++) {
      send_rx_data(remoteclient.socket, id);
    }

    send_tx_data(remoteclient.socket);
    send_vfo_data(remoteclient.socket, VFO_A);
    send_vfo_data(remoteclient.socket, VFO_B);
  }
  break;

  case CMD_MODE: {
    int v = header->b1;
    int m = header->b2;
    vfo_mode_changed(m);
    //
    // A change of the mode implies that all sorts of other settings
    // those "stored with the mode" are changed as well. So we need
    // to send back VFO, receiver, and transmitter data
    //
    send_vfo_data(remoteclient.socket, v);
    send_rx_data(remoteclient.socket, v);
    send_tx_data(remoteclient.socket);
  }
  break;

  case CMD_FILTER_VAR: {
    //
    // Update filter edges
    //
    int m = header->b1;
    int f = header->b2;

    if (f == filterVar1 || f == filterVar2) {
      filters[m][f].low =  from_short(header->s1);
      filters[m][f].high =  from_short(header->s2);
    }

    //
    // Perform a "filter changed" operation  on all receivers
    // that use THIS filter
    //
    for (int v = 0; v < receivers; v++) {
      if ((vfo[v].mode == m) && (vfo[v].filter == f)) {
        vfo_id_filter_changed(v, f);
        send_rx_filter_cut(remoteclient.socket, v);
      }
    }

    if (can_transmit) {
      send_tx_filter_cut(remoteclient.socket);
    }
  }
  break;

  case CMD_FILTER_SEL: {
    //
    // Set the new filter.
    //
    int v = header->b1;
    int f = header->b2;
    vfo_id_filter_changed(v, f);

    //
    // AGC line positions have changed
    // filter edges in receiver(s) may have changed
    //
    for (int id = 0; id < receivers; id++) {
      send_rx_filter_cut(remoteclient.socket, id);
      send_agc_gain(remoteclient.socket, receiver[id]);
    }

    if (can_transmit) {
      send_tx_filter_cut(remoteclient.socket);
    }

    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case CMD_DEVIATION: {
    int id = header->b1;
    vfo[id].deviation = from_short(header->s1);

    if (id < receivers) {
      rx_set_filter(receiver[id]);
      send_rx_filter_cut(remoteclient.socket, id);
    }

    if (can_transmit) {
      tx_set_filter(transmitter);
      send_tx_filter_cut(remoteclient.socket);
    }

    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case CMD_SOAPY_RXANT: {
    if (device == SOAPYSDR_USB_DEVICE) {
      int id = header->b1;
      int ant = header->b2;
      adc[id].antenna = ant;
#ifdef SOAPYSDR
      soapy_protocol_set_rx_antenna(id, ant);
#endif
    }
  }
  break;

  case CMD_SOAPY_TXANT: {
    if (device == SOAPYSDR_USB_DEVICE && can_transmit) {
      transmitter->antenna = header->b1;
#ifdef SOAPYSDR
      soapy_protocol_set_tx_antenna(transmitter->antenna);
#endif
    }
  }
  break;

  case CMD_SOAPY_AGC: {
    if (device == SOAPYSDR_USB_DEVICE) {
      int id = header->b1;
      int agc = header->b2;
      adc[id].agc = agc;
#ifdef SOAPYSDR
      soapy_protocol_set_automatic_gain(id, agc);

      if (!agc) { soapy_protocol_set_rx_gain(id); }

#endif
    }
  }
  break;

  case CMD_SPLIT: {
    if (can_transmit) {
      split = header->b1;
      tx_set_mode(transmitter, vfo_get_tx_mode());
      g_idle_add(ext_vfo_update, NULL);
      send_tx_data(remoteclient.socket);
      send_rx_data(remoteclient.socket, 0);
    }
  }
  break;

  case CMD_SIDETONEFREQ: {
    cw_keyer_sidetone_frequency = from_short(header->s1);
    rx_filter_changed(active_receiver);
    schedule_high_priority();
    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case CMD_CW: {
    tx_queue_cw_event(header->b1, (from_short(header->s1) << 12) | (from_short(header->s2) & 0xFFF));
  }
  break;

  case CMD_SAT: {
    sat_mode = header->b1;
    g_idle_add(ext_vfo_update, NULL);
    send_sat(remoteclient.socket, sat_mode);
  }
  break;

  case CMD_DUP: {
    radio_set_duplex(header->b1);
    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case CMD_LOCK: {
    locked = header->b1;
    g_idle_add(ext_vfo_update, NULL);
    send_lock(remoteclient.socket, locked);
  }
  break;

  case CMD_CTUN: {
    int v = header->b1;
    vfo[v].ctun = header->b2;

    if (!vfo[v].ctun) {
      vfo[v].offset = 0;
    }

    vfo[v].ctun_frequency = vfo[v].frequency;
    rx_set_offset(active_receiver);
    g_idle_add(ext_vfo_update, NULL);
    send_vfo_data(remoteclient.socket, v);
  }
  break;

  case CMD_TX_FPS:
    if (can_transmit) {
      transmitter->fps = header->b2;
      tx_set_framerate(transmitter);
    }

    break;

  case CMD_RX_FPS: {
    int id = header->b1;

    if (id < receivers) {
      receiver[id]->fps = header->b2;
      rx_set_framerate(receiver[id]);
    }
  }
  break;

  case CMD_RX_SELECT: {
    int id = header->b1;

    if (id < receivers) {
      rx_set_active(receiver[id]);
    }
  }
  break;

  case CMD_VFO_A_TO_B: {
    vfo_a_to_b();
    send_vfo_data(remoteclient.socket, VFO_B);

    if (receivers > 1) {
      send_rx_data(remoteclient.socket, 1);
    }

    if (can_transmit) {
      send_tx_data(remoteclient.socket);
    }
  }
  break;

  case CMD_VFO_B_TO_A: {
    vfo_b_to_a();
    send_vfo_data(remoteclient.socket, VFO_A);
    send_rx_data(remoteclient.socket, 0);

    if (can_transmit) {
      send_tx_data(remoteclient.socket);
    }
  }
  break;

  case CMD_VFO_SWAP: {
    vfo_a_swap_b();
    send_vfo_data(remoteclient.socket, VFO_A);
    send_vfo_data(remoteclient.socket, VFO_B);
    send_rx_data(remoteclient.socket, 0);

    if (receivers > 1) {
      send_rx_data(remoteclient.socket, 1);
    }

    if (can_transmit) {
      send_tx_data(remoteclient.socket);
    }
  }
  break;

  case CMD_RIT: {
    int id = header->b1;
    vfo_id_rit_value(id, from_short(header->s1));
    vfo_id_rit_onoff(id, header->b2);
    send_vfo_data(remoteclient.socket, id);
  }
  break;

  case CMD_XIT: {
    int id = header->b1;
    vfo_id_xit_value(id, from_short(header->s1));
    vfo_id_xit_onoff(id, header->b2);
    send_vfo_data(remoteclient.socket, id);
  }
  break;

  case CMD_SAMPLE_RATE: {
    const U64_COMMAND *command = (U64_COMMAND *)data;
    int id = command->header.b1;

    if (id < receivers) {
      long long rate = from_ll(command->u64);

      if (protocol == NEW_PROTOCOL) {
        rx_change_sample_rate(receiver[id], (int)rate);
      } else {
        radio_change_sample_rate((int)rate);
      }

      // If the sample rate was illegal, the actual sample rate is
      // not what has been sent. So return the actual value.
      send_sample_rate(remoteclient.socket, id, receiver[id]->sample_rate);
    }
  }
  break;

  case CMD_RECEIVERS: {
    int r = header->b1;
    radio_change_receivers(r);
    send_receivers(remoteclient.socket, receivers);

    // In P1, activating RX2 aligns its sample rate with RX1
    if (receivers == 2) {
      send_rx_data(remoteclient.socket, 1);
    }
  }
  break;

  case CMD_RIT_STEP: {
    int v = header->b1;
    int step = from_short(header->s1);
    vfo_id_set_rit_step(v, step);
    send_vfo_data(remoteclient.socket, v);
  }
  break;

  case CMD_FILTER_BOARD: {
    radio_load_filters(header->b1);
    send_radio_data(remoteclient.socket);

    if (filter_board == N2ADR) {
      // OC settings for 160m ... 10m have been set
      for (int b = band160; b <= band10; b++) {
        send_band_data(remoteclient.socket, b);
      }
    }
  }
  break;

  case CMD_REGION: {
    region = header->b1;
    radio_change_region(region);
    const BAND *band = band_get_band(band60);

    for (int s = 0; s < band->bandstack->entries; s++) {
      send_bandstack_data(remoteclient.socket, band60, s);
    }
  }
  break;

  case CMD_CWPEAK: {
    vfo_id_cwpeak_changed(header->b1, header->b2);
  }
  break;

  case CMD_ANAN10E: {
    radio_set_anan10E(header->b1);
    send_radio_data(remoteclient.socket);
  }
  break;

  case CMD_RX_EQ: {
    const EQUALIZER_COMMAND *command = (EQUALIZER_COMMAND *)data;
    int id = command->id;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->eq_enable = command->enable;

      for (int i = 0; i < 11; i++) {
        rx->eq_freq[i] = from_double(command->freq[i]);
        rx->eq_gain[i] = from_double(command->gain[i]);
      }

      rx_set_equalizer(rx);
    }
  }
  break;

  case CMD_TX_EQ:
    if (can_transmit) {
      const EQUALIZER_COMMAND *command = (EQUALIZER_COMMAND *)data;
      transmitter->eq_enable = command->enable;

      for (int i = 0; i < 11; i++) {
        transmitter->eq_freq[i] = from_double(command->freq[i]);
        transmitter->eq_gain[i] = from_double(command->gain[i]);
      }

      tx_set_equalizer(transmitter);
    }

    break;

  case CMD_RX_DISPLAY: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    int id = command->header.b1;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->display_detector_mode = command->header.b2;
      rx->display_average_mode = from_short(command->header.s1);
      rx->display_average_time = from_double(command->dbl);
      rx_set_average(rx);
      rx_set_detector(rx);
    }
  }
  break;

  case CMD_TX_DISPLAY:
    if (can_transmit) {
      const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
      transmitter->display_detector_mode = command->header.b2;
      transmitter->display_average_mode = from_short(command->header.s1);
      transmitter->display_average_time = from_double(command->dbl);
      tx_set_average(transmitter);
      tx_set_detector(transmitter);
    }

    break;

  case CMD_RADIOMENU: {
    const RADIOMENU_DATA *command = (RADIOMENU_DATA *)data;
    mic_ptt_tip_bias_ring = command->mic_ptt_tip_bias_ring;
    sat_mode = command->sat_mode;
    mic_input_xlr = command->mic_input_xlr;
    atlas_clock_source_10mhz = command->atlas_clock_source_10mhz;
    atlas_clock_source_128mhz = command->atlas_clock_source_128mhz;
    atlas_mic_source = command->atlas_mic_source;
    atlas_penelope = command->atlas_penelope;
    atlas_janus = command->atlas_janus;
    mic_ptt_enabled = command->mic_ptt_enabled;
    mic_bias_enabled = command->mic_bias_enabled;
    pa_enabled = command->pa_enabled;
    mute_spkr_amp = command->mute_spkr_amp;
    mute_spkr_xmit = command->mute_spkr_xmit;
    hl2_audio_codec = command->hl2_audio_codec;
    soapy_iqswap = command->soapy_iqswap;
    enable_tx_inhibit = command->enable_tx_inhibit;
    enable_auto_tune = command->enable_auto_tune;
    new_pa_board = command->new_pa_board;
    tx_out_of_band_allowed = command->tx_out_of_band_allowed;
    OCtune = command->OCtune;
    //
    rx_gain_calibration = from_short(command->rx_gain_calibration);
    OCfull_tune_time = from_short(command->OCfull_tune_time);
    OCmemory_tune_time = from_short(command->OCmemory_tune_time);
    //
    frequency_calibration = from_ll(command->frequency_calibration);
    //
    schedule_transmit_specific();
    schedule_general();
    schedule_high_priority();
  }
  break;

  case CMD_RXMENU: {
    //
    // cannot use send_agc since we transfer bypass info
    // from both ADCs
    // Data included here is what is changed in the RX menu
    //
    const RXMENU_DATA *command = (RXMENU_DATA *)data;
    int id = command->id;
    adc[id].dither = command->dither;
    adc[id].random = command->random;
    adc[id].preamp = command->preamp;
    adc[id].alex_attenuation = command->alex_attenuation;
    adc[0].filter_bypass = command->adc0_filter_bypass;
    adc[1].filter_bypass = command->adc1_filter_bypass;

    if (id == 0) {
      radio_set_alex_attenuation(command->alex_attenuation);
    }

    schedule_receive_specific();
    schedule_high_priority();
  }
  break;

  case CMD_DIVERSITY: {
    const DIVERSITY_COMMAND *command = (DIVERSITY_COMMAND *)data;
    suppress_popup_sliders++;
    radio_set_diversity(command->diversity_enabled);
    radio_set_diversity_gain(from_double(command->div_gain));
    radio_set_diversity_phase(from_double(command->div_phase));
    suppress_popup_sliders--;
  }
  break;

  case CMD_TXFILTER: {
    if (can_transmit) {
      transmitter->use_rx_filter = header->b1;
      transmitter->default_filter_low = from_short(header->s1);
      transmitter->default_filter_high = from_short(header->s2);
      tx_set_filter(transmitter);
    }
  }
  break;

  case CMD_PREEMP: {
    if (can_transmit) {
      transmitter->pre_emphasize = header->b1;
      tx_set_pre_emphasize(transmitter);
    }
  }
  break;

  case CMD_CTCSS: {
    if (can_transmit) {
      transmitter->ctcss_enabled = header->b1;
      transmitter->ctcss = header->b2;
      tx_set_ctcss(transmitter);
      send_tx_data(remoteclient.socket);
      g_idle_add(ext_vfo_update, NULL);
    }
  }
  break;

  case CMD_AMCARRIER: {
    if (can_transmit) {
      const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
      transmitter->am_carrier_level = from_double(command->dbl);
      tx_set_am_carrier_level(transmitter);
      send_tx_data(remoteclient.socket);
    }
  }
  break;

  case CMD_DIGIMAX: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    int mode = vfo_get_tx_mode();
    drive_digi_max = from_double(command->dbl);

    if ((mode == modeDIGL || mode == modeDIGU) && transmitter->drive > drive_digi_max + 0.5) {
      suppress_popup_sliders++;
      radio_set_drive(drive_digi_max);
      suppress_popup_sliders--;
    }
  }
  break;

  case CMD_TXMENU: {
    if (can_transmit) {
      const TXMENU_DATA *command = (TXMENU_DATA *)data;
      transmitter->tune_drive = command->tune_drive;
      transmitter->tune_use_drive = command->tune_use_drive;
      transmitter->swr_protection = command->swr_protection;
      transmitter->swr_alarm = from_double(command->swr_alarm);
      mic_boost = command->mic_boost;
      mic_linein = command->mic_linein;
      linein_gain = from_double(command->linein_gain);
      schedule_transmit_specific();
      send_tx_data(remoteclient.socket);
    }
  }
  break;

  case CMD_COMPRESSOR: {
    if (can_transmit) {
      const COMPRESSOR_DATA *command = (COMPRESSOR_DATA *)data;
      transmitter->compressor = command->compressor;
      transmitter->cfc = command->cfc;
      transmitter->cfc_eq = command->cfc_eq;
      transmitter->compressor_level = from_double(command->compressor_level);

      for (int i = 0; i < 11; i++) {
        transmitter->cfc_freq[i] = from_double(command->cfc_freq[i]);
        transmitter->cfc_lvl [i] = from_double(command->cfc_lvl [i]);
        transmitter->cfc_post[i] = from_double(command->cfc_post[i]);
      }

      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }
  }
  break;

  case CMD_DEXP: {
    if (can_transmit) {
      const DEXP_DATA  *command = (DEXP_DATA *)data;
      transmitter->dexp = command->dexp;
      transmitter->dexp_filter = command->dexp_filter;
      transmitter->dexp_trigger = from_short(command->dexp_trigger);
      transmitter->dexp_exp = from_short(command->dexp_exp);
      transmitter->dexp_filter_low = from_short(command->dexp_filter_low);
      transmitter->dexp_filter_high = from_short(command->dexp_filter_high);
      transmitter->dexp_tau = from_double(command->dexp_tau);
      transmitter->dexp_attack = from_double(command->dexp_attack);
      transmitter->dexp_release = from_double(command->dexp_release);
      transmitter->dexp_hold = from_double(command->dexp_hold);
      transmitter->dexp_hyst = from_double(command->dexp_hyst);
      tx_set_dexp(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }
  }
  break;

  case CMD_CAPTURE: {
    schedule_action(CAPTURE, PRESSED, 0);
  }
  break;

  case CMD_PSONOFF: {
    if (can_transmit) {
      tx_ps_onoff(transmitter, header->b1);
    }
  }
  break;

  case CMD_PSRESET: {
    if (can_transmit) {
      tx_ps_reset(transmitter);
    }
  }
  break;

  case CMD_PSRESUME: {
    if (can_transmit) {
      tx_ps_resume(transmitter);
    }
  }
  break;

  case CMD_PSATT: {
    if (can_transmit) {
      transmitter->auto_on = header->b1;
      transmitter->feedback = header->b2;
      transmitter->attenuation = from_short(header->s1);
      adc[2].antenna = from_short(header->s2);
      schedule_high_priority();
    }
  }
  break;

  case CMD_PSPARAMS: {
    if (can_transmit) {
      const PS_PARAMS  *command = (PS_PARAMS *)data;
      transmitter->ps_ptol = command->ps_ptol;
      transmitter->ps_oneshot = command->ps_oneshot;
      transmitter->ps_map = command->ps_map;
      transmitter->ps_setpk = from_double(command->ps_setpk);
      tx_ps_setparams(transmitter);
    }
  }
  break;

  case CMD_BINAURAL: {
    RECEIVER *rx = receiver[header->b1];
    rx->binaural = header->b2;
    rx_set_af_binaural(rx);
  }
  break;

  case CMD_RXFFT: {
    const U64_COMMAND *command = (U64_COMMAND *)data;
    RECEIVER *rx = receiver[command->header.b1];
    rx->low_latency = command->header.b2;
    rx->fft_size = from_ll(command->u64);
    rx_set_fft_latency(rx);
    rx_set_fft_size(rx);
  }
  break;

  case CMD_TXFFT: {
    if (can_transmit) {
      const U64_COMMAND *command = (U64_COMMAND *)data;
      transmitter->fft_size = from_ll(command->u64);
      tx_set_fft_size(transmitter);
    }
  }
  break;

  case CMD_PATRIM: {
    const PATRIM_DATA *command = (PATRIM_DATA *)data;
    pa_power = command->pa_power;

    for (int i = 0; i < 11; i++) {
      pa_trim[i] = from_double(command->pa_trim[i]);
    }
  }
  break;

  case CMD_RX_FILTER_CUT: {
    int id = header->b1;

    if (id < receivers) {
      RECEIVER *rx = receiver[id];
      rx->filter_low = from_short(header->s1);
      rx->filter_high = from_short(header->s2);
      rx_set_bandpass(rx);
      rx_set_agc(rx);
      send_agc_gain(remoteclient.socket, rx);
      g_idle_add(ext_vfo_update, NULL);
    }
  }
  break;

  //
  // This command never goes from the client to the server
  //
  case CMD_TX_FILTER_CUT:
    if (can_transmit) {
      TRANSMITTER *tx = transmitter;
      tx->filter_low = from_short(header->s1);
      tx->filter_high = from_short(header->s2);
      tx_set_bandpass(tx);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  default: {
    t_print("%s: forgotten case type=%d\n", __FUNCTION__, type);
  }
  break;
  }

  g_free(data);
  return G_SOURCE_REMOVE;
}
