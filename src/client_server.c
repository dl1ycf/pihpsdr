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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <strings.h>
#ifndef __APPLE__
  #include <endian.h>
#endif
#include <semaphore.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "adc.h"
#include "audio.h"
#include "client_server.h"
#include "band.h"
#include "dac.h"
#include "diversity_menu.h"
#include "discovered.h"
#include "equalizer_menu.h"
#include "ext.h"
#include "filter.h"
#include "iambic.h"
#include "main.h"
#include "message.h"
#include "new_protocol.h"
#include "noise_menu.h"
#include "radio.h"
#include "radio_menu.h"
#include "receiver.h"
#include "sliders.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "store.h"
#include "store_menu.h"
#include "transmitter.h"
#include "vfo.h"
#include "vox.h"
#include "zoompan.h"

//
// Four bytes to sync on
//
static uint8_t syncbytes[4] = {0xFA, 0xFA, 0xAF, 0xAF};

#define SYNC(x) memcpy(x, syncbytes, sizeof(syncbytes))

#define LISTEN_PORT 50000

int listen_port = LISTEN_PORT;

REMOTE_CLIENT remoteclient = { 0 };

GMutex client_mutex;

static char title[256];

int hpsdr_server = FALSE;
char hpsdr_pwd[HPSDR_PWD_LEN] = { 0 };

int client_socket = -1;
GThread *client_thread_id;
int start_spectrum(void *data);
gboolean remote_started = FALSE;

static GThread *listen_thread_id;
static int server_running = 0;
static int client_running = 0;
static int listen_socket = -1;

//
// Audio
//
#define MIC_RING_BUFFER_SIZE 9600
#define MIC_RING_LOW         3000

static short *mic_ring_buffer;
static volatile short  mic_ring_outpt = 0;
static volatile short  mic_ring_inpt = 0;

static int txaudio_buffer_index = 0;
static int rxaudio_buffer_index[2] = { 0, 0};
RXAUDIO_DATA rxaudio_data[2];  // for up to 2 receivers

TXAUDIO_DATA txaudio_data;

static int remote_command(void * data);

static GMutex accumulated_mutex;
static int accumulated_steps[2] = {0, 0};
static long long accumulated_hz[2] = {0LL, 0LL};
static int accumulated_round[2] = {FALSE, FALSE};
guint check_vfo_timer_id = 0;

//
// htonll and friends are macros, and this may have
// side effects. Better use functions that operate
// on simple variables (not expressions).
// Futhermore, explicit casting is done here once for all
//

static inline uint64_t to_double(double x) {
  uint64_t u64 = (x + 9.0E8) * 1.0E10;
#ifdef __APPLE__
  uint64_t ret = htonll(u64);
#else
  uint64_t ret = htobe64(u64);
#endif
  return ret;
}

static inline uint64_t to_ll(long long x) {
#ifdef __APPLE__
  uint64_t ret = htonll(x);
#else
  uint64_t ret = htobe64(x);
#endif
  return ret;
}

static inline uint32_t to_int(int x) {
  int32_t s32 = x;
  uint32_t ret = htonl(s32);
  return ret;
}

static inline uint16_t to_short(int x) {
  int16_t s16 = x;
  uint16_t ret = htons(s16);
  return ret;
}

static inline double from_double(uint64_t y) {
#ifdef __APPLE__
  uint64_t u64 = ntohll(y);
#else
  uint64_t u64 = be64toh(y);
#endif
  return (1.0E-10 * u64 - 9.0E8);
}

static inline long long from_ll(uint64_t y) {
#ifdef __APPLE__
  uint64_t u64 = ntohll(y);
#else
  uint64_t u64 = be64toh(y);
#endif
  return (long long) u64;
}

static inline int from_int(uint32_t y) {
  int32_t s32 = ntohl(y);
  return (int) s32;
}

static inline int from_short(uint16_t y) {
  int16_t s16 = ntohs(y);
  return (int) s16;
}

//
// From a challenge in s, calculate a password hash with  loop and salt
// The number of bytes in the challenge equals the length of a SHA512
// hash. Note the length of the hash is the "digest length", and the
// maximum length of the string to be hashed is 2*digest_length + 4
// passwords are chopped to a max length of the digest length.
//
// Note: digest length for SHA512 is 64 bytes.
//
static void generate_pwd_hash(unsigned char *s, unsigned char *hash, const char *pwd) {
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

static int recv_bytes(int s, char *buffer, int bytes) {
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
static int send_bytes(int s, char *buffer, int bytes) {
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

void server_tx_audio(short sample) {
  //
  // This is called in the client and collects data to be
  // sent to the server
  //
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

    if (radio_is_transmitting() && txmode != modeCWU && txmode != modeCWL && !tune && !transmitter->twotone) {
      //
      // The actual transmission of the mic audio samples only takes  place
      // if we *need* them (note VOX is handled locally)
      //
      SYNC(txaudio_data.header.sync);
      txaudio_data.header.data_type = to_short(INFO_TXAUDIO);
      txaudio_data.numsamples = from_short(txaudio_buffer_index);

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

void remote_rxaudio(const RECEIVER *rx, short left_sample, short right_sample) {
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
    rxaudio_data[id].numsamples = from_short(rxaudio_buffer_index[id]);
    send_bytes(remoteclient.socket, (char *)&rxaudio_data[id], sizeof(RXAUDIO_DATA));
    rxaudio_buffer_index[id] = 0;
  }
}

//
// Note that this is now only called when
// - display mutex is locked
// - displaying is set and a pixel_samples contain valid data
//
void remote_send_rxspectrum(int id) {
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
  spectrum_data.zoom  = rx->zoom;
  spectrum_data.pan   = to_short(rx->pan);
  spectrum_data.meter = to_double(rx->meter);
  spectrum_data.width = to_short(rx->width);
  samples = rx->pixel_samples;
  numsamples = rx->width;

  if (numsamples > SPECTRUM_DATA_SIZE) { numsamples = SPECTRUM_DATA_SIZE; }

  for (int i = 0; i < numsamples; i++) {
    int s = ((int) samples[i + rx->pan]) + 200;  // -200dBm ... 55dBm maps to 0 ... 55

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

void remote_send_txspectrum() {
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
      tx_ps_getpk(transmitter);
      ps_data.ps_getpk = to_double(transmitter->ps_getpk);
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
  disp_data.adc0_overload = adc0_overload;
  disp_data.adc1_overload = adc1_overload;
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

static void send_start_radio(int sock) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_START_RADIO);
  send_bytes(sock, (char *)&header, sizeof(HEADER));
}

void send_rxmenu(int sock, int id) {
  RXMENU_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(CMD_RXMENU);
  data.id = id;
  data.dither = receiver[id]->dither;
  data.random = receiver[id]->random;
  data.preamp = receiver[id]->preamp;
  data.adc0_filter_bypass = adc0_filter_bypass;
  data.adc1_filter_bypass = adc1_filter_bypass;
  send_bytes(sock, (char *)&data, sizeof(RXMENU_DATA));
}

void send_radiomenu(int sock) {
  RADIOMENU_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(CMD_RADIOMENU);
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
  data.hl2_audio_codec = hl2_audio_codec;
  data.soapy_iqswap = soapy_iqswap;
  data.enable_tx_inhibit = enable_tx_inhibit;
  data.enable_auto_tune = enable_auto_tune;
  data.new_pa_board = new_pa_board;
  data.tx_out_of_band_allowed = tx_out_of_band_allowed;
  data.OCtune = OCtune;
  //
  data.rx_gain_calibration = to_short(rx_gain_calibration);
  data.OCfull_tune_time = to_short(OCfull_tune_time);
  data.OCmemory_tune_time = to_short(OCmemory_tune_time);
  //
  data.frequency_calibration = to_ll(frequency_calibration);
  send_bytes(sock, (char *)&data, sizeof(RADIOMENU_DATA));
}

void send_memory_data(int sock, int index) {
  MEMORY_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_MEMORY);
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
  data.deviation          = to_short(mem[index].deviation);
  data.alt_deviation      = to_short(mem[index].alt_deviation);
  //
  data.frequency          = to_ll(mem[index].frequency);
  data.ctun_frequency     = to_ll(mem[index].ctun_frequency);
  data.alt_frequency      = to_ll(mem[index].frequency);
  data.alt_ctun_frequency = to_ll(mem[index].ctun_frequency);
  send_bytes(sock, (char *)&data, sizeof(MEMORY_DATA));
}

void send_band_data(int sock, int b) {
  BAND_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_BAND);
  BAND *band = band_get_band(b);
  snprintf(data.title, sizeof(data.title), "%s", band->title);
  data.band = b;
  data.OCrx = band->OCrx;
  data.OCtx = band->OCtx;
  data.alexRxAntenna = band->alexRxAntenna;
  data.alexTxAntenna = band->alexTxAntenna;
  data.alexAttenuation = band->alexAttenuation;
  data.disablePA = band->disablePA;
  data.current = band->bandstack->current_entry;
  data.gain = to_short(band->gain);
  data.pa_calibration = to_double(band->pa_calibration);
  data.frequencyMin = to_ll(band->frequencyMin);
  data.frequencyMax = to_ll(band->frequencyMax);
  data.frequencyLO = to_ll(band->frequencyLO);
  data.errorLO = to_ll(band->errorLO);
  send_bytes(sock, (char *)&data, sizeof(BAND_DATA));
}

void send_xvtr_changed(int sock) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_XVTR);
  send_bytes(sock, (char *)&header, sizeof(HEADER));
}

void send_bandstack_data(int sock, int b, int stack) {
  BANDSTACK_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_BANDSTACK);
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
  data.deviation = to_short(entry->deviation);
  data.frequency = to_ll(entry->frequency);
  data.ctun_frequency = to_ll(entry->ctun_frequency);
  send_bytes(sock, (char *)&data, sizeof(BANDSTACK_DATA));
}

void send_radio_data(int sock) {
  RADIO_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_RADIO);
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
  data.adc0_filter_bypass = adc0_filter_bypass;
  data.adc1_filter_bypass = adc1_filter_bypass;
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
  data.soapy_rx_antennas = radio->info.soapy.rx_antennas;
  data.soapy_tx_antennas = radio->info.soapy.tx_antennas;
  data.soapy_rx_gains = radio->info.soapy.rx_gains;
  data.soapy_tx_gains = radio->info.soapy.tx_gains;
  data.soapy_tx_channels = radio->info.soapy.tx_channels;
  data.soapy_rx_has_automatic_gain = radio->info.soapy.rx_has_automatic_gain;
  //
  memcpy(data.soapy_hardware_key, radio->info.soapy.hardware_key, 64);
  memcpy(data.soapy_driver_key, radio->info.soapy.driver_key, 64);

  for (int i = 0; i < radio->info.soapy.rx_antennas; i++) {
    memcpy(data.soapy_rx_antenna[i], radio->info.soapy.rx_antenna[i], 64);
  }

  for (int i = 0; i < radio->info.soapy.tx_antennas; i++) {
    memcpy(data.soapy_tx_antenna[i], radio->info.soapy.tx_antenna[i], 64);
  }

  for (int i = 0; i < radio->info.soapy.rx_gains; i++) {
    memcpy(data.soapy_rx_gain_elem_name[i], radio->info.soapy.rx_gain_elem_name[i], 64);
  }

  for (int i = 0; i < radio->info.soapy.tx_gains; i++) {
    memcpy(data.soapy_tx_gain_elem_name[i], radio->info.soapy.tx_gain_elem_name[i], 64);
  }

  //
  data.pa_power = to_short(pa_power);
  data.OCfull_tune_time = to_short(OCfull_tune_time);
  data.OCmemory_tune_time = to_short(OCmemory_tune_time);
  data.cw_keyer_sidetone_frequency = to_short(cw_keyer_sidetone_frequency);
  data.rx_gain_calibration = to_short(rx_gain_calibration);
  data.device = to_short(device);
  data.tx_filter_low = to_short(tx_filter_low);
  data.tx_filter_high = to_short(tx_filter_high);
  data.display_width = to_short(display_width);
  //
  data.drive_min = to_double(drive_min);
  data.drive_max = to_double(drive_max);
  data.drive_digi_max = to_double(drive_digi_max);
  data.div_gain = to_double(div_gain);
  data.div_phase = to_double(div_phase);

  for (int i = 0; i < 11; i++) {
    data.pa_trim[i] = to_double(pa_trim[i]);
  }

  data.soapy_rx_gain_step = to_double(radio->info.soapy.rx_gain_step);
  data.soapy_rx_gain_min  = to_double(radio->info.soapy.rx_gain_min );
  data.soapy_rx_gain_max  = to_double(radio->info.soapy.rx_gain_max );
  data.soapy_tx_gain_step = to_double(radio->info.soapy.tx_gain_step);
  data.soapy_tx_gain_min  = to_double(radio->info.soapy.tx_gain_min );
  data.soapy_tx_gain_max  = to_double(radio->info.soapy.tx_gain_max );

  for (int i = 0; i < radio->info.soapy.rx_gains; i++) {
    data.soapy_rx_gain_elem_step[i] = to_double(radio->info.soapy.rx_gain_elem_step[i]);
    data.soapy_rx_gain_elem_min[i] = to_double(radio->info.soapy.rx_gain_elem_min[i]);
    data.soapy_rx_gain_elem_max[i] = to_double(radio->info.soapy.rx_gain_elem_max[i]);
  }

  for (int i = 0; i < radio->info.soapy.tx_gains; i++) {
    data.soapy_tx_gain_elem_step[i] = to_double(radio->info.soapy.tx_gain_elem_step[i]);
    data.soapy_tx_gain_elem_min[i] = to_double(radio->info.soapy.tx_gain_elem_min[i]);
    data.soapy_tx_gain_elem_max[i] = to_double(radio->info.soapy.tx_gain_elem_max[i]);
  }

  //
  data.frequency_calibration = to_ll(frequency_calibration);
  data.soapy_radio_sample_rate = to_ll(soapy_radio_sample_rate);
  data.radio_frequency_min = to_ll(radio->frequency_min);
  data.radio_frequency_max = to_ll(radio->frequency_max);
  send_bytes(sock, (char *)&data, sizeof(RADIO_DATA));
}

void send_dac_data(int sock) {
  DAC_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_DAC);
  data.antenna = dac.antenna;
  data.gain    = to_double(dac.gain);
  send_bytes(sock, (char *)&data, sizeof(DAC_DATA));
}

void send_adc_data(int sock, int i) {
  ADC_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_ADC);
  data.adc = i;
  data.antenna = to_short(adc[i].antenna);
  data.attenuation = to_short(adc[i].attenuation);
  data.gain = to_double(adc[i].gain);
  data.min_gain = to_double(adc[i].min_gain);
  data.max_gain = to_double(adc[i].max_gain);
  send_bytes(sock, (char *)&data, sizeof(ADC_DATA));
}

static void send_tx_data(int sock) {
  if (can_transmit) {
    TRANSMITTER_DATA data;
    const TRANSMITTER *tx = transmitter;
    SYNC(data.header.sync);
    data.header.data_type = to_short(INFO_TRANSMITTER);
    //
    data.id = tx->id;
    data.dac = tx->dac;
    data.display_detector_mode = tx->display_detector_mode;
    data.display_average_mode = tx->display_average_mode;
    data.use_rx_filter = tx->use_rx_filter;
    data.alex_antenna = tx->alex_antenna;
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
    data.fps = to_short(tx->fps);
    data.dexp_filter_low = to_short(tx->dexp_filter_low);
    data.dexp_filter_high = to_short(tx->dexp_filter_high);
    data.dexp_trigger = to_short(tx->dexp_trigger);
    data.dexp_exp = to_short(tx->dexp_exp);
    data.filter_low = to_short(tx->filter_low);
    data.filter_high = to_short(tx->filter_high);
    data.deviation = to_short(tx->deviation);
    data.width = to_short(tx->width);
    data.height = to_short(tx->height);
    data.attenuation = to_short(tx->attenuation);
    data.fft_size = to_ll(tx->fft_size);

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

static void send_rx_data(int sock, int id) {
  RECEIVER_DATA data;
  SYNC(data.header.sync);
  data.header.data_type = to_short(INFO_RECEIVER);
  const RECEIVER *rx = receiver[id];
  data.id                    = rx->id;
  data.adc                   = rx->adc;
  data.agc                   = rx->agc;
  data.nb                    = rx->nb;
  data.nb2_mode              = rx->nb2_mode;
  data.nr                    = rx->nr;
  data.nr_agc                = rx->nr_agc;
  data.nr2_ae                = rx->nr2_ae;
  data.nr2_gain_method       = rx->nr2_gain_method;
  data.nr2_npe_method        = rx->nr2_npe_method;
  data.anf                   = rx->anf;
  data.snb                   = rx->snb;
  data.display_detector_mode = rx->display_detector_mode;
  data.display_average_mode  = rx->display_average_mode;
  data.zoom                  = rx->zoom;
  data.dither                = rx->dither;
  data.random                = rx->random;
  data.preamp                = rx->preamp;
  data.alex_antenna          = rx->alex_antenna;
  data.alex_attenuation      = rx->alex_attenuation;
  data.squelch_enable        = rx->squelch_enable;
  data.binaural              = rx->binaural;
  data.eq_enable             = rx->eq_enable;
  data.smetermode            = rx->smetermode;
  data.low_latency           = rx->low_latency;
  //
  data.fps                   = to_short(rx->fps);
  data.filter_low            = to_short(rx->filter_low);
  data.filter_high           = to_short(rx->filter_high);
  data.deviation             = to_short(rx->deviation);
  data.pan                   = to_short(rx->pan);
  data.width                 = to_short(rx->width);
  //
  data.hz_per_pixel          = to_double(rx->hz_per_pixel);
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

  data.fft_size              = to_ll(rx->fft_size);
  data.sample_rate           = to_ll(rx->sample_rate);
  send_bytes(sock, (char *)&data, sizeof(RECEIVER_DATA));
}

void send_vfo_data(int sock, int v) {
  VFO_DATA vfo_data;
  SYNC(vfo_data.header.sync);
  vfo_data.header.data_type = to_short(INFO_VFO);
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
  vfo_data.rit_step  = to_short(vfo[v].rit_step);
  vfo_data.deviation = to_short(vfo[v].deviation);
  //
  vfo_data.frequency = to_ll(vfo[v].frequency);
  vfo_data.ctun_frequency = to_ll(vfo[v].ctun_frequency);
  vfo_data.rit = to_ll(vfo[v].rit);
  vfo_data.xit = to_ll(vfo[v].xit);
  vfo_data.lo = to_ll(vfo[v].lo);
  vfo_data.offset = to_ll(vfo[v].offset);
  vfo_data.step   = to_ll(vfo[v].step);
  send_bytes(sock, (char *)&vfo_data, sizeof(vfo_data));
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
  // send DAC data structure
  //
  send_dac_data(remoteclient.socket);

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
  for (int i = 0; i < NUM_OF_MEMORYS; i++) {
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
  // If a complete command has been received, put a "remote_command()" with that
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
      t_print("%s: header.sync mismatch: %02x %02x %02x %02x\n",
              __FUNCTION__,
              header.sync[0],
              header.sync[1],
              header.sync[2],
              header.sync[3]);
      int syncs = 0;
      uint8_t c;

      while (syncs != sizeof(syncbytes) && remoteclient.running) {
        bytes_read = recv_bytes(remoteclient.socket, (char *)&c, 1);

        if (bytes_read <= 0) {
          t_print("%: ReadErr for HEADER RESYNC\n", __FUNCTION__);
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
      // The txaudio  command is statically allocated and the data will be IMMEDIATELY
      // (not through the GTK queue) put  to the ring buffer
      //
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
        g_idle_add(remote_command, command);
      }
    }
    break;

    case INFO_BANDSTACK: {
      BANDSTACK_DATA *command = g_new(BANDSTACK_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(BANDSTACK_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
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
        g_idle_add(remote_command, command);
      }
    }
    break;

    case INFO_DAC: {
      //
      // Sending DAC data from the client to the server has a very limited scope:
      // - antenna (for SoapySDR)
      //
      DAC_DATA *command = g_new(DAC_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(DAC_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
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
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_NOISE: {
      NOISE_COMMAND *command = g_new(NOISE_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(NOISE_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_RX_EQ:
    case CMD_TX_EQ: {
      EQUALIZER_COMMAND *command = g_new(EQUALIZER_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(EQUALIZER_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_RADIOMENU: {
      RADIOMENU_DATA *command = g_new(RADIOMENU_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(RADIOMENU_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_RXMENU: {
      RXMENU_DATA *command = g_new(RXMENU_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(RXMENU_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_DIVERSITY: {
      DIVERSITY_COMMAND *command = g_new(DIVERSITY_COMMAND, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(DIVERSITY_COMMAND) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_DEXP: {
      DEXP_DATA *command = g_new(DEXP_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(DEXP_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_COMPRESSOR: {
      COMPRESSOR_DATA *command = g_new(COMPRESSOR_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(COMPRESSOR_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_TXMENU: {
      TXMENU_DATA *command = g_new(TXMENU_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(TXMENU_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_PSPARAMS: {
      PS_PARAMS *command = g_new(PS_PARAMS, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(PS_PARAMS) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
      }
    }
    break;

    case CMD_PATRIM: {
      PATRIM_DATA *command = g_new(PATRIM_DATA, 1);
      command->header = header;

      if (recv_bytes(remoteclient.socket, (char *)command + sizeof(HEADER), sizeof(PATRIM_DATA) - sizeof(HEADER)) > 0) {
        g_idle_add(remote_command, command);
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
        g_idle_add(remote_command, command);
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
        g_idle_add(remote_command, command);
      }
    }
    break;

    //
    // All "header-only" commands simply make a copy of the header and
    // submit that copy  to remote_command().
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
      g_idle_add(remote_command, command);
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

void send_startstop_rxspectrum(int s, int id, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RX_SPECTRUM);
  header.b1 = id;
  header.b2 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_startstop_txspectrum(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TX_SPECTRUM);
  header.b2 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_frequency(int s, int v, long long hz) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_FREQ);
  command.header.b1 = v;
  command.u64 = to_ll(hz);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_vfo_move_to(int s, int v, long long hz) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_MOVETO);
  command.header.b1 = v;
  command.u64 = to_ll(hz);
  send_bytes(s, (char *)&command, sizeof(command));
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

void update_vfo_move(int v, long long hz, int round) {
  g_mutex_lock(&accumulated_mutex);
  accumulated_hz[v] += hz;
  accumulated_round[v] = round;
  g_mutex_unlock(&accumulated_mutex);
}

void send_store(int s, int index) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_STORE);
  header.b1 = index;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_recall(int s, int index) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RCL);
  header.b1 = index;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_stepsize(int s, int v, int stepsize) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_VFO_STEPSIZE);
  command.header.b1 = v;
  command.u64 = to_ll(stepsize);
  send_bytes(s, (char *)&command, sizeof(U64_COMMAND));
}

void send_vfo_step(int s, int v, int steps) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_STEP);
  header.b1 = v;
  header.s1 = to_short(steps);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void update_vfo_step(int v, int steps) {
  g_mutex_lock(&accumulated_mutex);
  accumulated_steps[v] += steps;
  g_mutex_unlock(&accumulated_mutex);
}

void send_zoom(int s, const RECEIVER *rx) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_ZOOM);
  header.b1 = rx->id;
  header.b2 = rx->zoom;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_meter(int s, int metermode, int alcmode) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_METER);
  header.b1 = metermode;
  header.b2 = alcmode;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_screen(int s, int hstack, int width) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SCREEN);
  header.b1 = hstack;
  header.s1 = to_short(width);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_pan(int s, const RECEIVER *rx) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_PAN);
  header.b1 = rx->id;
  header.s1 = to_short(rx->pan);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_drive(int s, double value) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_DRIVE);
  command.dbl = to_double(value);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_micgain(int s, double gain) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_MICGAIN);
  command.dbl = to_double(gain);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_volume(int s, int id, double volume) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_VOLUME);
  command.header.b1 = id;
  command.dbl = to_double(volume);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_diversity(int s, int enabled, double gain, double phase) {
  DIVERSITY_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_DIVERSITY);
  command.diversity_enabled = enabled;
  command.div_gain = to_double(gain);
  command.div_phase =  to_double(phase);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_agc(int s, int id, int agc) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_AGC);
  header.b1 = id;
  header.b2 = agc;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_agc_gain(int s, const RECEIVER *rx) {
  AGC_GAIN_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_AGC_GAIN);
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
  command.header.data_type = to_short(CMD_RFGAIN);
  command.header.b1 = id;
  command.dbl = to_double(gain);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_attenuation(int s, int id, int attenuation) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_ATTENUATION);
  header.b1 = id;
  header.s1 = to_short(attenuation);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_squelch(int s, int id, int enable, double squelch) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_SQUELCH);
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
    command.header.data_type = to_short(CMD_RX_EQ);
    command.enable = rx->eq_enable;

    for (int i = 0; i < 11; i++) {
      command.freq[i] = to_double(rx->eq_freq[i]);
      command.gain[i] = to_double(rx->eq_gain[i]);
    }
  } else if (id == 8) {
    command.header.data_type = to_short(CMD_TX_EQ);

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
  command.header.data_type = to_short(CMD_NOISE);
  command.id                        = rx->id;
  command.nb                        = rx->nb;
  command.nr                        = rx->nr;
  command.anf                       = rx->anf;
  command.snb                       = rx->snb;
  command.nb2_mode                  = rx->nb2_mode;
  command.nr_agc                    = rx->nr_agc;
  command.nr2_gain_method           = rx->nr2_gain_method;
  command.nr2_npe_method            = rx->nr2_npe_method;
  command.nr2_ae                    = rx->nr2_ae;
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

void send_capture(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_CAPTURE);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_bandstack(int s, int old, int new) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_BANDSTACK);
  header.b1 = old;
  header.b2 = new;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_band(int s, int v, int band) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_BAND_SEL);
  header.b1 = v;
  header.b2 = band;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_twotone(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TWOTONE);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_tune(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TUNE);
  header.b1 = state;
  header.s1 = to_short(full_tune);
  header.s2 = to_short(memory_tune);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_vox(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_VOX);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_toggle_mox(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TOGGLE_MOX);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_toggle_tune(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TOGGLE_TUNE);
  header.s1 = to_short(full_tune);
  header.s2 = to_short(memory_tune);
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_mox(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_MOX);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_mode(int s, int v, int mode) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_MODE);
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
    header.data_type = to_short(CMD_FILTER_VAR);
    header.b1 = m;
    header.b2 = f;
    header.s1 = to_short(filters[m][f].low);
    header.s2 = to_short(filters[m][f].high);
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_rx_filter_cut(int s, int id) {
  //
  // This changes the filter cuts in the receiver
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RX_FILTER_CUT);
  header.b1 = id;

  if (id < receivers) {
    header.s1  =  to_short(receiver[id]->filter_low);
    header.s2  =  to_short(receiver[id]->filter_high);
  }

  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_tx_filter_cut(int s) {
  //
  // This changes the filter cuts in the transmitter
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TX_FILTER_CUT);

  if (can_transmit) {
    header.s1  =  to_short(transmitter->filter_low);
    header.s2  =  to_short(transmitter->filter_high);
  }

  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_filter_sel(int s, int v, int f) {
  //
  // Change filter of VFO v to filter f
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_FILTER_SEL);
  header.b1 = v;
  header.b2 = f;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_sidetone_freq(int s, int f) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SIDETONEFREQ);
  header.s1 = to_short(f);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_cwpeak(int s, int v, int p) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_CWPEAK);
  header.b1 = v;
  header.b2 = p;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_digidrivemax(int s) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_DIGIMAX);
  command.dbl = to_double(drive_digi_max);
  send_bytes(s, (char *)&command, sizeof(DOUBLE_COMMAND));
}

void send_am_carrier(int s) {
  if (can_transmit) {
    DOUBLE_COMMAND command;
    SYNC(command.header.sync);
    command.header.data_type = to_short(CMD_AMCARRIER);
    command.dbl = to_double(transmitter->am_carrier_level);
    send_bytes(s, (char *)&command, sizeof(DOUBLE_COMMAND));
  }
}

void send_dexp(int s) {
  if (can_transmit) {
    DEXP_DATA command;
    SYNC(command.header.sync);
    command.header.data_type = to_short(CMD_DEXP);
    command.dexp = transmitter->dexp;
    command.dexp_filter = transmitter->dexp_filter;
    command.dexp_trigger = to_short(transmitter->dexp_trigger);
    command.dexp_exp = to_short(transmitter->dexp_exp);
    command.dexp_filter_low = to_short(transmitter->dexp_filter_low);
    command.dexp_filter_high = to_short(transmitter->dexp_filter_high);
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
    command.header.data_type = to_short(CMD_COMPRESSOR);
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
    command.header.data_type = to_short(CMD_TXMENU);
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
    header.data_type = to_short(CMD_CTCSS);
    header.b1 = transmitter->ctcss_enabled;
    header.b2 = transmitter->ctcss;
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_txfilter(int s) {
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_short(CMD_TXFILTER);
    header.b1 = transmitter->use_rx_filter;
    header.s1 = to_short(tx_filter_low);
    header.s2 = to_short(tx_filter_high);
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_preemp(int s) {
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_short(CMD_PREEMP);
    header.b1 = transmitter->pre_emphasize;
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_psparams(int s, const TRANSMITTER *tx) {
  PS_PARAMS params;
  SYNC(params.header.sync);
  params.header.data_type = to_short(CMD_PSPARAMS);
  params.ps_ptol = tx->ps_ptol;
  params.ps_oneshot = tx->ps_oneshot;
  params.ps_map = tx->ps_map;
  params.ps_setpk = to_double(tx->ps_setpk);
  send_bytes(s, (char *)&params, sizeof(PS_PARAMS));
}

void send_psresume(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_PSRESUME);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_psreset(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_PSRESET);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_psonoff(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_PSONOFF);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_psatt(int s) {
  //
  // This sends TX attenuation, PS auto-att, feedback, and PS ant
  if (can_transmit) {
    HEADER header;
    SYNC(header.sync);
    header.data_type = to_short(CMD_PSATT);
    header.b1 = transmitter->auto_on;
    header.b2 = transmitter->feedback;
    header.s1 = to_short(transmitter->attenuation);
    header.s2 = to_short(receiver[PS_RX_FEEDBACK]->alex_antenna);
    send_bytes(s, (char *)&header, sizeof(HEADER));
  }
}

void send_afbinaural(int s, const RECEIVER *rx) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_BINAURAL);
  header.b1 = rx->id;
  header.b2 = rx->binaural;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_rx_fft(int s, const RECEIVER *rx) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_RXFFT);
  command.header.b1 = rx->id;
  command.header.b2 = rx->low_latency;
  command.u64 = to_ll(rx->fft_size);
  send_bytes(s, (char *)&command, sizeof(U64_COMMAND));
}

void send_tx_fft(int s, const TRANSMITTER *tx) {
  U64_COMMAND command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_TXFFT);
  command.header.b1 = tx->id;
  command.u64 = to_ll(tx->fft_size);
  send_bytes(s, (char *)&command, sizeof(U64_COMMAND));
}

void send_patrim(int s) {
  PATRIM_DATA command;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_PATRIM);
  command.pa_power = pa_power;

  for (int i = 0; i < 11; i++) {
    command.pa_trim[i] = to_double(pa_trim[i]);
  }

  send_bytes(s, (char *)&command, sizeof(PATRIM_DATA));
}

void send_deviation(int s, int v, int dev) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_DEVIATION);
  header.b1 = v;
  header.s1 = to_short(dev);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_atob(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_VFO_A_TO_B);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_btoa(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_VFO_B_TO_A);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_vfo_swap(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_VFO_SWAP);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_heartbeat(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_HEARTBEAT);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_soapy_rxant(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SOAPY_RXANT);
  header.b1 = adc[0].antenna;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_soapy_txant(int s) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SOAPY_TXANT);
  header.b1 = dac.antenna;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_soapy_agc(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SOAPY_AGC);
  header.b1 = id;
  header.b2 = adc[id].agc;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_split(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SPLIT);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_cw(int s, int state, int wait) {
  //
  // Send this in one header although wait may exceed a short
  //
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_CW);
  header.b1  = state;
  header.s1 = to_short(wait >> 12);
  header.s2 = to_short(wait & 0xFFF);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_sat(int s, int sat) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_SAT);
  header.b1  = sat;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_duplex(int s, int state) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_DUP);
  header.b1 = state;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_display(int s, int id) {
  DOUBLE_COMMAND command;
  SYNC(command.header.sync);
  command.header.b1 = id;

  if (id < RECEIVERS) {
    command.header.data_type = to_short(CMD_RX_DISPLAY);
    command.header.b2 = receiver[id]->display_detector_mode;
    command.header.s1 = to_short(receiver[id]->display_average_mode);
    command.dbl = to_double(receiver[id]->display_average_time);
  } else if (can_transmit) {
    command.header.data_type = to_short(CMD_TX_DISPLAY);
    command.header.b2 = transmitter->display_detector_mode;
    command.header.s1 = to_short(transmitter->display_average_mode);
    command.dbl = to_double(transmitter->display_average_time);
  }

  send_bytes(s, (char *)&command, sizeof(DOUBLE_COMMAND));
}

void send_rxfps(int s, int id, int fps) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RX_FPS);
  header.b1 = id;
  header.b2 = fps;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_txfps(int s, int fps) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_TX_FPS);
  header.b2 = fps;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_lock(int s, int lock) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_LOCK);
  header.b1 = lock;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_ctun(int s, int vfo, int ctun) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_CTUN);
  header.b1 = vfo;
  header.b2 = ctun;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_rx_select(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RX_SELECT);
  header.b1 = id;
  send_bytes(s, (char *)&header, sizeof(header));
}

void send_rit(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RIT);
  header.b1 = id;
  header.b2 = vfo[id].rit_enabled;
  header.s1 = to_short(vfo[id].rit);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_xit(int s, int id) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_XIT);
  header.b1 = id;
  header.b2 = vfo[id].xit_enabled;
  header.s1 = to_short(vfo[id].xit);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_sample_rate(int s, int id, int sample_rate) {
  U64_COMMAND command;
  long long rate = (long long)sample_rate;
  SYNC(command.header.sync);
  command.header.data_type = to_short(CMD_SAMPLE_RATE);
  command.header.b1 = id;
  command.u64 = to_ll(rate);
  send_bytes(s, (char *)&command, sizeof(command));
}

void send_receivers(int s, int receivers) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RECEIVERS);
  header.b1 = receivers;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_rit_step(int s, int v, int step) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_RIT_STEP);
  header.b1 = v;
  header.s1 = to_short(step);
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_filter_board(int s, int filter_board) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_FILTER_BOARD);
  header.b1 = filter_board;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_adc(int s, int id, int adc) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_ADC);
  header.b1 = id;
  header.b2 = adc;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_anan10E(int s, int new) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_ANAN10E);
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
  header.data_type = to_short(CMD_REGION);
  header.b1 = region;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

void send_mute_rx(int s, int id, int mute) {
  HEADER header;
  SYNC(header.sync);
  header.data_type = to_short(CMD_MUTE_RX);
  header.b1 = id;
  header.b2 = mute;
  send_bytes(s, (char *)&header, sizeof(HEADER));
}

static void *listen_thread(void *arg) {
  struct sockaddr_in address;
  struct timeval timeout;
  int on = 1;
  t_print("%s: listening on port %d\n", __FUNCTION__, listen_port);

  while (server_running) {
    if (listen_socket >= 0) { close(listen_socket); }

    // create TCP socket to listen on
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_socket < 0) {
      t_print("%s: socket() failed\n", __FUNCTION__);
      return NULL;
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
      return NULL;
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
      t_print("%s: accept() failed\n");
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
    t_print("%s: client_connected from %s\n", __FUNCTION__, s);
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
      // Setting this has to be post-poned until HERE, since now
      // the RX thread starts to send audio data. If we were TXing
      // when the client successfully connects, go RX.
      //
      g_idle_add(ext_set_mox, GINT_TO_POINTER(0));
      remoteclient.running = TRUE;

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
      g_idle_add(ext_set_mox, GINT_TO_POINTER(0));

      //
      // Stop sending periodic data
      //
      if (remoteclient.timer_id != 0) {
        g_source_remove(remoteclient.timer_id);
        remoteclient.timer_id = 0;
      }
    }

    if (remoteclient.socket != -1) {
      close(remoteclient.socket);
      remoteclient.socket = -1;
    }
  }

  return NULL;
}

int create_hpsdr_server() {
  t_print("create_hpsdr_server\n");
  g_mutex_init(&client_mutex);
  server_running = TRUE;
  listen_thread_id = g_thread_new( "HPSDR_listen", listen_thread, NULL);
  return 0;
}

int destroy_hpsdr_server() {
  t_print("destroy_hpsdr_server\n");
  server_running = FALSE;
  return 0;
}

// CLIENT Code

//
// Not all VFO frequency updates generate a packet to be sent by the client.
// Instead, frequency updates are "collected" and sent out  (if necessary)
// every 100 milli seconds.
// We use this periodic job to send a heart beat every 15 sec.
//
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

static char server_host[128];

void start_vfo_timer() {
  g_mutex_init(&accumulated_mutex);
  check_vfo_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 100, check_vfo, NULL, NULL);
  t_print("check_vfo_timer_id %d\n", check_vfo_timer_id);
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
      t_print("client_thread: ReadErr for HEADER\n");
      return NULL;
    }

    if (memcmp(header.sync, syncbytes, sizeof(syncbytes))  != 0) {
      t_print("header.sync mismatch: %02x %02x %02x %02x\n",
              header.sync[0],
              header.sync[1],
              header.sync[2],
              header.sync[3]);
      int syncs = 0;
      uint8_t c;

      while (syncs != sizeof(syncbytes) && client_running) {
        bytes_read = recv_bytes(client_socket, (char *)&c, 1);

        if (bytes_read <= 0) {
          t_print("%: ReadErr for HEADER RESYNC\n", __FUNCTION__);
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

      t_print("Re-SYNC was successful!\n");
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

      adc0_overload = data.adc0_overload;
      adc1_overload = data.adc1_overload;
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
          set_drive(0.0);
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
        transmitter->ps_getpk = from_double(data.ps_getpk);
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
        t_print("WARNING: band data received for b=%d, too large.\n", data.band);
        break;
      }

      BAND *band = band_get_band(data.band);

      if (data.current > band->bandstack->entries) {
        t_print("WARNING: band stack too large for b=%d, s=%d.\n", data.band, data.current);
        break;
      }

      snprintf(band->title, sizeof(band->title), "%s", data.title);
      band->OCrx = data.OCrx;
      band->OCtx = data.OCtx;
      band->alexRxAntenna = data.alexRxAntenna;
      band->alexTxAntenna = data.alexTxAntenna;
      band->alexAttenuation = data.alexAttenuation;
      band->disablePA = data.disablePA;
      band->bandstack->current_entry = data.current;
      band->gain = from_short(data.gain);
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
        t_print("WARNING: band data received for b=%d, too large.\n", data.band);
        break;
      }

      BAND *band = band_get_band(data.band);

      if (data.stack > band->bandstack->entries) {
        t_print("WARNING: band stack too large for b=%d, s=%d.\n", data.band, data.stack);
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
      adc0_filter_bypass = data.adc0_filter_bypass;
      adc1_filter_bypass = data.adc1_filter_bypass;
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
      radio->info.soapy.rx_antennas = data.soapy_rx_antennas;
      radio->info.soapy.tx_antennas = data.soapy_tx_antennas;
      radio->info.soapy.rx_gains = data.soapy_rx_gains;
      radio->info.soapy.tx_gains = data.soapy_tx_gains;
      radio->info.soapy.tx_channels = data.soapy_tx_channels;
      radio->info.soapy.rx_has_automatic_gain = data.soapy_rx_has_automatic_gain;
      //
      memcpy(radio->info.soapy.hardware_key, data.soapy_hardware_key, 64);
      memcpy(radio->info.soapy.driver_key, data.soapy_driver_key, 64);

      for (int i = 0; i < radio->info.soapy.rx_antennas; i++) {
        memcpy(radio->info.soapy.rx_antenna[i], data.soapy_rx_antenna[i], 64);
      }

      for (int i = 0; i < radio->info.soapy.tx_antennas; i++) {
        memcpy(radio->info.soapy.tx_antenna[i], data.soapy_tx_antenna[i], 64);
      }

      for (int i = 0; i < radio->info.soapy.rx_gains; i++) {
        memcpy(radio->info.soapy.rx_gain_elem_name[i], data.soapy_rx_gain_elem_name[i], 64);
      }

      for (int i = 0; i < radio->info.soapy.tx_gains; i++) {
        memcpy(radio->info.soapy.tx_gain_elem_name[i], data.soapy_tx_gain_elem_name[i], 64);
      }

      //
      pa_power = from_short(data.pa_power);
      OCfull_tune_time = from_short(data.OCfull_tune_time);
      OCmemory_tune_time = from_short(data.OCmemory_tune_time);
      cw_keyer_sidetone_frequency = from_short(data.cw_keyer_sidetone_frequency);
      rx_gain_calibration = from_short(data.rx_gain_calibration);
      device = radio->device = from_short(data.device);
      tx_filter_low = from_short(data.tx_filter_low);
      tx_filter_high = from_short(data.tx_filter_high);
      display_width = from_short(data.display_width);
      //
      drive_min = from_double(data.drive_min);
      drive_max = from_double(data.drive_max);
      drive_digi_max = from_double(data.drive_digi_max);
      div_gain = from_double(data.div_gain);
      div_phase = from_double(data.div_phase);

      for (int i = 0; i < 11; i++) {
        pa_trim[i] = from_double(data.pa_trim[i]);
      }

      radio->info.soapy.rx_gain_step = from_double(data.soapy_rx_gain_step);
      radio->info.soapy.rx_gain_min  = from_double(data.soapy_rx_gain_min );
      radio->info.soapy.rx_gain_max  = from_double(data.soapy_rx_gain_max );
      radio->info.soapy.tx_gain_step = from_double(data.soapy_tx_gain_step);
      radio->info.soapy.tx_gain_min  = from_double(data.soapy_tx_gain_min );
      radio->info.soapy.tx_gain_max  = from_double(data.soapy_tx_gain_max );

      for (int i = 0; i < radio->info.soapy.rx_gains; i++) {
        radio->info.soapy.rx_gain_elem_step[i] = from_double(data.soapy_rx_gain_elem_step[i]);
        radio->info.soapy.rx_gain_elem_min [i] = from_double(data.soapy_rx_gain_elem_min [i]);
        radio->info.soapy.rx_gain_elem_max [i] = from_double(data.soapy_rx_gain_elem_max [i]);
      }

      for (int i = 0; i < radio->info.soapy.tx_gains; i++) {
        radio->info.soapy.tx_gain_elem_step[i] = from_double(data.soapy_tx_gain_elem_step[i]);
        radio->info.soapy.tx_gain_elem_min [i] = from_double(data.soapy_tx_gain_elem_min [i]);
        radio->info.soapy.tx_gain_elem_max [i] = from_double(data.soapy_tx_gain_elem_max [i]);
      }

      //
      frequency_calibration = from_ll(data.frequency_calibration);
      soapy_radio_sample_rate = from_ll(data.soapy_radio_sample_rate);
      radio->frequency_min = from_ll(data.radio_frequency_min);
      radio->frequency_max = from_ll(data.radio_frequency_max);

      if (protocol == SOAPYSDR_PROTOCOL) {
        radio->info.soapy.sample_rate = soapy_radio_sample_rate;
      }

      g_idle_add(ext_att_type_changed, NULL);
      snprintf(title, sizeof(title), "piHPSDR: %s remote at %s", radio->name, server);
      g_idle_add(ext_set_title, (void *)title);
    }
    break;

    case INFO_DAC: {
      DAC_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(DAC_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      dac.antenna = data.antenna;
      dac.gain = from_double(data.gain);
    }
    break;

    case INFO_ADC: {
      ADC_DATA data;

      if (recv_bytes(client_socket, (char *)&data + sizeof(HEADER), sizeof(ADC_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      int i = data.adc;
      adc[i].antenna = from_short(data.antenna);
      adc[i].attenuation = from_short(data.attenuation);
      adc[i].gain = from_double(data.gain);
      adc[i].min_gain = from_double(data.min_gain);
      adc[i].max_gain = from_double(data.max_gain);
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
      rx->dither                = data.dither;
      rx->random                = data.random;
      rx->preamp                = data.preamp;
      rx->alex_antenna          = data.alex_antenna;
      rx->alex_attenuation      = data.alex_attenuation;
      rx->squelch_enable        = data.squelch_enable;
      rx->binaural              = data.binaural;
      rx->eq_enable             = data.eq_enable;
      rx->smetermode            = data.smetermode;
      rx->low_latency           = data.low_latency;
      //
      rx->fps                   = from_short(data.fps);
      rx->filter_low            = from_short(data.filter_low);
      rx->filter_high           = from_short(data.filter_high);
      rx->deviation             = from_short(data.deviation);
      rx->pan                   = from_short(data.pan);
      rx->width                 = from_short(data.width);
      //
      rx->hz_per_pixel          = from_double(data.hz_per_pixel);
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
      transmitter->alex_antenna              = data.alex_antenna;
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
        rx->zoom = spectrum_data.zoom;
        rx->pan  = from_short(spectrum_data.pan);
        rx->meter = from_double(spectrum_data.meter);
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
          g_idle_add(ext_rx_remote_update_display, rx);
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
          for (int i = 0; i < tx->width; i++) {
            tx->pixel_samples[i] = (float)((int)spectrum_data.sample[i] - 200);
          }

          g_idle_add(ext_tx_remote_update_display, tx);
        }
      }
    }
    break;

    case INFO_RXAUDIO: {
      RXAUDIO_DATA adata;

      if (recv_bytes(client_socket, (char *)&adata + sizeof(HEADER), sizeof(RXAUDIO_DATA) - sizeof(HEADER)) < 0) { return NULL; }

      RECEIVER *rx = receiver[adata.rx];
      int numsamples = from_short(adata.numsamples);

      //
      // Note CAPTURing is only done on the server side
      //
      for (int i = 0; i < numsamples; i++) {
        short left_sample = from_short(adata.samples[(i * 2)]);
        short right_sample = from_short(adata.samples[(i * 2) + 1]);

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
      t_print("CMD_SAMPLE_RATE: id=%d rate=%lld\n", id, rate);

      if (protocol == NEW_PROTOCOL) {
        receiver[id]->sample_rate = (int)rate;
        receiver[id]->hz_per_pixel = (double)receiver[id]->sample_rate / (double)receiver[id]->pixels;
      } else {
        soapy_radio_sample_rate = (int)rate;

        for (id = 0; id < receivers; id++) {
          receiver[id]->sample_rate = (int)rate;
          receiver[id]->hz_per_pixel = (double)receiver[id]->sample_rate / (double)receiver[id]->pixels;
        }
      }
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
      t_print("CMD_RECEIVERS: receivers=%d\n", r);
      g_idle_add(ext_radio_remote_change_receivers, GINT_TO_POINTER(r));
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
      int zoom = header.b2;
      t_print("CMD_ZOOM: zoom=%d rx[%d]->zoom=%d\n", zoom, id, receiver[id]->zoom);

      if (receiver[id]->zoom != zoom) {
        g_idle_add(ext_remote_set_zoom, GINT_TO_POINTER(zoom));
      } else {
        receiver[id]->zoom = (int)(zoom + 0.5);
        rx_update_zoom(receiver[id]);
      }
    }
    break;

    case CMD_PAN: {
      int pan = from_short(header.s1);
      g_idle_add(ext_remote_set_pan, GINT_TO_POINTER(pan));
    }
    break;

    case CMD_VOLUME: {
      DOUBLE_COMMAND cmd;

      if (recv_bytes(client_socket, (char *)&cmd + sizeof(HEADER), sizeof(DOUBLE_COMMAND) - sizeof(HEADER)) < 0) { return NULL; }

      int id = cmd.header.b1;
      double volume = from_double(cmd.dbl);
      t_print("CMD_VOLUME: volume=%f rx[%d]->volume=%f\n", volume, id, receiver[id]->volume);
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
      adc[receiver[id]->adc].attenuation = from_short(header.s1);
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
      g_idle_add(ext_radio_remote_set_mox, GINT_TO_POINTER(header.b1));
    }
    break;

    case CMD_VOX: {
      g_idle_add(ext_radio_remote_set_vox, GINT_TO_POINTER(header.b1));
    }
    break;

    case CMD_TUNE: {
      g_idle_add(ext_radio_remote_set_tune, GINT_TO_POINTER(header.b1));
    }
    break;

    case CMD_TWOTONE: {
      radio_remote_set_twotone(header.b1);
      g_idle_add(ext_radio_remote_set_mox, GINT_TO_POINTER(header.b1));
    }
    break;

    default:
      t_print("client_thread: Unknown type=%d\n", from_short(header.data_type));
      break;
    }
  }

  return NULL;
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

  if (client_socket == -1) {
    t_print("radio_connect_remote: socket creation failed...\n");
    return -1;
  }

  setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(client_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  struct hostent *server = gethostbyname(host);

  if (server == NULL) {
    t_print("radio_connect_remote: no such host: %s\n", host);
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

  t_print("radio_connect_remote: socket %d bound to %s:%d\n", client_socket, host, port);
  unsigned char s[2 * SHA512_DIGEST_LENGTH];
  unsigned char sha[SHA512_DIGEST_LENGTH];

  if (recv_bytes(client_socket, (char *)s, 4) < 0) {
    t_print("Could not receive Version number\n");
    close(client_socket);
    return -1;
  }

  if (((CLIENT_SERVER_VERSION >> 24) & 0xFF) != s[0] ||
      ((CLIENT_SERVER_VERSION >> 16) & 0xFF) != s[1] ||
      ((CLIENT_SERVER_VERSION >>  8) & 0xFF) != s[2] ||
      ((CLIENT_SERVER_VERSION      ) & 0xFF) != s[3]) {
    t_print("Wrong Client/Server version number\n");
    close(client_socket);
    return -4;
  }

  if (recv_bytes(client_socket, (char *)s, SHA512_DIGEST_LENGTH) < 0) {
    t_print("Could not receive Challenge\n");
    close(client_socket);
    return -1;
  }

  generate_pwd_hash(s, sha, pwd);
  send_bytes(client_socket, (char *)sha, SHA512_DIGEST_LENGTH);

  if (recv_bytes(client_socket, (char *)s, 1) < 0) {
    t_print("Could not receive Pwd Receipt\n");
    close(client_socket);
    return -1;
  }

  if (*s != 0x7F) {
    t_print("Server did not accept password\n");
    close(client_socket);
    return -5;
  }

  snprintf(server_host, sizeof(server_host), "%s:%d", host, port);
  client_thread_id = g_thread_new("remote_client", client_thread, &server_host);
  return 0;
}

//
// Execute a remote command through the GTK idle queue
// and (possibly) send a response.
// Because of the response required, we cannot just
// delegate to actions.c
//
//
static int remote_command(void *data) {
  HEADER *header = (HEADER *)data;
  int type = from_short(header->data_type);

  switch (type) {
  case INFO_BAND: {
    //
    // Band data is sent from the XVTR, ANT, and PA menu on the client side
    //
    const BAND_DATA *band_data = (BAND_DATA *)data;

    if (band_data->band > BANDS + XVTRS) {
      t_print("WARNING: band data received for b=%d, too large.\n", band_data->band);
      break;
    }

    BAND *band = band_get_band(band_data->band);

    if (band_data->current > band->bandstack->entries) {
      t_print("WARNING: band stack too large for b=%d, s=%d.\n", band_data->band, band_data->current);
      break;
    }

    snprintf(band->title, sizeof(band->title), "%s", band_data->title);
    band->OCrx = band_data->OCrx;
    band->OCtx = band_data->OCtx;
    band->alexRxAntenna = band_data->alexRxAntenna;
    band->alexTxAntenna = band_data->alexTxAntenna;
    band->alexAttenuation = band_data->alexAttenuation;
    band->disablePA = band_data->disablePA;
    band->bandstack->current_entry = band_data->current;
    band->gain = from_short(band_data->gain);
    band->pa_calibration = from_double(band_data->pa_calibration);
    band->frequencyMin = from_ll(band_data->frequencyMin);
    band->frequencyMax = from_ll(band_data->frequencyMax);
    band->frequencyLO  = from_ll(band_data->frequencyLO);
    band->errorLO  = from_ll(band_data->errorLO);
    //
    // make some changes effective
    //
    radio_set_alex_antennas();
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
      t_print("WARNING: band data received for b=%d, too large.\n", bandstack_data->band);
      break;
    }

    BAND *band = band_get_band(bandstack_data->band);

    if (bandstack_data->stack > band->bandstack->entries) {
      t_print("WARNING: band stack too large for b=%d, s=%d.\n", bandstack_data->band, bandstack_data->stack);
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
    //
    // Sending ADC data from the client to the server has a very limited scope:
    // - antenna (for SoapySDR)
    //
    const ADC_DATA *command = (ADC_DATA *)data;
    int id = command->adc;
    adc[id].antenna = from_short(command->antenna);
#ifdef SOAPYSDR

    if (id == 0 && device == SOAPYSDR_USB_DEVICE) {
      soapy_protocol_set_rx_antenna(receiver[0], adc[0].antenna);
    }

#endif
  }
  break;

  case INFO_DAC: {
    //
    // Sending DAC data from the client to the server has a very limited scope:
    // - antenna (for SoapySDR)
    //
    const DAC_DATA *command = (DAC_DATA *)data;
    dac.antenna = from_short(command->antenna);
#ifdef SOAPYSDR

    if (device == SOAPYSDR_USB_DEVICE && can_transmit) {
      soapy_protocol_set_tx_antenna(transmitter, dac.antenna);
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
    vfo_move(hz, command->header.b2);
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
    vfo_move_to(hz);
    send_vfo_data(remoteclient.socket, VFO_A);  // need both in case of SAT/RSAT
    send_vfo_data(remoteclient.socket, VFO_B);  // need both in case of SAT/RSAT

    if (pan != active_receiver->pan) {
      send_pan(remoteclient.socket, active_receiver);
    }
  }
  break;

  case CMD_ZOOM: {
    int id = header->b1;
    set_zoom(id, (double)header->b2);
    send_zoom(remoteclient.socket, active_receiver);
    send_pan(remoteclient.socket, active_receiver);
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
    display_width = from_short(header->s1);
    radio_reconfigure_screen();
  }
  break;

  case CMD_PAN: {
    int id = header->b1;
    set_pan(id, (double)from_short(header->s1));
    send_pan(remoteclient.socket, receiver[id]);
  }
  break;

  case CMD_VOLUME: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    set_af_gain(command->header.b1, from_double(command->dbl));
  }
  break;

  case CMD_MICGAIN: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    set_mic_gain(from_double(command->dbl));
  }
  break;

  case CMD_DRIVE: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    set_drive(from_double(command->dbl));
  }
  break;

  case CMD_AGC: {
    int r = header->b1;

    if (r < receivers) {
      RECEIVER *rx = receiver[r];
      rx->agc = header->b2;
      rx_set_agc(rx);
      send_agc(remoteclient.socket, rx->id, rx->agc);
      g_idle_add(ext_vfo_update, NULL);
    }
  }
  break;

  case CMD_TOGGLE_TUNE: {
    full_tune = from_short(header->s1);
    memory_tune = from_short(header->s2);
    radio_toggle_tune();
    g_idle_add(ext_vfo_update, NULL);
    send_tune(remoteclient.socket, tune);
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
    send_vox(remoteclient.socket, mox);
  }
  break;

  case CMD_TUNE: {
    full_tune = from_short(header->s1);
    memory_tune = from_short(header->s2);
    radio_set_tune(header->b1);
    g_idle_add(ext_vfo_update, NULL);
    send_tune(remoteclient.socket, tune);
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
    int r = agc_gain_command->id;

    if (r < receivers) {
      RECEIVER *rx = receiver[r];
      rx->agc_hang_threshold = from_double(agc_gain_command->hang_thresh);
      set_agc_gain(r, from_double(agc_gain_command->gain));
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
    set_rf_gain(command->header.b1, from_double(command->dbl));
  }
  break;

  case CMD_ATTENUATION: {
    int att = from_short(header->s1);
    set_attenuation_value((double)att);
    send_attenuation(remoteclient.socket, active_receiver->id, att);
  }
  break;

  case CMD_SQUELCH: {
    const DOUBLE_COMMAND *command = (DOUBLE_COMMAND *)data;
    int r = command->header.b1;

    if (r < receivers) {
      receiver[r]->squelch_enable = command->header.b2;
      receiver[r]->squelch = from_double(command->dbl);
      set_squelch(receiver[r]);
    }
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
      adc[0].antenna = header->b1;
#ifdef SOAPYSDR
      soapy_protocol_set_rx_antenna(receiver[0], adc[0].antenna);
#endif
    }
  }
  break;

  case CMD_SOAPY_TXANT: {
    if (device == SOAPYSDR_USB_DEVICE && can_transmit) {
      dac.antenna = header->b1;
#ifdef SOAPYSDR
      soapy_protocol_set_tx_antenna(transmitter, dac.antenna);
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
      soapy_protocol_set_automatic_gain(active_receiver, agc);

      if (!agc) { soapy_protocol_set_gain(active_receiver); }

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
    duplex = header->b1;
    setDuplex();
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
    rx_set_offset(active_receiver, vfo[v].offset);
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
    filter_board = header->b1;
    load_filters();
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
    const RXMENU_DATA *command = (RXMENU_DATA *)data;
    int id = command->id;
    receiver[id]->dither = command->dither;
    receiver[id]->random = command->random;
    receiver[id]->preamp = command->preamp;
    adc0_filter_bypass = command->adc0_filter_bypass;
    adc1_filter_bypass = command->adc1_filter_bypass;
    schedule_receive_specific();
    schedule_high_priority();
  }
  break;

  case CMD_DIVERSITY: {
    const DIVERSITY_COMMAND *command = (DIVERSITY_COMMAND *)data;
    int save = suppress_popup_sliders;
    suppress_popup_sliders = 1;
    set_diversity(command->diversity_enabled);
    set_diversity_gain(from_double(command->div_gain));
    set_diversity_phase(from_double(command->div_phase));
    suppress_popup_sliders = save;
  }
  break;

  case CMD_TXFILTER: {
    if (can_transmit) {
      transmitter->use_rx_filter = header->b1;
      tx_filter_low = from_short(header->s1);
      tx_filter_high = from_short(header->s2);
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
      set_drive(drive_digi_max);
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
      receiver[PS_RX_FEEDBACK]->alex_antenna = from_short(header->s2);
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
