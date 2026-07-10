/* Copyright (C)
* 2024 - Christoph van Wüllen, DL1YCF
* 2024,2025, 2026 - Heiko Amft, DL1BZ (heavily extended for project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
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
#include <gdk/gdk.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdalign.h>

#ifdef __APPLE__
  #include <time.h>
#endif

#include <libwebsockets.h>

#include "radio.h"
#include "vfo.h"
#include "rigctl.h"
#include "ext.h"
#include "message.h"
#include "main.h"
#include "discovery.h"
#include "tci_audio.h"
#include "audio.h"
#include "band.h"
#include "filter.h"
#include "agc.h"
#include "sliders.h"

#define MAXDATASIZE     1024
#define MAXMSGSIZE      512
#define TCI_MAX_ARGS 16
#define TCI_BINARY_REASSEMBLY_MAX 65536
#define TCI_MAX_CLIENTS 8

#ifndef LWS_PROTOCOL_LIST_TERM
  #define LWS_PROTOCOL_LIST_TERM { NULL, NULL, 0, 0, 0, NULL, 0 }
#endif

int tci_enable = 0;
int tci_port   = 40001;

int tci_audio_rx_active = 0;
int tci_audio_tx_active = 0;
int tci_transmitter_owned = 0;
//
// OpCodes for WebSocket frames
//
enum OpCode {
  opCONT  = 0,
  opTEXT  = 1,
  opBIN   = 2,
  opCLOSE = 8,
  opPING  = 9,
  opPONG  = 10
};

static GThread *tci_server_thread_id = NULL;
static int tci_running = 0;
static guint tci_tx_chrono_timer_id = 0;

static struct lws_context *tci_lws_context = NULL;
static int tci_lws_pending_writable = 0;
static char tci_cw_msg_pending_callsign[MAXMSGSIZE];
static char tci_cw_msg_active_callsign[MAXMSGSIZE];
static char tci_cw_msg_active_suffix[MAXMSGSIZE];
static int tci_cw_msg_active = 0;
static int tci_cw_msg_call_pos = 0;
static int tci_cw_msg_call_repeat = 1;
static int tci_cw_msg_call_repeat_index = 0;
static int tci_cw_msg_suffix_pending = 0;
static int tci_cw_macros_delay_ms = 10;

typedef struct _client {
  int seq;                      // Seq. number of the client in tciclients[] array
  int fd;                       // socket
  int running;                  // set this to zero to close client connection
  int tx_owner;                 // indicates whether this client is entitled to do TX
  guint tci_timer;              // GTK id  of the periodic task
  guint tci_clearmox_timer;     // Timer for "delayed" TX->RX transition
  long long last_fa;            // last VFO-A  freq reported
  long long last_fb;            // last VFO-B  freq reported
  long long last_fx;            // last TX     freq reported
  int last_ma;                  // last VFO-A  mode reported
  int last_mb;                  // last VFO-B  mode reported
  int last_split;               // last split state reported
  int last_mox;                 // last mox   state reported
  int count;                    // ping counter
  int rxsensor;                 // enable transmit of S meter data
  int txsensor;                 // enable transmit of drive data
  int idle_queued;              // counter
  int last_trx;                 // last trx command rvcd. from the client
  struct lws *wsi;              // libwebsockets connection
  GQueue *lws_tx_queue;         // queued PAYLOAD objects for LWS writable callback
  int initial_sent;             // initial state already sent via LWS
  int rx_audio_enabled[TCI_RX_AUDIO_MAX_RECEIVERS];
  unsigned int rx_audio_read_count[TCI_RX_AUDIO_MAX_RECEIVERS];
  int tx_audio_enabled;
  unsigned int tx_audio_rx_count;
  unsigned char *binary_rx_buf;
  size_t binary_rx_len;
  size_t binary_rx_size;
} CLIENT;

static CLIENT tciclient[TCI_MAX_CLIENTS];

typedef struct _payload {
  CLIENT *client;
  int     type;
  char    msg[MAXMSGSIZE];
  unsigned char *bin;
  size_t  len;
} PAYLOAD;

static gpointer tci_lws_server (gpointer data);
static void tci_lws_free_queue (CLIENT *client);
static void tci_update_audio_global (void);
static void tci_audio_wakeup (void);
static void tci_handle_binary_lws (CLIENT *client, const unsigned char* data, size_t len, struct lws *wsi);
static void tci_handle_binary (CLIENT *client, const TCI_STREAM *stream, size_t len);

typedef struct {
  char *cmd;
  int argc;
  char *argv[TCI_MAX_ARGS];
} TCI_CMD;

typedef void (*TCI_HANDLER) (CLIENT *client, const TCI_CMD *cmd);

typedef struct {
  const char *name;
  int min_args;
  int max_args;   // -1 = unlimited
  TCI_HANDLER handler;
} TCI_DISPATCH;

static const TCI_DISPATCH tci_dispatch[];

static void tci_send_smeter (CLIENT *client, int v);
static void tci_send_rx_filter_band (CLIENT *client, int v);
static void tci_send_text (CLIENT *client, const char* msg);
static int tci_queue_frame (CLIENT *client, int type, const char* msg, int check_running);

static int tci_bool (const char* s) {
  return s != NULL && (*s == '1' || !g_ascii_strcasecmp (s, "true"));
}

static int tci_int (const char* s, int def) {
  return s != NULL ? atoi (s) : def;
}

static long long tci_ll (const char* s, long long def) {
  return s != NULL ? atoll (s) : def;
}

static double tci_double (const char* s, double def) {
  return s != NULL ? atof (s) : def;
}

static int tci_clamp_int(int value, int min, int max) {
  if (value < min) { return min; }
  if (value > max) { return max; }
  return value;
}

static double tci_clamp_double(double value, double min, double max) {
  if (value < min) { return min; }
  if (value > max) { return max; }
  return value;
}

//
// Launch TCI system. Called upon program start if TCI is
// enabled in the props file, and from the CAT/TCI menu
// if TCI is enabled there.
//
void launch_tci (void) {
  t_print ("---- LAUNCHING TCI LWS SERVER ----\n");
  memset(tciclient, 0, sizeof(tciclient));
  tci_audio_set_wakeup_callback (tci_audio_wakeup);
  tci_running = 1;
  tci_server_thread_id = g_thread_new ("tci lws server", tci_lws_server, GINT_TO_POINTER (tci_port));
}

static int tci_has_clients(void) {
  int ret = 0;
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) { ret = 1; }
  }
  return ret;
}

//
// Shut down TCI system. Called from CAT/TCI menu
// if TCI is disabled there.
//
void shutdown_tci (void) {
  t_print ("%s\n", __func__);
  if (tci_tx_chrono_timer_id != 0) {
    g_source_remove (tci_tx_chrono_timer_id);
    tci_tx_chrono_timer_id = 0;
  }
  tci_audio_set_wakeup_callback (NULL);
  if (tci_lws_context != NULL) {
    for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
      if (tciclient[c].wsi != NULL) {
        (void) tci_queue_frame(tciclient + c, opTEXT, "stop;", 0);
        lws_set_timeout(tciclient[c].wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
      }
    }
    lws_cancel_service (tci_lws_context);
    for (int i = 0; i < 50 && tci_has_clients(); i++) {
      lws_cancel_service (tci_lws_context);
      g_usleep(10000);
    }
  }
  tci_running = 0;
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
  if (tci_server_thread_id != NULL) {
    if (g_thread_self() != tci_server_thread_id) {
      g_thread_join (tci_server_thread_id);
    }
    tci_server_thread_id = NULL;
  }
}

static int tci_queue_frame (CLIENT *client, int type, const char* msg, int check_running) {
  PAYLOAD *resp;
  if (client == NULL) { return 0; }
  if (check_running && !client->running) { return 0; }
  resp = g_new (PAYLOAD, 1);
  resp->client = client;
  resp->type = type;
  resp->bin = NULL;
  resp->len = 0;
  if (msg != NULL) {
    g_strlcpy (resp->msg, msg, MAXMSGSIZE);
  } else {
    resp->msg[0] = 0;
  }
  if (type == opTEXT && client->idle_queued >= 100) {
    g_free (resp);
    return 0;
  }
  client->idle_queued++;
  if (client->wsi != NULL) {
    if (client->lws_tx_queue == NULL) {
      client->lws_tx_queue = g_queue_new();
    }
    g_queue_push_tail (client->lws_tx_queue, resp);
    tci_lws_pending_writable = 1;
    if (tci_lws_context != NULL) {
      lws_cancel_service (tci_lws_context);
    }
    return 1;
  }
  g_free (resp);
  if (client->idle_queued > 0) { client->idle_queued--; }
  return 0;
}

static int tci_queue_binary_frame (CLIENT *client, const unsigned char* data, size_t len) {
  PAYLOAD *resp;
  if (client == NULL || data == NULL || len == 0 || !client->running) { return 0; }
  resp = g_new (PAYLOAD, 1);
  resp->client = client;
  resp->type = opBIN;
  resp->msg[0] = 0;
  resp->bin = g_new(unsigned char, len);  // do not use g_memdup2() since this is too new
  memcpy(resp->bin, data, len);
  resp->len = len;
  if (client->idle_queued >= 100 || client->wsi == NULL) {
    g_free (resp->bin);
    g_free (resp);
    return 0;
  }
  if (client->lws_tx_queue == NULL) {
    client->lws_tx_queue = g_queue_new();
  }
  client->idle_queued++;
  g_queue_push_tail (client->lws_tx_queue, resp);
  tci_lws_pending_writable = 1;
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
  return 1;
}

static void tci_send_text (CLIENT *client, const char* msg) {
  if (rigctl_debug && client != NULL) { t_print ("TCI%d response: %s\n", client->seq, msg ? msg : "(null)"); }
  (void) tci_queue_frame (client, opTEXT, msg, 1);
}

static void tci_cw_msg_reset_state(void) {
  tci_cw_msg_pending_callsign[0] = 0;
  tci_cw_msg_active_callsign[0] = 0;
  tci_cw_msg_active_suffix[0] = 0;
  tci_cw_msg_active = 0;
  tci_cw_msg_call_pos = 0;
  tci_cw_msg_call_repeat = 1;
  tci_cw_msg_call_repeat_index = 0;
  tci_cw_msg_suffix_pending = 0;
}

static void tci_cw_send_to_all(const char *msg) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_text(tciclient + c, msg);
    }
  }
}

static int tci_cw_msg_queue_next(void) {
  int call_len;
  if (!tci_cw_msg_active) {
    return 0;
  }
  call_len = (int) strlen(tci_cw_msg_active_callsign);
  if (call_len > 0 && tci_cw_msg_call_repeat_index < tci_cw_msg_call_repeat) {
    if (tci_cw_msg_call_pos < call_len) {
      rigctl_queue_cw_char(tci_cw_msg_active_callsign[tci_cw_msg_call_pos]);
      tci_cw_msg_call_pos++;
      return 1;
    }
    tci_cw_msg_call_repeat_index++;
    if (tci_cw_msg_call_repeat_index < tci_cw_msg_call_repeat) {
      tci_cw_msg_call_pos = 0;
      rigctl_queue_cw_char(' ');
      return 1;
    }
  }
  if (tci_cw_msg_pending_callsign[0] != 0) {
    char callsign_msg[MAXMSGSIZE];
    snprintf(callsign_msg, sizeof(callsign_msg), "callsign_send:%s;", tci_cw_msg_pending_callsign);
    tci_cw_send_to_all(callsign_msg);
    tci_cw_msg_pending_callsign[0] = 0;
  }
  if (tci_cw_msg_suffix_pending) {
    tci_cw_msg_suffix_pending = 0;
    if (tci_cw_msg_active_suffix[0] != 0) {
      char suffix_text[MAXMSGSIZE];
      snprintf(suffix_text, sizeof(suffix_text), " %s", tci_cw_msg_active_suffix);
      rigctl_queue_cw_string(suffix_text);
      return 1;
    }
  }
  tci_cw_msg_reset_state();
  return 0;
}

static void tci_update_audio_global (void) {
  int enrx = 0;
  int entx = 0;
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
        if (tciclient[c].rx_audio_enabled[i]) { enrx = 1; break; }
      }
      if (tciclient[c].tx_audio_enabled) { entx = 1; }
    }
    if (enrx && entx) { break; }
  }
  tci_audio_rx_active = enrx;
  tci_audio_tx_active = entx;
}

static void tci_audio_wakeup (void) {
  tci_lws_pending_writable = 1;
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
}

static void tci_queue_rx_audio_frame (CLIENT *client, int receiver_id) {
  TCI_STREAM stream;
  size_t frame_len;
  if (client == NULL || !client->running || !client->rx_audio_enabled[receiver_id]) { return; }
  if (tci_audio_get_frame (receiver_id, &client->rx_audio_read_count[receiver_id], &stream, sizeof (stream),
                           &frame_len) == 0) {
    return;
  }
  (void) tci_queue_binary_frame (client, (const unsigned char *)&stream, frame_len);
}


static int tci_queue_tx_chrono_frame (CLIENT *client) {
  TCI_STREAM_HEADER header;  // only the header will be sent
  int queued;
  if (client == NULL || !client->running || !client->tx_audio_enabled) { return 0; }
  memset (&header, 0, sizeof (header));
  header.receiver = 0;
  header.sample_rate = TCI_AUDIO_SAMPLE_RATE;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = TCI_TX_AUDIO_CHRONO_LENGTH;
  header.type = TCI_STREAM_TX_CHRONO;
  header.channels = TCI_AUDIO_CHANNELS;
  queued = tci_queue_binary_frame (client, (const unsigned char*) &header, sizeof(TCI_STREAM_HEADER));
  if (queued) {
  } else if (rigctl_debug) {
    t_print ("TCI%d TX chrono queue FAILED enabled=%d running=%d\n",
             client->seq,
             client->tx_audio_enabled,
             client->running);
  }
  return queued;
}

static void tci_handle_binary (CLIENT *client, const TCI_STREAM *stream, size_t len) {
  if (client == NULL || stream == NULL || len < sizeof(TCI_STREAM_HEADER)) { return; }
  switch (stream->header.type) {
  case TCI_STREAM_TX_AUDIO:
    // simply drop if audio no longer enabled
    if (client->tx_audio_enabled) {
      client->tx_audio_rx_count++;
      tci_audio_handle_tx_frame (stream, len);
    }
    break;
  default:
    if (rigctl_debug) {
      t_print ("TCI%d binary ignored: type=%u len=%zu\n", client->seq, stream->header.type, len);
    }
    break;
  }
}

//
// Binary data my come in chunks. It may be a single chunk, it may be multiple.
// Take care to collect all chunks before calling tci_handle_binary.
//
static void tci_handle_binary_lws (CLIENT *client, const unsigned char* data, size_t len, struct lws *wsi) {
  size_t remaining;
  int final;
  size_t needed;
  if (client == NULL || data == NULL || wsi == NULL || len == 0) { return; }
  //
  // My observation is that the binary chunk NEVER comes in one part.
  // Therefore, we have to "put together" the individual parts in all
  // cases which means that the binary frame, as put together on
  // client->binary_rx_buf, is always aligned for all data types.
  //
  // Note wsjtx sends 8256 = 64 + 8192 byte frames (in several chunks) although
  // although only 64 + 4096 bytes are needed for the audio data, thus
  // wasting half of the bandwidth.
  //
  if (lws_is_first_fragment(wsi)) {
    //
    // If this is the first chunk of a multi-chunk message,
    // reset client->binary_rx_len. This we do to avoid
    // modifying binary_rx_len *outside* this function
    // since this may lead to race conditions.
    //
    client->binary_rx_len = 0;
  }
  final = lws_is_final_fragment (wsi);
  remaining = lws_remaining_packet_payload (wsi);
  needed = client->binary_rx_len + len;
  if (needed > TCI_BINARY_REASSEMBLY_MAX) {
    if (rigctl_debug) {
      t_print ("TCI%d binary fragment overflow: accumulated=%zu incoming=%zu max=%zu\n",
               client->seq,
               client->binary_rx_len,
               len,
               (size_t) TCI_BINARY_REASSEMBLY_MAX);
    }
    client->binary_rx_len = 0;
    return;
  }
  //
  // Start with a chunk of 8256 bytes (8192 bytes audio data)
  // and increase in chunks of 8192 bytes, if necessary
  // Note the max size of a frame is 65536 bytes.
  //
  if (needed > client->binary_rx_size) {
    size_t new_size = client->binary_rx_size ? client->binary_rx_size : 8256;
    while (new_size < needed) {
      new_size += 8192;
    }
    client->binary_rx_buf = g_realloc (client->binary_rx_buf, new_size);
    client->binary_rx_size = new_size;
  }
  memcpy (client->binary_rx_buf + client->binary_rx_len, data, len);
  client->binary_rx_len = needed;
  if (!final || remaining != 0) {
    return;
  }
  //
  // Now the whole frame is assembled, so we can process it
  //
  tci_handle_binary (client, (TCI_STREAM *)client->binary_rx_buf, client->binary_rx_len);
  client->binary_rx_len = 0;
}

static void tci_service_rx_audio (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
        if (tciclient[c].rx_audio_enabled[i]) {
          tci_queue_rx_audio_frame (tciclient + c, i);
        }
      }
    }
  }
}

//
// To keep things  simple, tci_send_dds does not report
// the center frequency but the "real" RX frequency
//
static void tci_send_dds (CLIENT *client, int v) {
  long long f;
  char msg[MAXMSGSIZE];
  if (v < 0 || v > 1) { return; }
  f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;
  snprintf (msg, MAXMSGSIZE, "dds:%d,%lld;", v, f);
  tci_send_text (client, msg);
}

static void tci_send_mox (CLIENT *client) {
  if (radio_is_transmitting()) {
    tci_send_text (client, "trx:0,true;");
    client->last_mox = 1;
  } else {
    tci_send_text (client, "trx:0,false;");
    client->last_mox = 0;
  }
}

static void tci_send_mox_state (CLIENT *client, int state) {
  if (client == NULL) { return; }
  if (client->last_mox == state) { return; }
  if (state) {
    tci_send_text (client, "trx:0,true;");
    client->last_mox = 1;
  } else {
    tci_send_text (client, "trx:0,false;");
    client->last_mox = 0;
  }
}

static void tci_broadcast_mox_state (int state) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_mox_state (tciclient + c, state);
    }
  }
}

void tci_mox_changed (int state) {
  if (!tci_running) { return; }
  tci_broadcast_mox_state (state);
}

static void tci_broadcast_tune_state (int state) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      if (state) {
        tci_send_text (tciclient + c, "tune:0,true;");
      } else {
        tci_send_text (tciclient + c, "tune:0,false;");
      }
    }
  }
}

void tci_tune_changed (int state) {
  if (!tci_running) { return; }
  tci_broadcast_tune_state (state);
}

static void tci_send_lock (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "lock:%d,%s;", receiver_id, locked ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_vfo_lock (CLIENT *client, int receiver_id, int channel_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (channel_id < 0 || channel_id > 1) { return; }
  snprintf (msg, MAXMSGSIZE, "vfo_lock:%d,%d,%s;", receiver_id, channel_id, locked ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_vfo_locks (CLIENT *client, int receiver_id) {
  tci_send_vfo_lock (client, receiver_id, 0);
  tci_send_vfo_lock (client, receiver_id, 1);
}

static void tci_broadcast_lock (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_lock (tciclient + c, VFO_A);
      tci_send_vfo_locks (tciclient + c, VFO_A);
    }
  }
}

void tci_lock_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_lock();
}

//
// There are four (!) frequencies to report, namely for RX0/1 channel0/1.
// RX=0 channel=0: reports VFO-A frequency, all other combination report VFO-B
//
// Thus logbook programs correctly display both frequencies no matter whether
// they  use RX0/channel0:RX0/channel1 or RX0/channel0:RX1/channel0
//
static void tci_send_vfo (CLIENT *client, int v, int c) {
  long long f;
  char msg[MAXMSGSIZE];
  if (v < 0 || v > 1) { return; }
  if (v >= receivers) { return; }
  if (c < 0 || c > 1) { return; }
  if (v  == VFO_A && c == 0) {
    f = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    client->last_fa = f;
  } else {
    f = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
    client->last_fb = f;
  }
  snprintf (msg, MAXMSGSIZE, "vfo:%d,%d,%lld;", v, c, f);
  tci_send_text (client, msg);
}

static void tci_broadcast_vfo (int v, int chan) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_vfo (tciclient + c, v, chan);
    }
  }
}

static void tci_set_vfo (CLIENT *client, int VfoNr, int Ch, long long SetFreq) {
  if (VfoNr < 0 || VfoNr > 1) { return; }
  if (VfoNr >= receivers) { return; }
  if (Ch < 0 || Ch > 1) { return; }
  if (VfoNr == VFO_A && Ch == 0) {
    vfo_id_set_frequency (VFO_A, SetFreq);
    client->last_fa = SetFreq;
    g_idle_add (ext_vfo_update, NULL);
  } else {
    vfo_id_set_frequency (VFO_B, SetFreq);
    client->last_fb = SetFreq;
    g_idle_add (ext_vfo_update, NULL);
  }
  tci_broadcast_vfo (VfoNr, Ch);
}

static void tci_send_limits (CLIENT *client) {
  char msg[MAXMSGSIZE];
  if (client == NULL || receiver[0] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "vfo_limits:%lld,%lld;",
            (long long) radio->frequency_min,
            (long long) radio->frequency_max);
  tci_send_text (client, msg);
  snprintf (msg, MAXMSGSIZE, "if_limits:%lld,%lld;",
            - (long long) (receiver[0]->sample_rate / 2),
            (long long) (receiver[0]->sample_rate / 2));
  tci_send_text (client, msg);
}

static void tci_send_drive (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int tx_drive;
  tx_drive = (int) (radio_get_drive() + 0.5);
  snprintf (msg, MAXMSGSIZE, "drive:0,%d;", tx_drive);
  tci_send_text (client, msg);
}

static void tci_format_tenth (char *dst, size_t len, double value) {
  int tenths;
  int sign;
  if (dst == NULL || len == 0) {
    return;
  }
  tenths = (int) ((value * 10.0) + (value >= 0.0 ? 0.5 : -0.5));
  sign = tenths < 0;
  if (sign) {
    tenths = -tenths;
  }
  snprintf (dst, len, "%s%d.%d", sign ? "-" : "", tenths / 10, tenths % 10);
}

static void tci_send_tx_sensors (CLIENT *client) {
  char msg[MAXMSGSIZE];
  char mic_s[32];
  char rms_s[32];
  char peak_s[32];
  char swr_s[32];
  double mic;
  double rms;
  double peak;
  double swr;
  if (client == NULL || transmitter == NULL || !can_transmit) {
    return;
  }
  if (!radio_is_transmitting() || transmitter->fwd <= 0.01) {
    return;
  }
  mic = transmitter->micpeak;
  rms = transmitter->fwd;
  peak = transmitter->fwd;
  swr = transmitter->swr;
  if (mic < -300.0) {
    mic = 0.0;
  }
  tci_format_tenth (mic_s, sizeof (mic_s), mic);
  tci_format_tenth (rms_s, sizeof (rms_s), rms);
  tci_format_tenth (peak_s, sizeof (peak_s), peak);
  tci_format_tenth (swr_s, sizeof (swr_s), swr);
  snprintf (msg, MAXMSGSIZE, "tx_sensors:0,%s,%s,%s,%s;", mic_s, rms_s, peak_s, swr_s);
  tci_send_text (client, msg);
}

static int tci_get_tune_drive_as_int (void) {
  int value;
  if (transmitter == NULL) {
    return 0;
  }
  value = transmitter->tune_use_drive ? radio_get_drive() : transmitter->tune_drive;
  if (value < 0) {
    value = 0;
  }
  if (value > 100) {
    value = 100;
  }
  return value;
}

static void tci_send_rit_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rit_enable:%d,%s;",
            receiver_id, vfo[receiver_id].rit_enabled ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rit_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rit_enable (tciclient + c, receiver_id);
    }
  }
}

static void tci_send_rit_offset_value (CLIENT *client, int receiver_id, long long value) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rit_offset:%d,%lld;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_send_rit_offset (CLIENT *client, int receiver_id) {
  long long value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  value = vfo[receiver_id].rit_enabled ? vfo[receiver_id].rit : 0;
  tci_send_rit_offset_value (client, receiver_id, value);
}

static void tci_broadcast_rit_offset (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rit_offset (tciclient + c, receiver_id);
    }
  }
}

void tci_rit_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  tci_broadcast_rit_enable (receiver_id);
  if (vfo[receiver_id].rit_enabled) {
    if (vfo[receiver_id].rit != 0) {
      tci_broadcast_rit_offset (receiver_id);
    }
  } else {
    tci_broadcast_rit_offset (receiver_id);
  }
}

static void tci_send_xit_enable (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int txvfo;
  if (client == NULL) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  snprintf (msg, MAXMSGSIZE, "xit_enable:0,%s;",
            vfo[txvfo].xit_enabled ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_xit_enable (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_xit_enable (tciclient + c);
    }
  }
}

static void tci_send_xit_offset_value (CLIENT *client, long long value) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "xit_offset:0,%lld;", value);
  tci_send_text (client, msg);
}

static void tci_send_xit_offset (CLIENT *client) {
  int txvfo;
  long long value;
  if (client == NULL) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  value = vfo[txvfo].xit_enabled ? vfo[txvfo].xit : 0;
  tci_send_xit_offset_value (client, value);
}

static void tci_broadcast_xit_offset (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_xit_offset (tciclient + c);
    }
  }
}

void tci_xit_enable_changed (void) {
  int txvfo;
  if (!tci_running) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  tci_broadcast_xit_enable();
  if (vfo[txvfo].xit_enabled) {
    if (vfo[txvfo].xit != 0) {
      tci_broadcast_xit_offset();
    }
  } else {
    tci_broadcast_xit_offset();
  }
}

void tci_rit_offset_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (!vfo[receiver_id].rit_enabled) { return; }
  tci_broadcast_rit_offset (receiver_id);
}

void tci_xit_offset_changed (void) {
  int txvfo;
  if (!tci_running) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  if (!vfo[txvfo].xit_enabled) { return; }
  tci_broadcast_xit_offset();
}

static void tci_send_tune_drive (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "tune_drive:0,%d;", tci_get_tune_drive_as_int());
  tci_send_text (client, msg);
}

static void tci_send_rx_filter_band (CLIENT *client, int v) {
  char msg[MAXMSGSIZE];
  int mode;
  int filter_id;
  FILTER *filter;
  if (v < 0 || v >= receivers || receiver[v] == NULL) { return; }
  mode = vfo[v].mode;
  filter_id = vfo[v].filter;
  if (mode < 0 || mode >= MODES) { return; }
  if (filter_id < 0 || filter_id >= FILTERS) { return; }
  filter = &filters[mode][filter_id];
  snprintf (msg, MAXMSGSIZE, "rx_filter_band:%d,%d,%d;", v, filter->low, filter->high);
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_filter_band_value (int receiver_id, int low, int high) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "rx_filter_band:%d,%d,%d;", receiver_id, low, high);
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_text (tciclient + c, msg);
    }
  }
}

void tci_rx_filter_band_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_filter_band (tciclient + c, receiver_id);
    }
  }
}

static void tci_send_split (CLIENT *client) {
  //
  // send "true" if tx is on VFO-B frequency
  //
  if (vfo_get_tx_vfo() == VFO_A) {
    tci_send_text (client, "split_enable:0,false;");
    client->last_split = 0;
  } else {
    tci_send_text (client, "split_enable:0,true;");
    client->last_split = 1;
  }
}

static void tci_send_tx_enable (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "tx_enable:0,%s;",
            can_transmit ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_tune (CLIENT *client) {
  if (can_transmit && transmitter->tune) {
    tci_send_text (client, "tune:0,true;");
  } else {
    tci_send_text (client, "tune:0,false;");
  }
}

static void tci_send_mute (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int state = 0;
  if (active_receiver != NULL) {
    state = active_receiver->mute_radio ? 1 : 0;
  }
  snprintf (msg, MAXMSGSIZE, "mute:%s;", state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_mute_state (CLIENT *client, int state) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "mute:%s;", state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_mute_state (int state) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_mute_state (tciclient + c, state);
    }
  }
}

static void tci_send_rx_mute (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_mute:%d,%s;", receiver_id, receiver[receiver_id]->mute_radio ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_mute_state (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_mute:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_mute_state (int receiver_id, int state) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_mute_state (tciclient + c, receiver_id, state);
    }
  }
}


void tci_mute_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (active_receiver != NULL) {
    tci_broadcast_mute_state(active_receiver->mute_radio ? 1 : 0);
  }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  tci_broadcast_rx_mute_state(receiver_id, receiver[receiver_id]->mute_radio ? 1 : 0);
}

void tci_rx_mute_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  tci_broadcast_rx_mute_state(receiver_id, receiver[receiver_id]->mute_radio ? 1 : 0);
}

static double tci_sql_db_from_slider(double value) {
  if (value < 0.0) { value = 0.0; }
  if (value > 100.0) { value = 100.0; }
  return ((value / 100.0) * 140.0) - 140.0;
}

static void tci_send_sql_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "sql_enable:%d,%s;", receiver_id,
            receiver[receiver_id]->squelch_enable ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_sql_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "sql_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_sql_level (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  double value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_sql_db_from_slider(receiver[receiver_id]->squelch);
  snprintf (msg, MAXMSGSIZE, "sql_level:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_send_sql_level_value (CLIENT *client, int receiver_id, double value) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_double(value, -140.0, 0.0);
  snprintf (msg, MAXMSGSIZE, "sql_level:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_broadcast_sql_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_sql_enable (tciclient + c, receiver_id);
    }
  }
}

static void tci_broadcast_sql_level (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_sql_level (tciclient + c, receiver_id);
    }
  }
}

void tci_sql_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_sql_enable(receiver_id);
}

void tci_sql_level_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_sql_level(receiver_id);
}

static void tci_send_rx_anf_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  int state;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  state = receiver[receiver_id]->anf;
  snprintf (msg, MAXMSGSIZE, "rx_anf_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_anf_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_anf_enable (tciclient + c, receiver_id);
    }
  }
}

void tci_rx_anf_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_anf_enable(receiver_id);
}

static void tci_send_rx_nf_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nf_enable:%d,%s;", receiver_id, "true");
  tci_send_text (client, msg);
}

static void tci_send_rx_nf_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nf_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_nf_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_nf_enable (tciclient + c, receiver_id);
    }
  }
}

void tci_rx_nf_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_nf_enable(receiver_id);
}

static int tci_rx_nb_allowed (int receiver_id) {
  int mode;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return 0;
  }
  mode = vfo[receiver_id].mode;
  return mode != modeDIGL && mode != modeDIGU;
}

static int tci_rx_nb_effective_state (int receiver_id) {
  if (!tci_rx_nb_allowed(receiver_id)) {
    return 0;
  }
  return receiver[receiver_id]->snb ? 1 : 0;
}

static void tci_send_rx_nb_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nb_enable:%d,%s;", receiver_id,
            tci_rx_nb_effective_state(receiver_id) ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_nb_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_nb_enable (tciclient + c, receiver_id);
    }
  }
}

void tci_rx_nb_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_nb_enable(receiver_id);
}

static void tci_send_rx_bin_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  int state;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  state = receiver[receiver_id]->binaural;
  snprintf (msg, MAXMSGSIZE, "rx_bin_enable:%d,%s;", receiver_id,
            state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_bin_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_bin_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_bin_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_bin_enable (tciclient + c, receiver_id);
    }
  }
}

void tci_rx_bin_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_bin_enable(receiver_id);
}

static int tci_rx_apf_allowed (int receiver_id) {
  int mode;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return 0;
  }
  mode = vfo[receiver_id].mode;
  return mode == modeCWU || mode == modeCWL;
}

static int tci_rx_apf_effective_state (int receiver_id) {
  if (!tci_rx_apf_allowed(receiver_id)) {
    return 0;
  }
  return vfo[receiver_id].cwAudioPeakFilter ? 1 : 0;
}

static void tci_send_rx_apf_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_apf_enable:%d,%s;", receiver_id,
            tci_rx_apf_effective_state(receiver_id) ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_apf_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_apf_enable (tciclient + c, receiver_id);
    }
  }
}

void tci_rx_apf_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_apf_enable(receiver_id);
}

static int tci_rx_nr_default_for_mode (int mode) {
  switch (mode) {
  case modeUSB:
  case modeLSB:
  case modeCWU:
  case modeCWL:
    return 4;
  case modeAM:
  case modeSAM:
    return 3;
  default:
    return 0;
  }
}

static int tci_rx_nr_allowed (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return 0;
  }
  return tci_rx_nr_default_for_mode(vfo[receiver_id].mode) != 0;
}

static int tci_rx_nr_effective_state (int receiver_id) {
  if (!tci_rx_nr_allowed(receiver_id)) {
    return 0;
  }
  return receiver[receiver_id]->nr != 0;
}

static void tci_send_rx_nr_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nr_enable:%d,%s;", receiver_id,
            tci_rx_nr_effective_state(receiver_id) ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_nr_enable (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_nr_enable (tciclient + c, receiver_id);
    }
  }
}

void tci_rx_nr_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_nr_enable(receiver_id);
}

static void tci_send_volume (CLIENT *client) {
  char msg[MAXMSGSIZE];
  double value = 0.0;
  if (active_receiver != NULL) {
    value = tci_clamp_double(active_receiver->volume, -40.0, 0.0);
  }
  snprintf (msg, MAXMSGSIZE, "volume:%0.0f;", value);
  tci_send_text (client, msg);
}

static void tci_send_volume_value (CLIENT *client, double value) {
  char msg[MAXMSGSIZE];
  value = tci_clamp_double(value, -40.0, 0.0);
  snprintf (msg, MAXMSGSIZE, "volume:%0.0f;", value);
  tci_send_text (client, msg);
}

static void tci_send_rx_volume (CLIENT *client, int receiver_id, int channel) {
  char msg[MAXMSGSIZE];
  double value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  if (channel < 0 || channel > 1) { return; }
  value = tci_clamp_double(receiver[receiver_id]->volume, -40.0, 0.0);
  snprintf (msg, MAXMSGSIZE, "rx_volume:%d,%d,%0.0f;", receiver_id, channel, value);
  tci_send_text (client, msg);
}

static void tci_send_rx_volume_value (CLIENT *client, int receiver_id, int channel, double value) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  if (channel < 0 || channel > 1) { return; }
  value = tci_clamp_double(value, -40.0, 0.0);
  snprintf (msg, MAXMSGSIZE, "rx_volume:%d,%d,%0.0f;", receiver_id, channel, value);
  tci_send_text (client, msg);
}

static void tci_broadcast_volume (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_volume (tciclient + c);
    }
  }
}

static void tci_broadcast_rx_volume (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_rx_volume (tciclient + c, receiver_id, 0);
      tci_send_rx_volume (tciclient + c, receiver_id, 1);
    }
  }
}

void tci_volume_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_volume();
  tci_broadcast_rx_volume(receiver_id);
}


static void tci_send_agc_gain (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  double value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_double(receiver[receiver_id]->agc_gain, -20.0, 120.0);
  snprintf (msg, MAXMSGSIZE, "agc_gain:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_send_agc_gain_value (CLIENT *client, int receiver_id, double value) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_double(value, -20.0, 120.0);
  snprintf (msg, MAXMSGSIZE, "agc_gain:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_broadcast_agc_gain (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_agc_gain (tciclient + c, receiver_id);
    }
  }
}

static void tci_broadcast_agc_gain_value (int receiver_id, double value) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_double(value, -20.0, 120.0);
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_agc_gain_value(tciclient + c, receiver_id, value);
    }
  }
}

void tci_agc_gain_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_agc_gain(receiver_id);
}

static const char *tci_agc_mode_name(int agc) {
  switch (agc) {
  case AGC_OFF:
    return "off";
  case AGC_FAST:
    return "fast";
  default:
    return "normal";
  }
}

static int tci_parse_agc_mode(const char *mode) {
  if (mode == NULL) {
    return AGC_MEDIUM;
  }
  if (!g_ascii_strcasecmp(mode, "off")) {
    return AGC_OFF;
  }
  if (!g_ascii_strcasecmp(mode, "fast")) {
    return AGC_FAST;
  }
  if (!g_ascii_strcasecmp(mode, "normal")) {
    return AGC_MEDIUM;
  }
  return -1;
}

static void tci_send_agc_mode(CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf(msg, MAXMSGSIZE, "agc_mode:%d,%s;", receiver_id, tci_agc_mode_name(receiver[receiver_id]->agc));
  tci_send_text(client, msg);
}

static void tci_send_agc_mode_value(CLIENT *client, int receiver_id, int agc) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf(msg, MAXMSGSIZE, "agc_mode:%d,%s;", receiver_id, tci_agc_mode_name(agc));
  tci_send_text(client, msg);
}

static void tci_broadcast_agc_mode(int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_agc_mode(tciclient + c, receiver_id);
    }
  }
}

static void tci_broadcast_agc_mode_value(int receiver_id, int agc) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_agc_mode_value(tciclient + c, receiver_id, agc);
    }
  }
}

void tci_agc_mode_changed(int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_agc_mode(receiver_id);
}

static void tci_send_txfreq (CLIENT *client) {
  char msg[MAXMSGSIZE];
  long long f = vfo_get_tx_freq();
  snprintf (msg, MAXMSGSIZE, "tx_frequency:%lld;", f);
  tci_send_text (client, msg);
  client->last_fx = f;
}

static void tci_broadcast_txfreq (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_txfreq (tciclient + c);
    }
  }
}

static void tci_broadcast_drive (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_drive (tciclient + c);
    }
  }
}

static void tci_broadcast_tune_drive (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_tune_drive (tciclient + c);
    }
  }
}

static void tci_broadcast_split (void) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_split (tciclient + c);
    }
  }
}

void tci_split_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_split();
}

static const char *tci_mode_name (int m) {
  switch (m) {
  case modeLSB:
    return "LSB";
  case modeUSB:
    return "USB";
  case modeDSB:
    return "DSB";
  case modeCWL:
  case modeCWU:
    return "CW";
  case modeFMN:
    return "FM";
  case modeAM:
    return "AM";
  case modeDIGU:
    return "DIGU";
  case modeSPEC:
    return "SPEC";
  case modeDIGL:
    return "DIGL";
  case modeSAM:
    return "SAM";
  case modeDRM:
    return "DRM";
  default:
    return "USB";
  }
}

static void tci_send_mode_value (CLIENT *client, int v, int m) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (v < 0 || v > 1) { return; }
  if (v >= receivers || receiver[v] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "modulation:%d,%s;", v, tci_mode_name (m));
  tci_send_text (client, msg);
  if (v == 0) {
    client->last_ma = m;
  } else {
    client->last_mb = m;
  }
}

static void tci_send_mode (CLIENT *client, int v) {
  if (v < 0 || v > 1) { return; }
  tci_send_mode_value (client, v, vfo[v].mode);
}

static void tci_broadcast_mode_value (int v, int m) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    if (tciclient[c].running) {
      tci_send_mode_value (tciclient + c, v, m);
      tci_send_rx_filter_band (tciclient + c, v);
    }
  }
}

void tci_vfo_changed (int id) {
  if (!tci_running) { return; }
  if (id == VFO_A) {
    tci_broadcast_vfo (VFO_A, 0);
  } else if (id == VFO_B) {
    tci_broadcast_vfo (VFO_A, 1);
    if (receivers > 1) {
      tci_broadcast_vfo (VFO_B, 0);
      tci_broadcast_vfo (VFO_B, 1);
    }
  }
}

void tci_vfos_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_vfo (VFO_A, 0);
  tci_broadcast_vfo (VFO_A, 1);
  tci_broadcast_mode_value (VFO_A, vfo[VFO_A].mode);
  if (receivers > 1) {
    tci_broadcast_vfo (VFO_B, 0);
    tci_broadcast_vfo (VFO_B, 1);
    tci_broadcast_mode_value (VFO_B, vfo[VFO_B].mode);
  }
  tci_broadcast_txfreq();
  tci_broadcast_drive();
  tci_broadcast_split();
}

void tci_mode_changed (int id) {
  if (!tci_running) { return; }
  if (id < VFO_A || id > VFO_B) { return; }
  tci_broadcast_mode_value (id, vfo[id].mode);
}

void tci_tx_frequency_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_txfreq();
}

void tci_drive_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_drive();
}

static int tci_parse_mode (const char* mode_str) {
  if (mode_str == NULL) { return -1; }
  if (!g_ascii_strcasecmp (mode_str, "lsb"))  { return modeLSB; }
  if (!g_ascii_strcasecmp (mode_str, "usb"))  { return modeUSB; }
  if (!g_ascii_strcasecmp (mode_str, "dsb"))  { return modeDSB; }
  if (!g_ascii_strcasecmp (mode_str, "cw"))   { return modeCWU; }
  if (!g_ascii_strcasecmp (mode_str, "cwl"))  { return modeCWL; }
  if (!g_ascii_strcasecmp (mode_str, "cwu"))  { return modeCWU; }
  if (!g_ascii_strcasecmp (mode_str, "fmn"))  { return modeFMN; }
  if (!g_ascii_strcasecmp (mode_str, "fm"))   { return modeFMN; }
  if (!g_ascii_strcasecmp (mode_str, "am"))   { return modeAM; }
  if (!g_ascii_strcasecmp (mode_str, "digu")) { return modeDIGU; }
  if (!g_ascii_strcasecmp (mode_str, "spec")) { return modeSPEC; }
  if (!g_ascii_strcasecmp (mode_str, "digl")) { return modeDIGL; }
  if (!g_ascii_strcasecmp (mode_str, "sam"))  { return modeSAM; }
  if (!g_ascii_strcasecmp (mode_str, "drm"))  { return modeDRM; }
  return -1;
}

static void tci_set_mode (CLIENT *client, int VfoNr, const char* mode_str) {
  if (VfoNr < 0 || VfoNr > 1) { return; }
  t_print("%s: Vfo=%d\n", __func__, VfoNr);
  int m = tci_parse_mode (mode_str);
  t_print("%s: Mode=%d\n", __func__, m);
  if (m < 0) {
    t_print ("TCI%d unknown mode: %s\n", client->seq, mode_str);
    tci_send_mode (client, VfoNr);
    return;
  }
  vfo_id_mode_changed (VfoNr, m);
  t_print("%s: Mode changed\n", __func__);
  tci_broadcast_mode_value (VfoNr, m);
  t_print("%s: broadcast done\n", __func__);
}

static void tci_cmd_rit_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id;
  if (cmd->argc < 1) { return; }
  receiver_id = tci_int (cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (cmd->argc >= 2) {
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    vfo_id_rit_onoff(receiver_id, state);
  } else {
    tci_send_rit_enable (client, receiver_id);
  }
}

static void tci_cmd_xit_enable (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  if (cmd->argc < 1) { return; }
  trx = tci_int (cmd->argv[0], -1);
  if (trx != 0) { return; }
  if (cmd->argc >= 2) {
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    vfo_xit_onoff(state);
  } else {
    tci_send_xit_enable (client);
  }
}

static void tci_cmd_rit_offset (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id;
  if (cmd->argc < 1) { return; }
  receiver_id = tci_int (cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (cmd->argc >= 2) {
    long long value = tci_ll (cmd->argv[1], 0);
    tci_send_rit_offset_value (client, receiver_id, value);
    vfo_id_rit_value(receiver_id, value);
  } else {
    tci_send_rit_offset (client, receiver_id);
  }
}

static void tci_cmd_xit_offset (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  if (cmd->argc < 1) { return; }
  trx = tci_int (cmd->argv[0], -1);
  if (trx != 0) { return; }
  if (cmd->argc >= 2) {
    long long value = tci_ll (cmd->argv[1], 0);
    tci_send_xit_offset_value (client, value);
    vfo_xit_value(value);
  } else {
    tci_send_xit_offset (client);
  }
}

static void tci_cmd_digl_offset (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_text (client, "digl_offset:0;");
}

static void tci_cmd_digu_offset (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_text (client, "digu_offset:0;");
}

static int tci_is_bool_arg (const char *s) {
  return s != NULL && (!g_ascii_strcasecmp (s, "true") ||
                       !g_ascii_strcasecmp (s, "false"));
}

static void tci_set_lock_state (CLIENT *client, int receiver_id, int state, int vfo_lock, int channel_id) {
  int old = locked;
  locked = state;
  if (old != state) {
    tci_broadcast_lock();
  } else if (vfo_lock) {
    if (channel_id >= 0) {
      tci_send_vfo_lock (client, receiver_id, channel_id);
    } else {
      tci_send_vfo_locks (client, receiver_id);
    }
  } else {
    tci_send_lock (client, receiver_id);
  }
}

static void tci_cmd_lock_common (CLIENT *client, const TCI_CMD *cmd, int vfo_lock) {
  int receiver_id;
  int channel_id;
  if (cmd->argc < 1) {
    if (vfo_lock) {
      tci_send_vfo_locks (client, VFO_A);
    } else {
      tci_send_lock (client, VFO_A);
    }
    return;
  }
  receiver_id = tci_int (cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id > 1) { return; }
  if (receiver_id >= receivers) { return; }
  if (!vfo_lock) {
    if (cmd->argc >= 2) {
      tci_set_lock_state (client, receiver_id, tci_bool (cmd->argv[1]), 0, -1);
    } else {
      tci_send_lock (client, receiver_id);
    }
    return;
  }
  if (cmd->argc >= 3) {
    channel_id = tci_int (cmd->argv[1], -1);
    if (channel_id < 0 || channel_id > 1) { return; }
    tci_set_lock_state (client, receiver_id, tci_bool (cmd->argv[2]), 1, channel_id);
  } else if (cmd->argc == 2) {
    if (tci_is_bool_arg (cmd->argv[1])) {
      tci_set_lock_state (client, receiver_id, tci_bool (cmd->argv[1]), 1, -1);
    } else {
      channel_id = tci_int (cmd->argv[1], -1);
      if (channel_id < 0 || channel_id > 1) { return; }
      tci_send_vfo_lock (client, receiver_id, channel_id);
    }
  } else {
    tci_send_vfo_locks (client, receiver_id);
  }
}

static void tci_cmd_lock (CLIENT *client, const TCI_CMD *cmd) {
  tci_cmd_lock_common (client, cmd, 0);
}

static void tci_cmd_vfo_lock (CLIENT *client, const TCI_CMD *cmd) {
  tci_cmd_lock_common (client, cmd, 1);
}

static void tci_cmd_split_enable (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  if (cmd->argc < 1) {
    tci_send_split (client);
    return;
  }
  trx = tci_int (cmd->argv[0], -1);
  if (trx != 0) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    radio_set_split(state);
  } else {
    tci_send_split (client);
  }
}

static void tci_send_trx_count (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "trx_count:1;");
  tci_send_text (client, msg);
}

static void tci_send_macros_cwspeed (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "cw_macros_speed:%d;", cw_keyer_speed);
  tci_send_text (client, msg);
}

static void tci_send_keyer_cwspeed (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "cw_keyer_speed:%d;", cw_keyer_speed);
  tci_send_text (client, msg);
}

static void tci_send_cw_macros_delay(CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf(msg, MAXMSGSIZE, "cw_macros_delay:%d;", tci_cw_macros_delay_ms);
  tci_send_text(client, msg);
}


static int tci_parse_text (char* s, TCI_CMD *c) {
  int argc = 0;
  if (s == NULL || c == NULL) { return -1; }
  memset (c, 0, sizeof (*c));
  char *end = strchr (s, ';');
  if (end != NULL) { *end = 0; }
  c->cmd = s;
  char *p = strchr (s, ':');
  if (p == NULL) { return 0; }
  *p++ = 0;
  while (argc < TCI_MAX_ARGS) {
    c->argv[argc++] = p;
    p = strchr (p, ',');
    if (p == NULL) { break; }
    *p++ = 0;
  }
  c->argc = argc;
  return 0;
}

static void tci_cmd_trx_count (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_trx_count (client);
}

static int tci_radio_clear_mox(gpointer data) {
  CLIENT *client = (CLIENT *) data;
  client->tx_audio_enabled = 0;
  client->last_trx = 0;
  client->tci_clearmox_timer = 0;
  radio_set_mox(0);
  tci_send_mox(client);
  tci_update_audio_global();
  //
  // This is not thread-safe.
  // A binary frame may be "in the process" when tx_audio_enabled goes to zero,
  // and processing might not be complete until we reset the ring buffer.
  // So let's cross fingers and disable tx_audio_enabled as the first, and
  // reset the ring buffer as the last statement here (radio_set_mox() takes
  // about 20 msec, so probably no reason to worry).
  //
  tci_audio_tx_reset();
  return G_SOURCE_REMOVE;
}

static void tci_cmd_trx (CLIENT *client, const TCI_CMD *cmd) {
  int trx = 0;
  int source_tci;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
  }
  source_tci = (cmd->argc >= 3 && cmd->argv[2] != NULL && !g_ascii_strcasecmp (cmd->argv[2], "tci"));
  if (cmd->argc >= 2) {
    //
    // If no client "owns" the TX and this one wants to go TX, make it owner
    //
    int state = tci_bool (cmd->argv[1]);
    if (!tci_transmitter_owned && state) {
      tci_transmitter_owned = 1;
      client->tx_owner = 1;
    }
    //
    // Silently ignore TRX request if another client "owns" the TX,
    // but reply with a valid "trx" command
    //
    if (client->tx_owner) {
      if (state) {
        if (source_tci) {
          //
          // Clear a possibly pending TX->RX transition
          //
          if (client->tci_clearmox_timer != 0) {
            g_source_remove(client->tci_clearmox_timer);
            client->tci_clearmox_timer = 0;
          }
          tci_audio_tx_reset();
          client->tx_audio_enabled = 1;
          client->tx_audio_rx_count = 0;
          tci_send_text (client, "audio_samplerate:48000;");
          tci_send_text (client, "audio_stream_sample_type:float32;");
          tci_send_text (client, "audio_stream_channels:1;");
          tci_send_text (client, "audio_stream_samples:512;");
          tci_send_text (client, "tx_stream_audio_buffering:50;");
          tci_send_text (client, "audio_start:0;");
        }
        client->last_trx = 1;
        radio_set_mox(1);
        tci_send_mox (client);
        tci_update_audio_global();
      } else {
        int delay = client->tx_audio_enabled ? 50 : 0;
        // Use a fixed "PTT delay" for TCI audio,
        // 50 msec if TCI audio was active, no delay if no TCI audio
        client->tci_clearmox_timer = g_timeout_add(delay, tci_radio_clear_mox, client);
      }
    } else {
      // This is the response to a trx:0 command
      // for clients that do not "own" the TX
      tci_send_mox (client);
    }
  } else {
    // This is the response to a query (trx:0) command
    tci_send_mox (client);
  }
}

static void tci_cmd_tune (CLIENT *client, const TCI_CMD *cmd) {
  int trx = 0;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
  }
  if (cmd->argc >= 2) {
    //
    // If no client "owns" the TX and this one wants to go TX, make it owner
    //
    int state = tci_bool (cmd->argv[1]);
    if (!tci_transmitter_owned && state) {
      tci_transmitter_owned = 1;
      client->tx_owner = 1;
    }
    //
    // Silently ignore TRX request if another client "owns" the TX,
    // but reply with a valid "tune" and "trx" command
    //
    if (client->tx_owner) {
      int state = tci_bool (cmd->argv[1]);
      radio_set_tune(state);
      t_print ("TCI%d TUNE request=%d\n", client->seq, state);
    } else {
      tci_send_tune (client);
      tci_send_mox (client);
    }
  } else {
    tci_send_tune (client);
  }
}

static void tci_cmd_mute (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1) {
    int state = tci_bool (cmd->argv[0]) ? 1 : 0;
    active_receiver->mute_radio = state;
    tci_broadcast_mute_state (state);
  } else {
    tci_send_mute (client);
  }
}

static void tci_cmd_rx_mute (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    tci_broadcast_rx_mute_state (receiver_id, state);
  } else {
    tci_send_rx_mute (client, receiver_id);
  }
}

static void tci_cmd_rx_apf_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool(cmd->argv[1]) ? 1 : 0;
    vfo[receiver_id].cwAudioPeakFilter = state;
  } else {
    tci_send_rx_apf_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_nb_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    receiver[receiver_id]->nb = tci_bool(cmd->argv[1]) ? 1 : 0;
    rx_set_noise(receiver[receiver_id]);
  } else {
    tci_send_rx_nb_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_anf_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    receiver[receiver_id]->anf = tci_bool(cmd->argv[1]) ? 1 : 0;
    rx_set_noise(receiver[receiver_id]);
  } else {
    tci_send_rx_anf_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_nf_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool(cmd->argv[1]) ? 1 : 0;
    tci_send_rx_nf_enable_value(client, receiver_id, state);
  } else {
    tci_send_rx_nf_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_bin_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool(cmd->argv[1]) ? 1 : 0;
    receiver[receiver_id]->binaural = state;
    tci_send_rx_bin_enable_value(client, receiver_id, state);
  } else {
    tci_send_rx_bin_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_nr_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool(cmd->argv[1]) ? 4 : 0;
    RECEIVER *rx = receiver[receiver_id];
    rx->nr = state;
    rx_set_noise(rx);
  } else {
    tci_send_rx_nr_enable(client, receiver_id);
  }
}




static void tci_cmd_volume (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1) {
    double volume = tci_clamp_double(tci_double(cmd->argv[0], 0.0), -40.0, 0.0);
    if (active_receiver != NULL && active_receiver->id >= 0 && active_receiver->id < receivers && active_receiver->id < 2) {
      suppress_popup_sliders++;
      radio_set_af_gain(0, volume);
      suppress_popup_sliders--;
      tci_send_volume_value (client, volume);
    }
  } else {
    tci_send_volume (client);
  }
}

static void tci_cmd_rx_volume (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  int channel = tci_int (cmd->argv[1], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (channel < 0 || channel > 1) {
    return;
  }
  if (cmd->argc >= 3) {
    double volume = tci_clamp_double(tci_double(cmd->argv[2], 0.0), -40.0, 0.0);
    suppress_popup_sliders++;
    radio_set_af_gain(receiver_id, volume);
    suppress_popup_sliders--;
    tci_send_rx_volume_value (client, receiver_id, channel, volume);
  } else {
    tci_send_rx_volume (client, receiver_id, channel);
  }
}

static void tci_cmd_agc_gain (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    double value = tci_clamp_double(tci_double(cmd->argv[1], receiver[receiver_id]->agc_gain), -20.0, 120.0);
    suppress_popup_sliders++;
    radio_set_agc_gain(receiver_id, value);
    suppress_popup_sliders--;
    tci_broadcast_agc_gain_value (receiver_id, value);
  } else {
    tci_send_agc_gain (client, receiver_id);
  }
}

static void tci_cmd_agc_mode (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int agc = tci_parse_agc_mode(cmd->argv[1]);
    if (agc < 0 || agc >= AGC_LAST) {
      return;
    }
    receiver[receiver_id]->agc = agc;
    rx_set_agc(receiver[receiver_id]);
    g_idle_add(ext_vfo_update, NULL);
    tci_broadcast_agc_mode_value(receiver_id, agc);
  } else {
    tci_send_agc_mode(client, receiver_id);
  }
}

static void tci_cmd_sql_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    int state = tci_bool(cmd->argv[1]) ? 1 : 0;
    tci_send_sql_enable_value(client, receiver_id, state);
    ++suppress_popup_sliders;
    radio_set_squelch_enable(receiver_id, state);
    --suppress_popup_sliders;
  } else {
    tci_send_sql_enable(client, receiver_id);
  }
}

static void tci_cmd_sql_level (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    double level_db = tci_clamp_double(tci_double(cmd->argv[1], tci_sql_db_from_slider(receiver[receiver_id]->squelch)),
                                       -140.0, 0.0);
    tci_send_sql_level_value(client, receiver_id, level_db);
    ++suppress_popup_sliders;
    radio_set_squelch(receiver_id, level_db);
    --suppress_popup_sliders;
  } else {
    tci_send_sql_level(client, receiver_id);
  }
}

static void tci_cmd_rx_sensors_enable (CLIENT *client, const TCI_CMD *cmd) {
  client->rxsensor = tci_bool (cmd->argv[0]);
}

static void tci_cmd_tx_sensors_enable (CLIENT *client, const TCI_CMD *cmd) {
  client->txsensor = tci_bool (cmd->argv[0]);
}

static void tci_cmd_spot (CLIENT *client, const TCI_CMD *cmd) {
}

static void tci_cmd_spot_delete (CLIENT *client, const TCI_CMD *cmd) {
}

static void tci_cmd_spot_clear (CLIENT *client, const TCI_CMD *cmd) {
}


static void tci_cmd_iq_start (CLIENT *client, const TCI_CMD *cmd) {
}

static void tci_cmd_iq_stop (CLIENT *client, const TCI_CMD *cmd) {
}

static void tci_cmd_audio_start (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  client->rx_audio_enabled[receiver_id] = 1;
  client->rx_audio_read_count[receiver_id] = tci_audio_get_write_count (receiver_id);
  tci_update_audio_global();
  snprintf (msg, MAXMSGSIZE, "audio_start:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_send_audio_samplerate (CLIENT *client) {
  tci_send_text (client, "audio_samplerate:48000;");
}

static void tci_send_audio_stream_sample_type (CLIENT *client) {
  tci_send_text (client, "audio_stream_sample_type:3;");
}

static void tci_send_audio_stream_channels (CLIENT *client) {
  tci_send_text (client, "audio_stream_channels:2;");
}

static void tci_send_audio_stream_samples (CLIENT *client) {
  tci_send_text (client, "audio_stream_samples:512;");
}

static void tci_cmd_audio_samplerate (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_samplerate (client);
  tci_send_audio_stream_samples (client);
}

static void tci_cmd_iq_samplerate(CLIENT *client, const TCI_CMD *cmd) {
  int samplerate;
  char msg[MAXMSGSIZE];
  if (client == NULL || cmd->argc != 1 || cmd->argv[0] == NULL) {
    return;
  }
  samplerate = tci_int(cmd->argv[0], 0);
  if (samplerate != 48000 && samplerate != 96000 && samplerate != 192000 && samplerate != 384000) {
    return;
  }
  snprintf(msg, MAXMSGSIZE, "iq_samplerate:%d;", samplerate);
  tci_send_text(client, msg);
}

static void tci_cmd_mon_volume(CLIENT *client, const TCI_CMD *cmd) {
  if (client == NULL) {
    return;
  }
  tci_send_text(client, "mon_volume:-60;");
}

static void tci_cmd_mon_enable(CLIENT *client, const TCI_CMD *cmd) {
  if (client == NULL) {
    return;
  }
  tci_send_text(client, "mon_enable:false;");
}

static void tci_cmd_audio_stream_sample_type (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_stream_sample_type (client);
}

static void tci_cmd_audio_stream_channels (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_stream_channels (client);
}

static void tci_cmd_audio_stream_samples (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_stream_samples (client);
}

static void tci_cmd_audio_stop (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  client->rx_audio_enabled[receiver_id] = 0;
  tci_update_audio_global();
  snprintf (msg, MAXMSGSIZE, "audio_stop:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_cmd_modulation (CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int (cmd->argv[0], 0);
  if (cmd->argc >= 2) {
    tci_set_mode (client, VfoNr, cmd->argv[1]);
  } else {
    tci_send_mode (client, VfoNr);
  }
}

static void tci_cmd_vfo (CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int (cmd->argv[0], 0);
  int Ch = tci_int (cmd->argv[1], 0);
  if (cmd->argc >= 3) {
    tci_set_vfo (client, VfoNr, Ch, tci_ll (cmd->argv[2], 0));
  } else {
    tci_send_vfo (client, VfoNr, Ch);
  }
}

static void tci_cmd_rx_smeter (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_smeter (client, tci_int (cmd->argv[0], 0));
}

static void tci_cmd_drive (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  int value;
  int changed = 0;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
    if (cmd->argc >= 2) {
      value = tci_int (cmd->argv[1], 0);
      if (value < 0) {
        value = 0;
      }
      if (value > 100) {
        value = 100;
      }
      suppress_popup_sliders++;
      radio_set_drive (value);
      suppress_popup_sliders--;
      changed = 1;
    }
  }
  if (changed) {
    tci_broadcast_drive();
  } else {
    tci_send_drive (client);
  }
}

static void tci_cmd_tune_drive (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  int value;
  int changed = 0;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
    if (cmd->argc >= 2) {
      value = tci_int (cmd->argv[1], 0);
      if (value < 0) {
        value = 0;
      }
      if (value > 100) {
        value = 100;
      }
      if (transmitter != NULL) {
        transmitter->tune_drive = value;
        if (can_transmit && transmitter->tune_use_drive) {
          transmitter->tune_use_drive = 0;
        }
        changed = 1;
      }
    }
  }
  if (changed) {
    tci_broadcast_tune_drive();
  } else {
    tci_send_tune_drive (client);
  }
}

static void tci_cmd_rx_filter_band (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 3) {
    int low = tci_int (cmd->argv[1], 0);
    int high = tci_int (cmd->argv[2], 0);
    filter_edges_changed(receiver_id, low, high);
    tci_broadcast_rx_filter_band_value (receiver_id, low, high);
  } else {
    tci_send_rx_filter_band (client, receiver_id);
  }
}


static char *tci_cw_decode_text(const char *text) {
  GString *out;
  int speed_open = 0;
  if (text == NULL) {
    return NULL;
  }
  out = g_string_new(NULL);
  for (const char *p = text; *p != '\0'; p++) {
    switch (*p) {
    case '^':
      g_string_append_c(out, ':');
      break;
    case '~':
      g_string_append_c(out, ',');
      break;
    case '*':
      g_string_append_c(out, ';');
      break;
    case '|': {
      const char *end = strchr(p + 1, '|');
      if (end != NULL) {
        if (speed_open) {
          g_string_append_c(out, ']');
          speed_open = 0;
        }
        g_string_append(out, "[.");
        for (const char *q = p + 1; q < end; q++) {
          switch (*q) {
          case '^':
            g_string_append_c(out, ':');
            break;
          case '~':
            g_string_append_c(out, ',');
            break;
          case '*':
            g_string_append_c(out, ';');
            break;
          default:
            g_string_append_c(out, *q);
            break;
          }
        }
        g_string_append_c(out, ']');
        p = end;
      } else {
        g_string_append_c(out, *p);
      }
      break;
    }
    case '>':
      if (speed_open) {
        g_string_append_c(out, ']');
      }
      g_string_append(out, "[+");
      speed_open = 1;
      break;
    case '<':
      if (speed_open) {
        g_string_append_c(out, ']');
      }
      g_string_append(out, "[-");
      speed_open = 1;
      break;
    default:
      g_string_append_c(out, *p);
      break;
    }
  }
  if (speed_open) {
    g_string_append_c(out, ']');
  }
  return g_string_free(out, FALSE);
}

static void tci_cw_msg_append_part(GString *out, const char *part) {
  if (part == NULL || part[0] == 0 || strcmp(part, "_") == 0) {
    return;
  }
  if (out->len > 0) {
    g_string_append_c(out, ' ');
  }
  g_string_append(out, part);
}

static char *tci_cw_msg_extract_callsign(const char *src, int *repeat) {
  char *call;
  char *dollar;
  int r = 1;
  if (src == NULL) {
    if (repeat != NULL) { *repeat = 1; }
    return g_strdup("");
  }
  call = g_strdup(src);
  dollar = strrchr(call, '$');
  if (dollar != NULL) {
    r = atoi(dollar + 1);
    if (r < 1) { r = 1; }
    *dollar = 0;
  }
  if (repeat != NULL) { *repeat = r; }
  return call;
}

static void tci_cmd_cw_msg(CLIENT *client, const TCI_CMD *cmd) {
  char *prefix = NULL;
  char *callsign_arg = NULL;
  char *callsign = NULL;
  char *suffix = NULL;
  GString *prefix_text;
  int repeat = 1;
  int queued = 0;
  if (cmd->argc == 1 && cmd->argv[0] != NULL) {
    //
    // cw_msg:CALL;  just updates the call sign
    //
    if (!tci_cw_msg_active || tci_cw_msg_pending_callsign[0] == 0) {
      t_print("TCI%d cw_msg callsign correction ignored: no active cw_msg\n", client->seq);
      return;
    }
    callsign_arg = tci_cw_decode_text(cmd->argv[0]);
    if (callsign_arg == NULL) {
      return;
    }
    callsign = tci_cw_msg_extract_callsign(callsign_arg, NULL);
    if (callsign != NULL && callsign[0] != 0 && strcmp(callsign, "_") != 0) {
      int new_len = (int) strlen(callsign);
      g_strlcpy(tci_cw_msg_active_callsign, callsign, sizeof(tci_cw_msg_active_callsign));
      g_strlcpy(tci_cw_msg_pending_callsign, callsign, sizeof(tci_cw_msg_pending_callsign));
      if (tci_cw_msg_call_pos > new_len) {
        tci_cw_msg_call_pos = new_len;
      }
      t_print("TCI%d cw_msg callsign correction accepted callsign=%s pos=%d repeat=%d/%d\n", client->seq,
              tci_cw_msg_active_callsign, tci_cw_msg_call_pos, tci_cw_msg_call_repeat_index + 1,
              tci_cw_msg_call_repeat);
    }
    g_free(callsign_arg);
    g_free(callsign);
    return;
  }
  if (cmd->argc < 4 || cmd->argv[1] == NULL || cmd->argv[2] == NULL || cmd->argv[3] == NULL) {
    return;
  }
  prefix = tci_cw_decode_text(cmd->argv[1]);
  callsign_arg = tci_cw_decode_text(cmd->argv[2]);
  suffix = tci_cw_decode_text(cmd->argv[3]);
  if (prefix == NULL || callsign_arg == NULL || suffix == NULL) {
    g_free(prefix);
    g_free(callsign_arg);
    g_free(suffix);
    return;
  }
  callsign = tci_cw_msg_extract_callsign(callsign_arg, &repeat);
  tci_cw_msg_reset_state();
  g_strlcpy(tci_cw_msg_active_callsign, callsign, sizeof(tci_cw_msg_active_callsign));
  g_strlcpy(tci_cw_msg_pending_callsign, callsign, sizeof(tci_cw_msg_pending_callsign));
  if (suffix[0] != 0 && strcmp(suffix, "_") != 0) {
    g_strlcpy(tci_cw_msg_active_suffix, suffix, sizeof(tci_cw_msg_active_suffix));
    tci_cw_msg_suffix_pending = 1;
  }
  tci_cw_msg_call_repeat = repeat;
  tci_cw_msg_call_repeat_index = 0;
  tci_cw_msg_call_pos = 0;
  tci_cw_msg_active = 1;
  prefix_text = g_string_new(NULL);
  tci_cw_msg_append_part(prefix_text, prefix);
  if (prefix_text->len > 0) {
    g_string_append_c(prefix_text, ' ');
    rigctl_queue_cw_string(prefix_text->str);
  } else {
    queued = tci_cw_msg_queue_next();
  }
  t_print("TCI%d cw_msg queued=%d prefix=%s callsign=%s repeat=%d suffix=%s\n", client->seq, queued,
          prefix_text->str, callsign, repeat, tci_cw_msg_active_suffix[0] ? tci_cw_msg_active_suffix : "_");
  g_string_free(prefix_text, TRUE);
  g_free(prefix);
  g_free(callsign_arg);
  g_free(callsign);
  g_free(suffix);
}

static void tci_cmd_cw_macros_speed (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    cw_keyer_speed = tci_clamp_int(tci_int(cmd->argv[0], cw_keyer_speed), 1, 100);
  }
  tci_send_macros_cwspeed (client);
}

static void tci_cmd_cw_keyer_speed (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    cw_keyer_speed = tci_clamp_int(tci_int(cmd->argv[0], cw_keyer_speed), 1, 100);
  }
  tci_send_keyer_cwspeed (client);
}

static void tci_cmd_cw_macros_delay (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    tci_cw_macros_delay_ms = tci_clamp_int(tci_int(cmd->argv[0], tci_cw_macros_delay_ms), 0, 5000);
  }
  tci_send_cw_macros_delay(client);
}

static void tci_cmd_cw_macros_speed_up(CLIENT *client, const TCI_CMD *cmd) {
  int step = 1;
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    step = tci_int(cmd->argv[0], 1);
  }
  if (step < 0) { step = -step; }
  cw_keyer_speed = tci_clamp_int(cw_keyer_speed + step, 1, 100);
  tci_send_macros_cwspeed(client);
}

static void tci_cmd_cw_macros_speed_down(CLIENT *client, const TCI_CMD *cmd) {
  int step = 1;
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    step = tci_int(cmd->argv[0], 1);
  }
  if (step < 0) { step = -step; }
  cw_keyer_speed = tci_clamp_int(cw_keyer_speed - step, 1, 100);
  tci_send_macros_cwspeed(client);
}


static void tci_cmd_cw_macros (CLIENT *client, const TCI_CMD *cmd) {
  GString *raw;
  char *decoded;
  if (cmd->argc < 2 || cmd->argv[1] == NULL) {
    return;
  }
  raw = g_string_new(cmd->argv[1]);
  for (int i = 2; i < cmd->argc; i++) {
    g_string_append_c(raw, ',');
    if (cmd->argv[i] != NULL) {
      g_string_append(raw, cmd->argv[i]);
    }
  }
  decoded = tci_cw_decode_text(raw->str);
  g_string_free(raw, TRUE);
  if (decoded == NULL) {
    return;
  }
  rigctl_queue_cw_string(decoded);
  g_free(decoded);
}

static void tci_cmd_cw_macros_stop (CLIENT *client, const TCI_CMD *cmd) {
  rigctl_purge_cw();
  tci_cw_msg_reset_state();
  t_print("TCI%d cw_macros_stop\n", client->seq);
}

static void tci_cmd_cw_terminal (CLIENT *client, const TCI_CMD *cmd) {
  int enabled;
  if (cmd->argc < 1 || cmd->argv[0] == NULL) {
    return;
  }
  enabled = g_ascii_strcasecmp(cmd->argv[0], "true") == 0 || strcmp(cmd->argv[0], "1") == 0;
  if (enabled) {
    tci_send_text(client, "cw_terminal:true;");
  } else {
    tci_send_text(client, "cw_terminal:false;");
  }
}

static void tci_cmd_stop (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  client->rxsensor = 0;
  client->txsensor = 0;
  tci_send_text (client, "stop;");
  client->running = 0;
  if (client->tx_owner) {
    client->tx_owner = 0;
    tci_transmitter_owned = 0;
    if (client->tci_clearmox_timer != 0) {
      g_source_remove(client->tci_clearmox_timer);
      client->tci_clearmox_timer = 0;
    }
    if (client->last_trx) {
      //
      // If the last trx command from this client was "go TX", then
      // put piHPSDR into RX mode
      //
      if (client->tci_clearmox_timer != 0) {
        g_source_remove(client->tci_clearmox_timer);
        client->tci_clearmox_timer = 0;
      }
      radio_set_mox(0);
      client->last_trx = 0;
    }
  }
}

static const TCI_DISPATCH tci_dispatch[] = {
  { "trx_count",         0,  0, tci_cmd_trx_count },
  { "trx",               0, -1, tci_cmd_trx },
  { "split_enable",      1,  2, tci_cmd_split_enable },
  { "lock",              1,  2, tci_cmd_lock },
  { "vfo_lock",          1,  3, tci_cmd_vfo_lock },
  { "sql_enable",        1,  2, tci_cmd_sql_enable },
  { "sql_level",         1,  2, tci_cmd_sql_level },
  { "rx_anf_enable",     1,  2, tci_cmd_rx_anf_enable },
  { "rx_apf_enable",     1,  2, tci_cmd_rx_apf_enable },
  { "rx_nb_enable",      1,  2, tci_cmd_rx_nb_enable },
  { "rx_nf_enable",      1,  2, tci_cmd_rx_nf_enable },
  { "rx_bin_enable",     1,  2, tci_cmd_rx_bin_enable },
  { "rx_nr_enable",      1,  2, tci_cmd_rx_nr_enable },
  { "rit_enable",        1,  2, tci_cmd_rit_enable },
  { "xit_enable",        1,  2, tci_cmd_xit_enable },
  { "rit_offset",        1,  2, tci_cmd_rit_offset },
  { "xit_offset",        1,  2, tci_cmd_xit_offset },
  { "digl_offset",      0,  1, tci_cmd_digl_offset },
  { "digu_offset",      0,  1, tci_cmd_digu_offset },
  { "tune",              0, -1, tci_cmd_tune },
  { "mute",              0,  1, tci_cmd_mute },
  { "rx_mute",           1,  2, tci_cmd_rx_mute },
  { "volume",            0,  1, tci_cmd_volume },
  { "rx_volume",         2,  3, tci_cmd_rx_volume },
  { "agc_gain",          1,  2, tci_cmd_agc_gain },
  { "agc_mode",          1,  2, tci_cmd_agc_mode },
  { "iq_start",          0,  1, tci_cmd_iq_start },
  { "iq_stop",           0,  1, tci_cmd_iq_stop },
  { "iq_samplerate",     1,  1, tci_cmd_iq_samplerate },
  { "mon_volume",        0,  1, tci_cmd_mon_volume },
  { "mon_enable",        0,  1, tci_cmd_mon_enable },
  { "audio_samplerate",            0, -1, tci_cmd_audio_samplerate },
  { "audio_stream_sample_type",    0, -1, tci_cmd_audio_stream_sample_type },
  { "audio_stream_channels",       0, -1, tci_cmd_audio_stream_channels },
  { "audio_stream_samples",        0, -1, tci_cmd_audio_stream_samples },
  { "rx_sensors_enable", 1,  2, tci_cmd_rx_sensors_enable },
  { "tx_sensors_enable", 1,  2, tci_cmd_tx_sensors_enable },
  { "spot",              3,  5, tci_cmd_spot },
  { "spot_delete",       1,  1, tci_cmd_spot_delete },
  { "spot_clear",        0,  0, tci_cmd_spot_clear },
  { "audio_start",       1,  1, tci_cmd_audio_start },
  { "audio_stop",        1,  1, tci_cmd_audio_stop },
  { "modulation",        1,  2, tci_cmd_modulation },
  { "vfo",               2,  3, tci_cmd_vfo },
  { "rx_smeter",         1,  3, tci_cmd_rx_smeter },
  { "drive",             0,  2, tci_cmd_drive },
  { "tune_drive",        1,  2, tci_cmd_tune_drive },
  { "rx_filter_band",    1,  3, tci_cmd_rx_filter_band },
  { "cw_macros",         2, -1, tci_cmd_cw_macros },
  { "cw_macros_stop",    0,  0, tci_cmd_cw_macros_stop },
  { "cw_msg",             1,  4, tci_cmd_cw_msg },
  { "cw_terminal",        1,  1, tci_cmd_cw_terminal },
  { "cw_macros_speed",   0,  1, tci_cmd_cw_macros_speed },
  { "cw_keyer_speed",    0,  1, tci_cmd_cw_keyer_speed },
  { "cw_macros_delay",   0,  1, tci_cmd_cw_macros_delay },
  { "cw_macros_speed_up",   1,  1, tci_cmd_cw_macros_speed_up },
  { "cw_macros_speed_down", 1,  1, tci_cmd_cw_macros_speed_down },
  { "stop",              0,  0, tci_cmd_stop },
  { NULL,                0,  0, NULL }
};

static void tci_handle_text (CLIENT *client, char* msg) {
  TCI_CMD cmd;
  if (tci_parse_text (msg, &cmd) < 0 || cmd.cmd == NULL) { return; }
  for (char * p = cmd.cmd; *p != 0; p++) {
    *p = g_ascii_tolower (*p);
  }
  for (int i = 0; tci_dispatch[i].name != NULL; i++) {
    const TCI_DISPATCH *d = &tci_dispatch[i];
    if (cmd.cmd[0] != d->name[0] || strcmp (cmd.cmd, d->name) != 0) { continue; }
    if (cmd.argc < d->min_args) {
      t_print ("TCI%d %s: too few args (%d < %d)\n", client->seq, d->name, cmd.argc, d->min_args);
      return;
    }
    if (d->max_args >= 0 && cmd.argc > d->max_args) {
      t_print ("TCI%d %s: too many args (%d > %d)\n", client->seq, d->name, cmd.argc, d->max_args);
      return;
    }
    d->handler (client, &cmd);
    return;
  }
  if (rigctl_debug) { t_print ("TCI%d unknown command: %s\n", client->seq, cmd.cmd ? cmd.cmd : "(null)"); }
}

static void tci_send_smeter (CLIENT *client, int v) {
  //
  // UNDOCUMENTED in the TCI protocol, but MLDX sends this
  // ATTENTION: in some countries, %f sends a comma instead of a decimal
  //            point and this is a desaster. Therefore we fake a
  //            floating point number.
  //
  char msg[MAXMSGSIZE];
  int lvl;
  if (v < 0 || v > 1) { return; }
  if (v >= receivers || receiver[v] == NULL) { return; }
  lvl = (int) (receiver[v]->rxlvl - 0.5);
  // snprintf(msg, MAXMSGSIZE, "rx_smeter:%d,0,%d.0;",v,lvl);
  // tci_send_text(client, msg);
  // snprintf(msg, MAXMSGSIZE, "rx_smeter:%d,1,%d.0;",v,lvl);
  // tci_send_text(client, msg);
  snprintf (msg, MAXMSGSIZE, "rx_sensors:%d,%d.0;", v, lvl);
  tci_send_text (client, msg);
}

static void tci_send_rx (CLIENT *client, int v) {
  //
  // Send S-meter reading.
  // ATTENTION: in some countries, %f sends a comma instead of a decimal
  //            point and this is a desaster. Therefore we fake a
  //            floating point number.
  //
  char msg[MAXMSGSIZE];
  int lvl;
  if (v < 0 || v > 1) { return; }
  if (v >= receivers || receiver[v] == NULL) { return; }
  lvl = (int) (receiver[v]->rxlvl - 0.5);
  snprintf (msg, MAXMSGSIZE, "rx_channel_sensors:%d,0,%d.0;", v, lvl);
  tci_send_text (client, msg);
  snprintf (msg, MAXMSGSIZE, "rx_channel_sensors:%d,1,%d.0;", v, lvl);
  tci_send_text (client, msg);
}

static gboolean tci_reporter (gpointer data) {
  //
  // This function is called repeatedly as long as the client  runs
  //
  CLIENT *client = (CLIENT*) data;
  if (!client->running) {
    client->tci_timer = 0;
    return G_SOURCE_REMOVE;
  }
  if (++ (client->count) >= 30) {
    client->count = 0;
    (void) tci_queue_frame (client, opPING, NULL, 0);
  }
  //
  // Periodically send
  // - TX frequency        (if changed, every 500 msec)
  // - Split status        (if changed, every 500 msec)
  // - MOX status          (if changed, every 500 msec)
  // - RX sensors          (if requested by client, once per second, ignoring rxsensors_ms)
  // - TX sensors          (if requested by client, once per second, ignoring txsensors_ms)
  // - VFO A/B frequencies (if changed, every 500 msec)
  // - VFO A/B modes       (if changed, every 500 msec)
  //
  long long fx = vfo_get_tx_freq();
  if (fx != client->last_fx) {
    tci_send_txfreq (client);
  }
  int sp = (vfo_get_tx_vfo() == VFO_B);
  int mx = radio_is_transmitting();
  if (sp != client->last_split) {
    tci_send_split (client);
  }
  if (mx != client->last_mox) {
    tci_send_mox (client);
  }
  if (client->rxsensor && (client->count & 1)) {
    tci_send_rx (client, 0);
    tci_send_rx (client, 1);
  }
  if (client->txsensor && (client->count & 1)) {
    tci_send_tx_sensors(client);
  }
  if (receivers > 0 && client->rxsensor && (client->count & 1)) {
    if (receivers == 1) {
      tci_send_smeter (client, 0);
    } else {
      tci_send_smeter (client, 0);
      tci_send_smeter (client, 1);
    }
  }
  //
  // Determine VFO-A/B frequency/mode, report if changed
  //
  long long fa = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
  long long fb = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
  int       ma = vfo[VFO_A].mode;
  int       mb = vfo[VFO_B].mode;
  if (fa != client->last_fa) {
    tci_send_vfo (client, 0, 0);
  }
  if (fb != client->last_fb) {
    tci_send_vfo (client, 0, 1);
    if (receivers > 1) {
      tci_send_vfo (client, 1, 0);
      tci_send_vfo (client, 1, 1);
    }
  }
  if (ma  != client->last_ma) {
    tci_send_mode (client, 0);
    tci_send_rx_filter_band (client, 0);
  }
  if (mb  != client->last_mb) {
    if (receivers > 1) {
      tci_send_mode (client, 1);
      tci_send_rx_filter_band (client, 1);
    } else {
      client->last_mb = mb;
    }
  }
  return TRUE;
}

//
// Initialise TCI client state. Return slot number or
// -1, if all slots are full
//
static int tci_init_client (int fd) {
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    CLIENT *client = tciclient + c;
    if (!client->running) {
      client->seq               = c;
      client->fd                = fd;
      client->running           = 1;
      client->tx_owner          = 0;
      client->tci_timer         = 0;
      client->last_fa           = -1;
      client->last_fb           = -1;
      client->last_fx           = -1;
      client->last_ma           = -1;
      client->last_mb           = -1;
      client->last_split        = -1;
      client->last_mox          = -1;
      client->count             =  0;
      client->rxsensor          =  0;
      client->txsensor          =  0;
      client->idle_queued       =  0;
      client->last_trx          =  0;
      client->wsi               = NULL;
      client->lws_tx_queue      = NULL;
      client->initial_sent      =  0;
      client->tx_audio_enabled  = 0;
      client->tx_audio_rx_count = 0;
      client->binary_rx_buf     = NULL;
      client->binary_rx_len     = 0;
      client->binary_rx_size    = 0;
      for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
        client->rx_audio_enabled[i] = 0;
        client->rx_audio_read_count[i] = 0;
      }
      return c;
    }
  }
  return -1;
}

static int tci_process_ws_payload (gpointer data) {
  PAYLOAD *load = (PAYLOAD *) data;
  switch (load->type) {
  case opTEXT:
    if (rigctl_debug) {
      t_print ("TCI%d command rcvd=%s\n", load->client->seq, load->msg);
    }
    tci_handle_text (load->client, load->msg);
    break;
  case opPING:
    if (rigctl_debug) { t_print ("TCI%d PING rcvd\n", load->client->seq); }
    (void) tci_queue_frame (load->client, opPONG, NULL, 0);
    break;
  case opCLOSE:
    if (rigctl_debug) { t_print ("TCI%d CLOSE rcvd\n", load->client->seq); }
    load->client->running = 0;
    if (load->client->tx_owner) {
      load->client->tx_owner = 0;
      tci_transmitter_owned = 0;
    }
    break;
  default:
    if (rigctl_debug) {
      t_print ("TCI%d unknown frame type=%d ignored\n", load->client->seq, load->type);
    }
    break;
  }
  g_free(data);
  return G_SOURCE_REMOVE;
}

static void tci_send_initial_state (CLIENT *client) {
  //
  // Send initial state info to client
  // using emulatation Expert SunSDR2Pro
  //
  // tci_send_text(client, "protocol:ExpertSDR3,1.8;");
  // tci_send_text(client, "device:SunSDR2PRO;");
  tci_send_text (client, "protocol:ExpertSDR3,2.0;");
  tci_send_text (client, "device:SunSDR2QRP;");
  tci_send_text (client, can_transmit ? "receive_only:false;" : "receive_only:true;");
  tci_send_trx_count (client);
  tci_send_text (client, "channels_count:2;");
  //
  // With transverters etc. the upper frequency can be
  // very large. For the time being we go up to the 70cm band
  // No need to send vfo and modulation  commands, since this is
  // automatically  done in the tci_reporter task.
  //
  // tci_send_text(client, "vfo_limits:0,450000000;");
  // tci_send_text(client, "if_limits:-96000,96000;");
  tci_send_limits (client);
  tci_send_text (client, "modulations_list:LSB,USB,DSB,CW,FMN,AM,DIGU,SPEC,DIGL,SAM,DRM;");
  tci_send_dds (client, VFO_A);
  tci_send_text (client, "if:0,0,0;");
  tci_send_text (client, "if:0,1,0;");
  tci_send_vfo (client, VFO_A, 0);
  tci_send_vfo (client, VFO_A, 1);
  tci_send_mode (client, VFO_A);
  tci_send_rx_filter_band (client, VFO_A);
  if (receivers > 1) {
    tci_send_dds (client, VFO_B);
    tci_send_text (client, "if:1,0,0;");
    tci_send_text (client, "if:1,1,0;");
    tci_send_vfo (client, VFO_B, 0);
    tci_send_vfo (client, VFO_B, 1);
    tci_send_mode (client, VFO_B);
    tci_send_rx_filter_band (client, VFO_B);
  }
  tci_send_text (client, "rx_enable:0,true;");
  if (receivers > 1) {
    tci_send_text (client, "rx_enable:1,true;");
  }
  tci_send_lock (client, VFO_A);
  tci_send_vfo_locks (client, VFO_A);
  tci_send_sql_enable (client, VFO_A);
  tci_send_sql_level (client, VFO_A);
  tci_send_rx_anf_enable (client, VFO_A);
  tci_send_rx_apf_enable (client, VFO_A);
  tci_send_rx_nb_enable (client, VFO_A);
  tci_send_rx_nf_enable (client, VFO_A);
  tci_send_rx_bin_enable (client, VFO_A);
  tci_send_rx_nr_enable (client, VFO_A);
  tci_send_rit_enable (client, VFO_A);
  tci_send_rit_offset (client, VFO_A);
  tci_send_xit_enable (client);
  tci_send_xit_offset (client);
  tci_send_tune_drive (client);
  tci_send_tx_enable (client);
  tci_send_split (client);
  tci_send_mox (client);
  tci_send_tune (client);
  tci_send_mute (client);
  tci_send_rx_mute (client, VFO_A);
  tci_send_volume (client);
  tci_send_rx_volume (client, VFO_A, 0);
  tci_send_rx_volume (client, VFO_A, 1);
  tci_send_agc_gain (client, VFO_A);
  tci_send_agc_mode (client, VFO_A);
  if (receivers > 1) {
    tci_send_sql_enable (client, VFO_B);
    tci_send_sql_level (client, VFO_B);
    tci_send_rx_anf_enable (client, VFO_B);
    tci_send_rx_apf_enable (client, VFO_B);
    tci_send_rx_nb_enable (client, VFO_B);
    tci_send_rx_nf_enable (client, VFO_B);
    tci_send_rx_bin_enable (client, VFO_B);
    tci_send_rx_nr_enable (client, VFO_B);
    tci_send_rit_enable (client, VFO_B);
    tci_send_rit_offset (client, VFO_B);
    tci_send_rx_mute (client, VFO_B);
    tci_send_rx_volume (client, VFO_B, 0);
    tci_send_rx_volume (client, VFO_B, 1);
    tci_send_agc_gain (client, VFO_B);
    tci_send_agc_mode (client, VFO_B);
  }
  tci_send_macros_cwspeed (client);
  tci_send_cw_macros_delay(client);
  tci_send_keyer_cwspeed (client);
  tci_send_text (client, "ready;");
  tci_send_text (client, "start;");
}

static void tci_lws_free_queue (CLIENT *client) {
  GQueue *queue;
  queue = client->lws_tx_queue;
  client->lws_tx_queue = NULL;
  client->idle_queued = 0;
  if (queue == NULL) { return; }
  while (!g_queue_is_empty (queue)) {
    PAYLOAD *resp = (PAYLOAD *) g_queue_pop_head (queue);
    g_free (resp->bin);
    g_free (resp);
  }
  g_queue_free (queue);
}

static int tci_lws_write_queued (CLIENT *client) {
  PAYLOAD *resp = NULL;
  struct lws *wsi;
  wsi = client->wsi;
  if (client->lws_tx_queue != NULL && !g_queue_is_empty (client->lws_tx_queue)) {
    resp = (PAYLOAD*) g_queue_pop_head (client->lws_tx_queue);
  }
  if (resp == NULL) { return 0; }
  if (resp->type == opCLOSE || wsi == NULL) {
    if (client->idle_queued > 0) {
      client->idle_queued--;
    }
    g_free (resp);
    return -1;
  }
  size_t len = (resp->type == opBIN) ? resp->len : strlen (resp->msg);
  enum lws_write_protocol protocol = LWS_WRITE_TEXT;
  unsigned char *buf = g_malloc (LWS_PRE + len);
  int rc;
  if (resp->type == opBIN) {
    protocol = LWS_WRITE_BINARY;
    memcpy (&buf[LWS_PRE], resp->bin, len);
  } else if (resp->type == opPING) {
    protocol = LWS_WRITE_PING;
    memcpy (&buf[LWS_PRE], resp->msg, len);
  } else if (resp->type == opPONG) {
    protocol = LWS_WRITE_PONG;
    memcpy (&buf[LWS_PRE], resp->msg, len);
  } else {
    memcpy (&buf[LWS_PRE], resp->msg, len);
  }
  rc = lws_write (wsi, &buf[LWS_PRE], len, protocol);
  g_free (buf);
  g_free (resp->bin);
  g_free (resp);
  if (client->idle_queued > 0) {
    client->idle_queued--;
  }
  if (rc < 0) {
    client->running = 0;
    if (client->tx_owner) {
      client->tx_owner = 0;
      tci_transmitter_owned = 0;
    }
    return -1;
  }
  if (client->wsi != NULL && client->lws_tx_queue != NULL && !g_queue_is_empty (client->lws_tx_queue)) {
    lws_callback_on_writable (client->wsi);
  }
  return 0;
}

static int tci_lws_callback (struct lws *wsi, enum lws_callback_reasons reason,
                             void *user, void *in, size_t len) {
  int *slot = (int *)user;
  //
  // The first switch statement handles all cases where "slot" need
  // not be properly initialized. All cases end with "return 0"
  //
  switch (reason) {
  case LWS_CALLBACK_ESTABLISHED:
    //
    // put before the switch, because "slot" is not yet defined
    //
    if (rigctl_debug) {
      char proto[128];
      char uri[256];
      proto[0] = 0;
      uri[0] = 0;
      lws_hdr_copy (wsi, proto, sizeof (proto), WSI_TOKEN_PROTOCOL);
      lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_GET_URI);
      t_print ("LWS ESTABLISHED uri=%s protocol=%s\n", uri, proto);
    }
    int c = tci_init_client (lws_get_socket_fd (wsi));
    if (c >= 0 && c < TCI_MAX_CLIENTS) {
      tciclient[c].wsi = wsi;
      tciclient[c].lws_tx_queue = g_queue_new();
      t_print ("%s: starting client in slot %d: socket=%d\n", __func__, tciclient[c].seq, tciclient[c].fd);
      cat_control++;
      g_idle_add (ext_vfo_update, NULL);
      tciclient[c].initial_sent = 0;
      lws_callback_on_writable (wsi);
      tciclient[c].tci_timer = g_timeout_add (500, tci_reporter, tciclient + c);
    } else {
      t_print ("%s: count not start client\n", __func__);
    }
    *slot = c;
    return 0;
    break;
  case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    if (rigctl_debug) {
      char uri[256] = {0};
      char proto[256] = {0};
      char ua[256] = {0};
      lws_hdr_copy (wsi, uri,   sizeof (uri),   WSI_TOKEN_GET_URI);
      lws_hdr_copy (wsi, proto, sizeof (proto), WSI_TOKEN_PROTOCOL);
      lws_hdr_copy (wsi, ua,    sizeof (ua),    WSI_TOKEN_HTTP_USER_AGENT);
      t_print ("LWS HANDSHAKE uri=%s\n", uri);
      t_print ("LWS HANDSHAKE protocol=%s\n", proto);
      t_print ("LWS HANDSHAKE user-agent=%s\n", ua);
    }
    return 0;
    break;
  case LWS_CALLBACK_HTTP:
    if (rigctl_debug) {
      char uri[256];
      uri[0] = 0;
      lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_GET_URI);
      t_print ("LWS HTTP uri=%s\n", uri);
    }
    return 0;
    break;
  case LWS_CALLBACK_RECEIVE:
  case LWS_CALLBACK_SERVER_WRITEABLE:
  case LWS_CALLBACK_CLOSED:
    //
    // These three cases will be handled leter on, and here we must secure
    // That the "user" parameter (slot) is valid
    //
    if (slot == NULL) {
      t_print("%s: NULL slot\n", __func__);
      return 0;
    }
    if (*slot < 0 || *slot >= TCI_MAX_CLIENTS) {
      t_print("%s: Invalid slot %d\n", __func__, *slot);
      return 0;
    }
    if (!tciclient[*slot].running) {
      t_print("%s: client %d not running\n", __func__, *slot);
      return 0;
    }
    break;
  default:
    return 0;
    break;
  }
  //
  // At this point we have secured that "client" points to a valid slot
  //
  CLIENT *client = tciclient + *slot;
  switch (reason) {
  case LWS_CALLBACK_RECEIVE:
    if (lws_frame_is_binary (wsi)) {
      tci_handle_binary_lws (client, (const unsigned char*) in, len, wsi);
      break;
    } else {
      if (rigctl_debug) { t_print ("LWS RECEIVE len=%zu\n", len); }
      PAYLOAD *load = g_new(PAYLOAD, 1);
      size_t n = (len < sizeof (load->msg) - 1) ? len : sizeof (load->msg) - 1;
      memcpy (load->msg, in, n);
      load->client = client;
      load->msg[n] = 0;
      load->type = opTEXT;
      g_idle_add(tci_process_ws_payload, load);
    }
    break;
  case LWS_CALLBACK_SERVER_WRITEABLE:
    if (client->wsi == NULL) { return 0; }
    if (!client->running) { return -1; }
    if (!client->initial_sent) {
      tci_send_initial_state (client);
      client->initial_sent = 1;
    }
    return tci_lws_write_queued (client);
    break;
  case LWS_CALLBACK_CLOSED:
    if (rigctl_debug) {
      t_print ("LWS CLOSED client=%d\n", client->seq);
    }
    client->running = 0;
    if (client->tx_owner) {
      //
      // If the last trx command from this client was "go TX", then
      // put piHPSDR into RX mode
      //
      if (client->tci_clearmox_timer != 0) {
        g_source_remove(client->tci_clearmox_timer);
        client->tci_clearmox_timer = 0;
      }
      if (client->last_trx) {
        g_idle_add(ext_radio_set_mox, GINT_TO_POINTER(0));
        client->last_trx = 0;
      }
      client->tx_owner = 0;
      tci_transmitter_owned = 0;
    }
    client->wsi = NULL;
    tci_update_audio_global();
    if (client->tci_timer != 0) {
      g_source_remove (client->tci_timer);
      client->tci_timer = 0;
    }
    tci_lws_free_queue (client);
    g_free (client->binary_rx_buf); // g_free is valid on NULL pointers
    client->binary_rx_buf = NULL;
    client->binary_rx_len = 0;
    client->binary_rx_size = 0;
    t_print ("%s: leaving client\n", __func__);
    if (cat_control > 0) {
      cat_control--;
    }
    g_idle_add (ext_vfo_update, NULL);
    break;
  default:
    break;
  }
  return 0;
}

static const struct lws_protocols tci_lws_protocols[] = {
  { "chat",       tci_lws_callback, sizeof (int), 8192, 0, NULL, 0 },
  { "superchat",  tci_lws_callback, sizeof (int), 8192, 0, NULL, 0 },
  { "tci",        tci_lws_callback, sizeof (int), 8192, 0, NULL, 0 },
  LWS_PROTOCOL_LIST_TERM
};

static gpointer tci_lws_server (gpointer data) {
  static int first = 1;
  struct lws_context_creation_info info;
  int port = GPOINTER_TO_INT (data);
  lws_set_log_level(LLL_ERR, NULL);
  memset (&info, 0, sizeof (info));
  signal (SIGPIPE, SIG_IGN);
  info.port = port;
  info.protocols = tci_lws_protocols;
  info.gid = -1;
  info.uid = -1;
  if (first) {
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    first = 0;
  }
  t_print ("%s: starting TCI LWS server on port %d\n", __func__, port);
  tci_lws_context = lws_create_context (&info);
  if (tci_lws_context == NULL) {
    t_print ("%s: lws_create_context failed\n", __func__);
    return NULL;
  }
  for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
    tciclient[c].running = 0;
  }
  while (tci_running) {
    int do_writable = 0;
    tci_service_rx_audio();
    if (tci_lws_pending_writable) {
      tci_lws_pending_writable = 0;
      do_writable = 1;
    }
    if (do_writable) {
      for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
        CLIENT *client = tciclient + c;
        struct lws *wsi = NULL;
        if (client->running && client->wsi != NULL &&
            client->lws_tx_queue != NULL && !g_queue_is_empty (client->lws_tx_queue)) {
          wsi = client->wsi;
        }
        if (wsi != NULL) {
          lws_callback_on_writable (wsi);
        }
      }
    }
    lws_service (tci_lws_context, 0);
    g_usleep (1000);
  }
  lws_context_destroy (tci_lws_context);
  tci_lws_context = NULL;
  return NULL;
}

//
// This is called for each TX mic sample, and fires a TCI chrono frame
// to all tx audio clients once per audio frame (this is the clock for
// the client sending audio data)
//
// Note there can be at most one TX audio client active at any time,
// therefore send the chrono frame only to the first client that wishes
// to do tx audio.
//
void tci_tx_chrono_loop() {
  static int counter = 1;
  if (--counter <= 0) {
    for (int c = 0; c < TCI_MAX_CLIENTS; c++) {
      if (tciclient[c].running && tciclient[c].tx_audio_enabled) {
        tci_queue_tx_chrono_frame(tciclient + c);
        // Send TX CHRONO frames only to one client!
        break;
      }
    }
    counter = TCI_TX_AUDIO_FRAME_FRAMES;
  }
}

