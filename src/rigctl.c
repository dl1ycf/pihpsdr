/* Copyright (C)
*  2016 Steve Wilson <wevets@gmail.com>
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

/* TS-2000 emulation via TCP */
/*
 * PiHPSDR RigCtl by Steve KA6S Oct 16 2016
 * With a kindly assist from Jae, K5JAE who has helped
 * greatly with hamlib integration!
 */
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "receiver.h"
#include "toolbar.h"
#include "band_menu.h"
#include "sliders.h"
#include "rigctl.h"
#include "radio.h"
#include "channel.h"
#include "filter.h"
#include "mode.h"
#include "filter.h"
#include "band.h"
#include "bandstack.h"
#include "filter_menu.h"
#include "vfo.h"
#include "sliders.h"
#include "transmitter.h"
#include "agc.h"
#include <wdsp.h>
#include "store.h"
#include "ext.h"
#include "rigctl_menu.h"
#include "noise_menu.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "iambic.h"              // declare keyer_update()
#include "actions.h"
#include "new_menu.h"
#include "zoompan.h"
#include "exit_menu.h"
#include "message.h"

#include <math.h>

#define NEW_PARSER

// IP stuff below
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <netinet/tcp.h>

int rigctl_port_base = 19090;
int rigctl_enable = 0;

// max number of bytes we can get at once
#define MAXDATASIZE 2000

gboolean rigctl_debug = FALSE;

int parse_cmd (void *data);

int cat_control;

extern int enable_tx_equalizer;

typedef struct {GMutex m; } GT_MUTEX;

GT_MUTEX * mutex_a;
GT_MUTEX * mutex_busy;

#define MAX_CLIENTS 3
static GThread *rigctl_server_thread_id = NULL;
static GThread *rigctl_cw_thread_id = NULL;
static int server_running;

static int server_socket = -1;
static struct sockaddr_in server_address;

typedef struct _client {
  int fd;
  int fifo;    // only needed for serial clients to
  // indicate this is a FIFO and not a
  // true serial line
  int busy;    // only needed for serial clients over FIFOs
  int done;    // only needed for serial clients over FIFOs
  int running; // set this to zero to terminate client
  socklen_t address_length;
  struct sockaddr_in address;
  GThread *thread_id;
  guint andromeda_timer;  // for periodic andromeda_tasks
} CLIENT;

typedef struct _command {
  CLIENT *client;
  char *command;
} COMMAND;

static CLIENT tcp_client[MAX_CLIENTS];     // TCP clients
static CLIENT serial_client[MAX_SERIAL];   // serial clienta
SERIALPORT SerialPorts[MAX_SERIAL];

static gpointer rigctl_client (gpointer data);

void close_rigctl_ports() {
  int i;
  struct linger linger = { 0 };
  linger.l_onoff = 1;
  linger.l_linger = 0;
  t_print("%s: server_socket=%d\n", __FUNCTION__, server_socket);
  server_running = 0;

  for (i = 0; i < MAX_CLIENTS; i++) {
    tcp_client[i].running = 0;

    if (tcp_client[i].fd != -1) {
      t_print("%s: setting SO_LINGER to 0 for client_socket: %d\n", __FUNCTION__, tcp_client[i].fd);

      if (setsockopt(tcp_client[i].fd, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) == -1) {
        t_perror("setsockopt(...,SO_LINGER,...) failed for client");
      }

      t_print("%s: closing client socket: %d\n", __FUNCTION__, tcp_client[i].fd);
      close(tcp_client[i].fd);
      tcp_client[i].fd = -1;
    }

    if (tcp_client[i].thread_id) {
      g_thread_join(tcp_client[i].thread_id);
      tcp_client[i].thread_id = NULL;
    }
  }

  if (server_socket >= 0) {
    t_print("%s: setting SO_LINGER to 0 for server_socket: %d\n", __FUNCTION__, server_socket);

    if (setsockopt(server_socket, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) == -1) {
      t_perror("setsockopt(...,SO_LINGER,...) failed for server");
    }

    t_print("s: closing server_socket: %d\n", __FUNCTION__, server_socket);
    close(server_socket);
    server_socket = -1;
  }
}

//
// Check Running on CM4/Saturn board
//
/* test /dev/xdma dir exists (1 success, -1 does not exist, -2 not dir) */
int
xdma_dir ()
{
  DIR *dirptr;
  if (access ( "/dev/xdma", F_OK ) != -1 ) {
    // file exists
    if ((dirptr = opendir (d)) != NULL) {
      closedir (dirptr); /* d exists and is a directory */
    } else {
      return -2; /* d exists but is not a directory */
    }
  } else {
    return -1;     /* d does not exist */
  }
  return 1;
}

//
//  CW ring buffer
//

#define CW_BUF_SIZE 80
static char cw_buf[CW_BUF_SIZE];
static int  cw_buf_in, cw_buf_out;

static int dotsamples;
static int dashsamples;

//
// send_dash()         send a "key-down" of a dashlen, followed by a "key-up" of a dotlen
// send_dot()          send a "key-down" of a dotlen,  followed by a "key-up" of a dotlen
// send_space(int len) send a "key_down" of zero,      followed by a "key-up" of len*dotlen
//
// The "trick" to get proper timing is, that we really specify  the number of samples
// for the next element (dash/dot/nothing) and the following pause. 30 wpm is no
// problem, and without too much "busy waiting". We just take a nap until 10 msec
// before we have to act, and then wait several times for 1 msec until we can shoot.
//
void send_dash() {
  for (;;) {
    int TimeToGo = cw_key_up + cw_key_down;

    // TimeToGo is invalid if local CW keying has set in
    if (cw_key_hit || cw_not_ready) { return; }

    if (TimeToGo == 0) { break; }

    // sleep until 10 msec before ignition
    if (TimeToGo > 500) { usleep((long)(TimeToGo - 500) * 20L); }

    // sleep 1 msec
    usleep(1000L);
  }

  // If local CW keying has set in, do not interfere
  if (cw_key_hit || cw_not_ready) { return; }

  cw_key_down = dashsamples;
  cw_key_up   = dotsamples;
}

void send_dot() {
  for (;;) {
    int TimeToGo = cw_key_up + cw_key_down;

    // TimeToGo is invalid if local CW keying has set in
    if (cw_key_hit || cw_not_ready) { return; }

    if (TimeToGo == 0) { break; }

    // sleep until 10 msec before ignition
    if (TimeToGo > 500) { usleep((long)(TimeToGo - 500) * 20L); }

    // sleep 1 msec
    usleep(1000L);
  }

  // If local CW keying has set in, do not interfere
  if (cw_key_hit || cw_not_ready) { return; }

  cw_key_down = dotsamples;
  cw_key_up   = dotsamples;
}

void send_space(int len) {
  for (;;) {
    int TimeToGo = cw_key_up + cw_key_down;

    // TimeToGo is invalid if local CW keying has set in
    if (cw_key_hit || cw_not_ready) { return; }

    if (TimeToGo == 0) { break; }

    // sleep until 10 msec before ignition
    if (TimeToGo > 500) { usleep((long)(TimeToGo - 500) * 20L); }

    // sleep 1 msec
    usleep(1000L);
  }

  // If local CW keying has set in, do not interfere
  if (cw_key_hit || cw_not_ready) { return; }

  cw_key_up = len * dotsamples;
}

void rigctl_send_cw_char(char cw_char) {
  char pattern[9],*ptr;
  ptr = &pattern[0];

  switch (cw_char) {
  case 'a':
  case 'A':
    strcpy(pattern, ".-");
    break;

  case 'b':
  case 'B':
    strcpy(pattern, "-...");
    break;

  case 'c':
  case 'C':
    strcpy(pattern, "-.-.");
    break;

  case 'd':
  case 'D':
    strcpy(pattern, "-..");
    break;

  case 'e':
  case 'E':
    strcpy(pattern, ".");
    break;

  case 'f':
  case 'F':
    strcpy(pattern, "..-.");
    break;

  case 'g':
  case 'G':
    strcpy(pattern, "--.");
    break;

  case 'h':
  case 'H':
    strcpy(pattern, "....");
    break;

  case 'i':
  case 'I':
    strcpy(pattern, "..");
    break;

  case 'j':
  case 'J':
    strcpy(pattern, ".---");
    break;

  case 'k':
  case 'K':
    strcpy(pattern, "-.-");
    break;

  case 'l':
  case 'L':
    strcpy(pattern, ".-..");
    break;

  case 'm':
  case 'M':
    strcpy(pattern, "--");
    break;

  case 'n':
  case 'N':
    strcpy(pattern, "-.");
    break;

  case 'o':
  case 'O':
    strcpy(pattern, "---");
    break;

  case 'p':
  case 'P':
    strcpy(pattern, ".--.");
    break;

  case 'q':
  case 'Q':
    strcpy(pattern, "--.-");
    break;

  case 'r':
  case 'R':
    strcpy(pattern, ".-.");
    break;

  case 's':
  case 'S':
    strcpy(pattern, "...");
    break;

  case 't':
  case 'T':
    strcpy(pattern, "-");
    break;

  case 'u':
  case 'U':
    strcpy(pattern, "..-");
    break;

  case 'v':
  case 'V':
    strcpy(pattern, "...-");
    break;

  case 'w':
  case 'W':
    strcpy(pattern, ".--");
    break;

  case 'x':
  case 'X':
    strcpy(pattern, "-..-");
    break;

  case 'y':
  case 'Y':
    strcpy(pattern, "-.--");
    break;

  case 'z':
  case 'Z':
    strcpy(pattern, "--..");
    break;

  case '0':
    strcpy(pattern, "-----");
    break;

  case '1':
    strcpy(pattern, ".----");
    break;

  case '2':
    strcpy(pattern, "..---");
    break;

  case '3':
    strcpy(pattern, "...--");
    break;

  case '4':
    strcpy(pattern, "....-");
    break;

  case '5':
    strcpy(pattern, ".....");
    break;

  case '6':
    strcpy(pattern, "-....");
    break;

  case '7':
    strcpy(pattern, "--...");
    break;

  case '8':
    strcpy(pattern, "---..");
    break;

  case '9':
    strcpy(pattern, "----.");
    break;

  //
  //     DL1YCF:
  //     There were some signs I considered wrong, other
  //     signs missing. Therefore I put the signs here
  //     from ITU Recommendation M.1677-1 (2009)
  //     in the order given there.
  //
  case '.':
    strcpy(pattern, ".-.-.-");
    break;

  case ',':
    strcpy(pattern, "--..--");
    break;

  case ':':
    strcpy(pattern, "---..");
    break;

  case '?':
    strcpy(pattern, "..--..");
    break;

  case '\'':
    strcpy(pattern, ".----.");
    break;

  case '-':
    strcpy(pattern, "-....-");
    break;

  case '/':
    strcpy(pattern, "-..-.");
    break;

  case '(':
    strcpy(pattern, "-.--.");
    break;

  case ')':
    strcpy(pattern, "-.--.-");
    break;

  case '"':
    strcpy(pattern, ".-..-.");
    break;

  case '=':
    strcpy(pattern, "-...-");
    break;

  case '+':
    strcpy(pattern, ".-.-.");
    break;

  case '@':
    strcpy(pattern, ".--.-.");
    break;

  //
  //     Often used, but not ITU: Ampersand for "wait"
  //
  case '&':
    strcpy(pattern, ".-...");
    break;

  default:
    strcpy(pattern, "");
  }

  while (*ptr != '\0') {
    if (*ptr == '-') {
      send_dash();
    }

    if (*ptr == '.') {
      send_dot();
    }

    ptr++;
  }

  // The last element (dash or dot) sent already has one dotlen space appended.
  // If the current character is another "printable" sign, we need an additional
  // pause of 2 dotlens to form the inter-character spacing of 3 dotlens.
  // However if the current character is a "space" we must produce an inter-word
  // spacing (7 dotlens) and therefore need 6 additional dotlens
  // We need no longer take care of a sequence of spaces since adjacent spaces
  // are now filtered out while filling the CW character (ring-) buffer.

  if (cw_char == ' ') {
    send_space(6);  // produce inter-word space of 7 dotlens
  } else {
    send_space(2);  // produce inter-character space of 3 dotlens
  }
}

//
// This thread constantly looks whether CW data
// is available in the ring buffer, and produces CW in this case.
//
static gpointer rigctl_cw_thread(gpointer data) {
  int i;
  char cwchar;

  while (server_running) {
    // wait for CW data (periodically look every 100 msec)
    if (cw_buf_in == cw_buf_out) {
      usleep(100000L);
      continue;
    }

    //
    // if TX mode is not CW, drain ring buffer
    //
    int txmode = get_tx_mode();

    if (txmode != modeCWU && txmode != modeCWL) {
      cw_buf_out = cw_buf_in;
      continue;
    }

    //
    // Take one character from the ring buffer
    //
    cwchar = cw_buf[cw_buf_out];
    i = cw_buf_out + 1;

    if (i >= CW_BUF_SIZE) { i = 0; }

    cw_buf_out = i;
    cw_key_hit = 0;
    // The dot and dash length may have changed, so recompute them here
    // This means that we can change the speed (KS command) while
    // the buffer is being sent
    dotsamples = 57600 / cw_keyer_speed;
    dashsamples = (3456 * cw_keyer_weight) / cw_keyer_speed;
    CAT_cw_is_active = 1;

    if (!mox) {
      // activate PTT
      g_idle_add(ext_mox_update, GINT_TO_POINTER(1));
      // have to wait until it is really there
      // Note that if out-of-band, we would wait
      // forever here, so allow at most 200 msec
      // We also have to wait for cw_not_ready becoming zero
      i = 200;

      while ((!mox || cw_not_ready) && i-- > 0) { usleep(1000L); }

      // still no MOX? --> silently discard CW character and give up
      if (!mox) {
        CAT_cw_is_active = 0;
        continue;
      }
    }

    // At this point, mox==1 and CAT_cw_active == 1
    if (cw_key_hit || cw_not_ready) {
      //
      // CW transmission has been aborted, either due to manually
      // removing MOX, changing the mode to non-CW, or because a CW key has been hit.
      // Do not remove PTT in the latter case
      CAT_cw_is_active = 0;

      // If a CW key has been hit, we continue in TX mode.
      // Otherwise, switch PTT off.
      if (!cw_key_hit && mox) {
        g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
      }

      //
      // drain ring buffer, then wait 0.5 sec, then drain again
      // this lets the CAT system swallow further incoming CW messages
      //
      cw_buf_out = cw_buf_in;
      usleep(500000L);
      cw_buf_out = cw_buf_in;
    } else {
      rigctl_send_cw_char(cwchar);

      //
      // Character has been sent, so continue.
      // Since the second character possibly comes 250 msec after
      // the first one, we have to wait if the buffer stays
      // empty. If so, stop CAT CW
      // If the ring buffer is empty for 500 msec, stop CAT CW
      //
      for (i = 0; i < 5; i++ ) {
        if (cw_buf_in != cw_buf_out) { break; }

        usleep(50000);
      }

      if (cw_buf_in != cw_buf_out) { continue; }

      CAT_cw_is_active = 0;

      if (!cw_key_hit) {
        g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
        // wait up to 500 msec for MOX having gone
        // otherwise there might be a race condition when sending
        // the next character really soon
        i = 10;

        while (mox && (i--) > 0) { usleep(50000L); }
      }
    }

    // end of while (server_running)
  }

  // We arrive here if the rigctl server shuts down.
  // This very rarely happens. But we should shut down the
  // local CW system gracefully, in case we were in the mid
  // of a transmission
  if (CAT_cw_is_active) {
    CAT_cw_is_active = 0;
    g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
  }

  rigctl_cw_thread_id = NULL;
  return NULL;
}

void send_resp (int fd, char * msg) {
  if (rigctl_debug) { t_print("RIGCTL: RESP=%s\n", msg); }

  int length = strlen(msg);
  int count = 0;

  //
  // Possibly, the channel is already closed. In this case
  // give up (rc < 0) or at most try a few times (rc == 0)
  // since we are in the GTK idle loop
  //
  while (length > 0) {
    int rc = write(fd, msg, length);

    if (rc < 0) { return; }

    if (rc == 0) {
      count++;

      if (count > 10) { return; }
    }

    length -= rc;
    msg += rc;
  }
}

//
// 2-25-17 - K5JAE - removed duplicate rigctl
//

static gpointer rigctl_server(gpointer data) {
  int port = GPOINTER_TO_INT(data);
  int on = 1;
  int i;
  t_print("%s: starting TCP server on port %d\n", __FUNCTION__, port);
  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (server_socket < 0) {
    t_perror("rigctl_server: listen socket failed");
    return NULL;
  }

  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  // bind to listening port
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr * )&server_address, sizeof(server_address)) < 0) {
    t_perror("rigctl_server: listen socket bind failed");
    close(server_socket);
    return NULL;
  }

  for (i = 0; i < MAX_CLIENTS; i++) {
    tcp_client[i].fd = -1;
  }

  // listen with a max queue of 3
  if (listen(server_socket, 3) < 0) {
    t_perror("rigctl_server: listen failed");
    close(server_socket);
    return NULL;
  }

  // must start the thread here in order NOT to inherit a lock
  cw_buf_in = 0;
  cw_buf_out = 0;

  if (!rigctl_cw_thread_id) { rigctl_cw_thread_id = g_thread_new("RIGCTL cw", rigctl_cw_thread, NULL); }

  while (server_running) {
    int spare;
    //
    // find a spare slot
    //
    spare = -1;

    for (i = 0; i < MAX_CLIENTS; i++) {
      if (tcp_client[i].fd == -1) {
        spare = i;
        break;
      }
    }

    // if all slots are in use, wait and continue
    if (spare < 0) {
      usleep(100000L);
      continue;
    }

    //
    // A slot is available, try to get connection via accept()
    //
    t_print("%s: slot= %d waiting for connection\n", __FUNCTION__, spare);
    tcp_client[spare].fd = accept(server_socket, (struct sockaddr*)&tcp_client[spare].address,
                                  &tcp_client[spare].address_length);

    if (tcp_client[spare].fd < 0) {
      t_perror("rigctl_server: client accept failed");
      tcp_client[spare].fd = -1;
      continue;
    }

    t_print("%s: slot= %d connected with fd=%d\n", __FUNCTION__, spare, tcp_client[spare].fd);
    //
    // Setting TCP_NODELAY may (or may not) improve responsiveness
    // by *disabling* Nagle's algorithm for clustering small packets
    //
#ifdef __APPLE__

    if (setsockopt(tcp_client[spare].fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#else

    if (setsockopt(tcp_client[spare].fd, SOL_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#endif
      t_perror("TCP_NODELAY");
    }

    //
    // Spawn off a thread for handling this new connection
    //
    tcp_client[spare].running = 1;
    tcp_client[spare].thread_id = g_thread_new("rigctl client", rigctl_client, (gpointer)&tcp_client[spare]);
  }

  close(server_socket);
  return NULL;
}

static gpointer rigctl_client (gpointer data) {
  CLIENT *client = (CLIENT *)data;
  t_print("%s: starting rigctl_client: socket=%d\n", __FUNCTION__, client->fd);
  g_mutex_lock(&mutex_a->m);
  cat_control++;

  if (rigctl_debug) { t_print("RIGCTL: CTLA INC cat_contro=%d\n", cat_control); }

  g_mutex_unlock(&mutex_a->m);
  g_idle_add(ext_vfo_update, NULL);
  int i;
  int numbytes;
  char  cmd_input[MAXDATASIZE] ;
  char *command = g_new(char, MAXDATASIZE);
  int command_index = 0;

  while (client->running && (numbytes = recv(client->fd, cmd_input, MAXDATASIZE - 2, 0)) > 0 ) {
    for (i = 0; i < numbytes; i++) {
      command[command_index] = cmd_input[i];
      command_index++;

      if (cmd_input[i] == ';') {
        command[command_index] = '\0';

        if (rigctl_debug) { t_print("RIGCTL: command=%s\n", command); }

        COMMAND *info = g_new(COMMAND, 1);
        info->client = client;
        info->command = command;
        g_idle_add(parse_cmd, info);
        command = g_new(char, MAXDATASIZE);
        command_index = 0;
      }
    }
  }

  t_print("%s: Leaving rigctl_client thread\n", __FUNCTION__);

  //
  // If rigctl is disabled via the GUI, the connections are closed by close_rigctl_ports()
  // but even the we should decrement cat_control
  //
  if (client->fd != -1) {
    t_print("%s: setting SO_LINGER to 0 for client_socket: %d\n", __FUNCTION__, client->fd);
    struct linger linger = { 0 };
    linger.l_onoff = 1;
    linger.l_linger = 0;

    if (setsockopt(client->fd, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) == -1) {
      t_perror("setsockopt(...,SO_LINGER,...) failed for client");
    }

    close(client->fd);
    client->fd = -1;
  }

  // Decrement CAT_CONTROL
  g_mutex_lock(&mutex_a->m);
  cat_control--;

  if (rigctl_debug) { t_print("RIGCTL: CTLA DEC - cat_control=%d\n", cat_control); }

  g_mutex_unlock(&mutex_a->m);
  g_idle_add(ext_vfo_update, NULL);
  return NULL;
}

static int ts2000_mode(int m) {
  int mode = 1;

  switch (m) {
  case modeLSB:
    mode = 1;
    break;

  case modeUSB:
    mode = 2;
    break;

  case modeCWL:
    mode = 7;
    break;

  case modeCWU:
    mode = 3;
    break;

  case modeFMN:
    mode = 4;
    break;

  case modeAM:
  case modeSAM:
    mode = 5;
    break;

  case modeDIGL:
    mode = 6;
    break;

  case modeDIGU:
    mode = 9;
    break;

  default:
    break;
  }

  return mode;
}


gboolean parse_extended_cmd (const char *command, const CLIENT *client) {
  gboolean implemented = TRUE;
  char reply[256];
  reply[0] = '\0';

  switch (command[2]) {
  case 'A': //ZZAx
    switch (command[3]) {
    case 'A': //ZZAA
      implemented = FALSE;
      break;

    case 'B': //ZZAB
      implemented = FALSE;
      break;

    case 'C': //ZZAC

      // sets or reads the Step Size
      if (command[4] == ';') {
        // read the step size
        sprintf(reply, "ZZAC%02d;", vfo_get_stepindex());
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';') {
        // set the step size
        int i = atoi(&command[4]) ;
        vfo_set_step_from_index(i);
        g_idle_add(ext_vfo_update, NULL);
      } else {
      }

      break;

    case 'D': //ZZAD

      // move VFO A down by selected step
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_A, -hz, FALSE);
      } else {
      }

      break;

    case 'E': //ZZAE

      // move VFO A down nn tune steps
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_A, -steps);
      }

      break;

    case 'F': //ZZAF

      // move VFO A up nn tune steps
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_A, steps);
      }

      break;

    case 'G': //ZZAG

      // read/set audio gain
      if (command[4] == ';') {
        // send reply back
        sprintf(reply, "ZZAG%03d;", (int)(receiver[0]->volume * 100.0));
        send_resp(client->fd, reply) ;
      } else {
        int gain = atoi(&command[4]);
        receiver[0]->volume = (double)gain / 100.0;
        update_af_gain();
      }

      break;

    case 'I': //ZZAI
      implemented = FALSE;
      break;

    case 'P': //ZZAP
      implemented = FALSE;
      break;

    case 'R': //ZZAR

      // read/set RX0 AGC Threshold
      if (command[4] == ';') {
        // send reply back
        sprintf(reply, "ZZAR%+04d;", (int)(receiver[0]->agc_gain));
        send_resp(client->fd, reply) ;
      } else {
        int threshold = atoi(&command[4]);
        set_agc_gain(VFO_A, (double)threshold);
      }

      break;

    case 'S': //ZZAS

      // read/set RX1 AGC Threshold
      if (receivers == 2) {
        if (command[4] == ';') {
          // send reply back
          sprintf(reply, "ZZAS%+04d;", (int)(receiver[1]->agc_gain));
          send_resp(client->fd, reply) ;
        } else {
          int threshold = atoi(&command[4]);
          set_agc_gain(VFO_B, (double)threshold);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'T': //ZZAT
      implemented = FALSE;
      break;

    case 'U': //ZZAU

      // move VFO A up by selected step
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_A, hz, FALSE);
      } else {
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'B': //ZZBx
    switch (command[3]) {
    case 'A': //ZZBA

      // move RX2 down one band
      if (command[4] == ';') {
        if (receivers == 2) {
          band_minus(receiver[1]->id);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'B': //ZZBB

      // move RX2 up one band
      if (command[4] == ';') {
        if (receivers == 2) {
          band_plus(receiver[1]->id);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'D': //ZZBD

      // move RX1 down one band
      if (command[4] == ';') {
        band_minus(receiver[0]->id);
      }

      break;

    case 'E': //ZZBE

      // move VFO B down nn tune steps
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_B, -steps);
      }

      break;

    case 'F': //ZZBF

      // move VFO B up nn tune steps
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_B, +steps);
      }

      break;

    case 'G': //ZZBG
      implemented = FALSE;
      break;

    case 'I': //ZZBI
      implemented = FALSE;
      break;

    case 'M': //ZZBM

      // move VFO B down by selected step
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_B, -hz, FALSE);
      } else {
      }

      break;

    case 'P': //ZZBP

      // move VFO B up by selected step
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_B, hz, FALSE);
      } else {
      }

      break;

    case 'R': //ZZBR
      implemented = FALSE;
      break;

    case 'S': //ZZBS

      // set/read RX1 band switch
      if (command[4] == ';') {
        int b;

        switch (vfo[VFO_A].band) {
        case band136:
          b = 136;
          break;

        case band472:
          b = 472;
          break;

        case band160:
          b = 160;
          break;

        case band80:
          b = 80;
          break;

        case band60:
          b = 60;
          break;

        case band40:
          b = 40;
          break;

        case band30:
          b = 30;
          break;

        case band20:
          b = 20;
          break;

        case band17:
          b = 17;
          break;

        case band15:
          b = 15;
          break;

        case band12:
          b = 12;
          break;

        case band10:
          b = 10;
          break;

        case band6:
          b = 6;
          break;

        case bandGen:
          b = 888;
          break;

        case bandWWV:
          b = 999;
          break;

        default:
          b = 20;
          break;
        }

        sprintf(reply, "ZZBS%03d;", b);
        send_resp(client->fd, reply) ;
      } else if (command[7] == ';') {
        int band = band20;
        int b = atoi(&command[4]);

        switch (b) {
        case 136:
          band = band136;
          break;

        case 472:
          band = band472;
          break;

        case 160:
          band = band160;
          break;

        case 80:
          band = band80;
          break;

        case 60:
          band = band60;
          break;

        case 40:
          band = band40;
          break;

        case 30:
          band = band30;
          break;

        case 20:
          band = band20;
          break;

        case 17:
          band = band17;
          break;

        case 15:
          band = band15;
          break;

        case 12:
          band = band12;
          break;

        case 10:
          band = band10;
          break;

        case 6:
          band = band6;
          break;

        case 888:
          band = bandGen;
          break;

        case 999:
          band = bandWWV;
          break;
        }

        vfo_band_changed(VFO_A, band);
      }

      break;

    case 'T': //ZZBT
      // set/read RX2 band switch
      break;

    case 'U': //ZZBU

      // move RX1 up one band
      if (command[4] == ';') {
        band_plus(receiver[0]->id);
      }

      break;

    case 'Y': //ZZBY
      // closes console (ignored)
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'C': //ZZCx
    switch (command[3]) {
    case 'B': //ZZCB: VFO A to B
      if (!locked) {
        if (command[4] == ';') {
          vfo_a_to_b();
        }
      }

      break;

    case 'D': //ZZCD: VFO B to A
      if (!locked) {
        if (command[4] == ';') {
          vfo_b_to_a();
        }
      }

      break;

    case 'F': //ZZCF: Swap VFO A and B
      if (!locked) {
        if (command[4] == ';') {
          vfo_a_swap_b();
        }
      }

      break;

    case 'I': //ZZCI
      implemented = FALSE;
      break;

    case 'L': //ZZCL
      implemented = FALSE;
      break;

    case 'M': //ZZCM
      implemented = FALSE;
      break;

    case 'N': //ZZCN

      // set/read VFO A CTUN
      if (command[4] == ';') {
        // return the CTUN status
        sprintf(reply, "ZZCN%d;", vfo[VFO_A].ctun);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int state = atoi(&command[4]);
        vfo_ctun_update(VFO_A, state);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'O': //ZZCO

      // set/read VFO B CTUN
      if (command[4] == ';') {
        // return the CTUN status
        sprintf(reply, "ZZCO%d;", vfo[VFO_B].ctun);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int state = atoi(&command[4]);
        vfo_ctun_update(VFO_B, state);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'P': //ZZCP

      // set/read compander
      if (command[4] == ';') {
        sprintf(reply, "ZZCP%d;", 0);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        // ignore
      }

      break;

    case 'S': //ZZCS
      implemented = FALSE;
      break;

    case 'T': //ZZCT
      implemented = FALSE;
      break;

    case 'U': //ZZCU
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'D': //ZZDx
    switch (command[3]) {
    case 'A': //ZZDA
      break;

    case 'B': //ZZDB

      // set/read RX Reference
      if (command[4] == ';') {
        sprintf(reply, "ZZDB%d;", 0); // currently always 0
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        // ignore
      }

      break;

    case 'C': //ZZDC

      // set/get diversity gain
      if (command[4] == ';') {
        sprintf(reply, "ZZDC%04d;", (int)div_gain);
        send_resp(client->fd, reply) ;
      } else if (command[8] == ';') {
        // ignore
      }

      break;

    case 'D': //ZZDD

      // set/get diversity phase
      if (command[4] == ';') {
        sprintf(reply, "ZZDD%04d;", (int)div_phase);
        send_resp(client->fd, reply) ;
      } else if (command[8] == ';') {
        // ignore
      }

    case 'E': //ZZDE
      implemented = FALSE;
      break;

    case 'F': //ZZDF
      implemented = FALSE;
      break;

    case 'M': //ZZDM

      // set/read Display Mode
      if (command[4] == ';') {
        int v = 0;

        if (receiver[0]->display_waterfall) {
          v = 8;
        } else {
          v = 2;
        }

        sprintf(reply, "ZZDM%d;", v);
        send_resp(client->fd, reply) ;
      } else {
      }

      break;

    case 'N': //ZZDN

      // set/read waterfall low
      if (command[4] == ';') {
        sprintf(reply, "ZZDN%+4d;", receiver[0]->waterfall_low);
        send_resp(client->fd, reply) ;
      } else {
      }

      break;

    case 'O': //ZZDO

      // set/read waterfall high
      if (command[4] == ';') {
        sprintf(reply, "ZZDO%+4d;", receiver[0]->waterfall_high);
        send_resp(client->fd, reply) ;
      } else {
      }

      break;

    case 'P': //ZZDP

      // set/read panadapter high
      if (command[4] == ';') {
        sprintf(reply, "ZZDP%+4d;", receiver[0]->panadapter_high);
        send_resp(client->fd, reply) ;
      } else {
      }

      break;

    case 'Q': //ZZDQ

      // set/read panadapter low
      if (command[4] == ';') {
        sprintf(reply, "ZZDQ%+4d;", receiver[0]->panadapter_low);
        send_resp(client->fd, reply) ;
      } else {
      }

      break;

    case 'R': //ZZDR

      // set/read panadapter step
      if (command[4] == ';') {
        sprintf(reply, "ZZDR%2d;", receiver[0]->panadapter_step);
        send_resp(client->fd, reply) ;
      } else {
      }

      break;

    case 'U': //ZZDU
      implemented = FALSE;
      break;

    case 'X': //ZZDX
      implemented = FALSE;
      break;

    case 'Y': //ZZDY
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'E': //ZZEx
    switch (command[3]) {
    case 'A': //ZZEA

      // set/read rx equalizer values
      if (command[4] == ';') {
        sprintf(reply, "ZZEA%03d%03d%03d%03d%03d00000000000000000000;", 3, rx_equalizer[0], rx_equalizer[1], rx_equalizer[2],
                rx_equalizer[3]);
        send_resp(client->fd, reply) ;
      } else if (command[37] == ';') {
        char temp[4];
        temp[3] = '\0';
        strncpy(temp, &command[4], 3);
        int bands = atoi(temp);

        if (bands == 3) {
          strncpy(temp, &command[7], 3);
          rx_equalizer[0] = atoi(temp);
          strncpy(temp, &command[10], 3);
          rx_equalizer[1] = atoi(temp);
          strncpy(temp, &command[13], 3);
          rx_equalizer[2] = atoi(temp);
        } else {
        }
      } else {
      }

      break;

    case 'B': //ZZEB

      // set/read tx equalizer values
      if (command[4] == ';') {
        sprintf(reply, "ZZEB%03d%03d%03d%03d%03d00000000000000000000;", 3, tx_equalizer[0], tx_equalizer[1], tx_equalizer[2],
                tx_equalizer[3]);
        send_resp(client->fd, reply) ;
      } else if (command[37] == ';') {
        char temp[4];
        temp[3] = '\0';
        strncpy(temp, &command[4], 3);
        int bands = atoi(temp);

        if (bands == 3) {
          strncpy(temp, &command[7], 3);
          tx_equalizer[0] = atoi(temp);
          strncpy(temp, &command[10], 3);
          tx_equalizer[1] = atoi(temp);
          strncpy(temp, &command[13], 3);
          tx_equalizer[2] = atoi(temp);
        } else {
        }
      } else {
      }

      break;

    case 'M': //ZZEM
      implemented = FALSE;
      break;

    case 'R': //ZZER

      // set/read rx equalizer
      if (command[4] == ';') {
        sprintf(reply, "ZZER%d;", enable_rx_equalizer);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        enable_rx_equalizer = atoi(&command[4]);
      } else {
      }

      break;

    case 'T': //ZZET

      // set/read tx equalizer
      if (command[4] == ';') {
        sprintf(reply, "ZZET%d;", enable_tx_equalizer);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        enable_tx_equalizer = atoi(&command[4]);
      } else {
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'F': //ZZFx
    switch (command[3]) {
    case 'A': //ZZFA

      // set/read VFO-A frequency
      if (command[4] == ';') {
        if (vfo[VFO_A].ctun) {
          sprintf(reply, "ZZFA%011lld;", vfo[VFO_A].ctun_frequency);
        } else {
          sprintf(reply, "ZZFA%011lld;", vfo[VFO_A].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[15] == ';') {
        long long f = atoll(&command[4]);
        vfo_set_frequency(VFO_A, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'B': //ZZFB

      // set/read VFO-B frequency
      if (command[4] == ';') {
        if (vfo[VFO_B].ctun) {
          sprintf(reply, "ZZFB%011lld;", vfo[VFO_B].ctun_frequency);
        } else {
          sprintf(reply, "ZZFB%011lld;", vfo[VFO_B].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[15] == ';') {
        long long f = atoll(&command[4]);
        vfo_set_frequency(VFO_B, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'D': //ZZFD

      // set/read deviation
      if (command[4] == ';') {
        sprintf(reply, "ZZFD%d;", vfo[VFO_A].deviation == 2500 ? 0 : 1);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int d = atoi(&command[4]);
        // TODO: should we check for the mode being FMN?
        vfo[VFO_A].deviation = d ? 5000 : 2500;
        set_filter(receiver[0]);

        if (can_transmit) {
          tx_set_filter(transmitter);
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'H': //ZZFH

      // set/read RX1 filter high
      if (command[4] == ';') {
        sprintf(reply, "ZZFH%05d;", receiver[0]->filter_high);
        send_resp(client->fd, reply) ;
      } else if (command[9] == ';') {
        int fh = atoi(&command[4]);
        fh = fmin(9999, fh);
        fh = fmax(-9999, fh);

        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        filter->high = fh;
        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'I': //ZZFI

      // set/read RX1 DSP receive filter
      if (command[4] == ';') {
        sprintf(reply, "ZZFI%02d;", vfo[VFO_A].filter);
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';') {
        int filter = atoi(&command[4]);
        // update RX1 filter
        vfo_id_filter_changed(VFO_A, filter);
      }

      break;

    case 'J': //ZZFJ

      // set/read RX2 DSP receive filter
      if (command[4] == ';') {
        sprintf(reply, "ZZFJ%02d;", vfo[VFO_B].filter);
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';') {
        // update RX2 filter
        // int filter=atoi(&command[4]);
      }

      break;

    case 'L': //ZZFL

      // set/read RX1 filter low
      if (command[4] == ';') {
        sprintf(reply, "ZZFL%05d;", receiver[0]->filter_low);
        send_resp(client->fd, reply) ;
      } else if (command[9] == ';') {
        int fl = atoi(&command[4]);
        fl = fmin(9999, fl);
        fl = fmax(-9999, fl);

        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        filter->low = fl;
        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //ZZFM
      implemented = FALSE;
      break;

    case 'R': //ZZFR
      implemented = FALSE;
      break;

    case 'S': //ZZFS
      implemented = FALSE;
      break;

    case 'V': //ZZFV
      implemented = FALSE;
      break;

    case 'W': //ZZFW
      implemented = FALSE;
      break;

    case 'X': //ZZFX
      implemented = FALSE;
      break;

    case 'Y': //ZZFY
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'G': //ZZGx
    switch (command[3]) {
    case 'E': //ZZGE
      implemented = FALSE;
      break;

    case 'L': //ZZGL
      implemented = FALSE;
      break;

    case 'T': //ZZGT

      // set/read RX1 AGC
      if (command[4] == ';') {
        sprintf(reply, "ZZGT%d;", receiver[0]->agc);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int agc = atoi(&command[4]);
        // update RX1 AGC
        receiver[0]->agc = agc;
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'U': //ZZGU

      // set/read RX2 AGC
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZGU%d;", receiver[1]->agc);
          send_resp(client->fd, reply) ;
        } else if (command[5] == ';') {
          int agc = atoi(&command[4]);
          // update RX2 AGC
          receiver[1]->agc = agc;
          g_idle_add(ext_vfo_update, NULL);
        }
      } else {
        implemented = FALSE;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'H': //ZZHx
    switch (command[3]) {
    case 'A': //ZZHA
      implemented = FALSE;
      break;

    case 'R': //ZZHR
      implemented = FALSE;
      break;

    case 'T': //ZZHT
      implemented = FALSE;
      break;

    case 'U': //ZZHU
      implemented = FALSE;
      break;

    case 'V': //ZZHV
      implemented = FALSE;
      break;

    case 'W': //ZZHW
      implemented = FALSE;
      break;

    case 'X': //ZZHX
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'I': //ZZIx
    switch (command[3]) {
    case 'D': //ZZID
      strcpy(reply, "ZZID240;");
      send_resp(client->fd, reply) ;
      break;

    case 'F': //ZZIF
      implemented = FALSE;
      break;

    case 'O': //ZZIO
      implemented = FALSE;
      break;

    case 'S': //ZZIS
      implemented = FALSE;
      break;

    case 'T': //ZZIT
      implemented = FALSE;
      break;

    case 'U': //ZZIU
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'K': //ZZKx
    switch (command[3]) {
    case 'M': //ZZIM
      implemented = FALSE;
      break;

    case 'O': //ZZIO
      implemented = FALSE;
      break;

    case 'S': //ZZIS
      implemented = FALSE;
      break;

    case 'Y': //ZZIY
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'L': //ZZLx
    switch (command[3]) {
    case 'A': //ZZLA

      // read/set RX0 gain
      if (command[4] == ';') {
        // send reply back
        sprintf(reply, "ZZLA%03d;", (int)(receiver[0]->volume * 100.0));
        send_resp(client->fd, reply) ;
      } else {
        int gain = atoi(&command[4]);
        receiver[0]->volume = (double)gain / 100.0;
        update_af_gain();
      }

      break;

    case 'B': //ZZLB
      implemented = FALSE;
      break;

    case 'C': //ZZLC

      // read/set RX1 gain
      if (receivers == 2) {
        if (command[4] == ';') {
          // send reply back
          sprintf(reply, "ZZLC%03d;", (int)(receiver[1]->volume * 100.0));
          send_resp(client->fd, reply) ;
        } else {
          int gain = atoi(&command[4]);
          receiver[1]->volume = (double)gain / 100.0;
          update_af_gain();
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'D': //ZZLD
      implemented = FALSE;
      break;

    case 'E': //ZZLE
      implemented = FALSE;
      break;

    case 'F': //ZZLF
      implemented = FALSE;
      break;

    case 'G': //ZZLG
      implemented = FALSE;
      break;

    case 'H': //ZZLH
      implemented = FALSE;
      break;

    case 'I': //ZZLI
      if (can_transmit) {
        if (command[4] == ';') {
          // send reply back
          sprintf(reply, "ZZLI%d;", transmitter->puresignal);
          send_resp(client->fd, reply) ;
        } else {
          int ps = atoi(&command[4]);
          transmitter->puresignal = ps;
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'M': //ZZMx
    switch (command[3]) {
    case 'A': { //ZZMA
      int mute = atoi(&command[4]);
      receiver[0]->mute_radio = mute;
    }
    break;

    case 'B': //ZZMB
      implemented = FALSE;
      break;

    case 'D': //ZZMD

      // set/read RX1 operating mode
      if (command[4] == ';') {
        sprintf(reply, "ZZMD%02d;", vfo[VFO_A].mode);
        send_resp(client->fd, reply);
      } else if (command[6] == ';') {
        vfo_id_mode_changed(VFO_A, atoi(&command[4]));
      }

      break;

    case 'E': //ZZME

      // set/read RX2 operating mode
      if (command[4] == ';') {
        sprintf(reply, "ZZMD%02d;", vfo[VFO_B].mode);
        send_resp(client->fd, reply);
      } else if (command[6] == ';') {
        vfo_id_mode_changed(VFO_A, atoi(&command[4]));
      }

      break;

    case 'G': //ZZMG

      // set/read mic gain
      if (command[4] == ';') {
        sprintf(reply, "ZZMG%03d;", (int)mic_gain);
        send_resp(client->fd, reply);
      } else if (command[7] == ';') {
        mic_gain = (double)atoi(&command[4]);
      }

      break;

    case 'L': //ZZML

      // read DSP modes and indexes
      if (command[4] == ';') {
        sprintf(reply, "ZZML LSB00: USB01: DSB02: CWL03: CWU04: FMN05:  AM06:DIGU07:SPEC08:DIGL09: SAM10: DRM11;");
        send_resp(client->fd, reply);
      }

      break;

    case 'N': //ZZMN

      // read Filter Names and indexes
      if (command[6] == ';') {
        int mode = atoi(&command[4]) - 1;
        FILTER *f = filters[mode];
        sprintf(reply, "ZZMN");
        char temp[32];

        for (int i = 0; i < FILTERS; i++) {
          sprintf(temp, "%5s%5d%5d", f[i].title, f[i].high, f[i].low);
          strcat(reply, temp);
        }

        strcat(reply, ";");
        send_resp(client->fd, reply);
      }

      break;

    case 'O': //ZZMO

      // set/read MON status
      if (command[4] == ';') {
        sprintf(reply, "ZZMO%d;", 0);
        send_resp(client->fd, reply);
      }

      break;

    case 'R': //ZZMR

      // set/read RX Meter mode
      if (command[4] == ';') {
        sprintf(reply, "ZZMR%d;", smeter + 1);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        smeter = atoi(&command[4]) - 1;
      }

      break;

    case 'S': //ZZMS
      implemented = FALSE;
      break;

    case 'T': //ZZMT
      if (command[4] == ';') {
        sprintf(reply, "ZZMT%02d;", 1); // forward power
        send_resp(client->fd, reply);
      } else {
      }

      break;

    case 'U': //ZZMU
      implemented = FALSE;
      break;

    case 'V': //ZZMV
      implemented = FALSE;
      break;

    case 'W': //ZZMW
      implemented = FALSE;
      break;

    case 'X': //ZZMX
      implemented = FALSE;
      break;

    case 'Y': //ZZMY
      implemented = FALSE;
      break;

    case 'Z': //ZZMZ
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'N': //ZZNx
    switch (command[3]) {
    case 'A': //ZZNA

      // set/read RX1 NB1
      if (command[4] == ';') {
        sprintf(reply, "ZZNA%d;", (receiver[0]->nb == 1));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->nb = 1; }

        update_noise();
      }

      break;

    case 'B': //ZZNB

      // set/read RX1 NB2
      if (command[4] == ';') {
        sprintf(reply, "ZZNB%d;", (receiver[0]->nb == 2));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->nb = 2; }

        update_noise();
      }

      break;

    case 'C': //ZZNC

      // set/read RX2 NB1
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZNC%d;", (receiver[1]->nb == 1));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nb = 1; }

          update_noise();
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'D': //ZZND

      // set/read RX2 NB2
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZND%d;", (receiver[1]->nb == 2));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nb = 2; }

          update_noise();
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'L': //ZZNL
      // set/read NB1 threshold
      implemented = FALSE;
      break;

    case 'M': //ZZNM
      // set/read NB2 threshold
      implemented = FALSE;
      break;

    case 'N': //ZZNN

      // set/read RX1 SNB status
      if (command[4] == ';') {
        sprintf(reply, "ZZNN%d;", receiver[0]->snb);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        receiver[0]->snb = atoi(&command[4]);
        update_noise();
      }

      break;

    case 'O': //ZZNO

      // set/read RX2 SNB status
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZNO%d;", receiver[1]->snb);
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          receiver[1]->snb = atoi(&command[4]);
          update_noise();
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'R': //ZZNR

      // set/read RX1 NR
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZNR%d;", (receiver[0]->nr == 1));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[0]->nr = 1; }

          update_noise();
        }
      }

      break;

    case 'S': //ZZNS

      // set/read RX1 NR2
      if (command[4] == ';') {
        sprintf(reply, "ZZNS%d;", (receiver[0]->nr == 2));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->nr = 2; }

        update_noise();
      }

      break;

    case 'T': //ZZNT

      // set/read RX1 ANF
      if (command[4] == ';') {
        sprintf(reply, "ZZNT%d;", receiver[0]->anf);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->anf = 1; }

        update_noise();
      }

      break;

    case 'U': //ZZNU

      // set/read RX2 ANF
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZNU%d;", receiver[1]->anf);
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->anf = 1; }

          update_noise();
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'V': //ZZNV

      // set/read RX2 NR
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZNV%d;", (receiver[1]->nr == 1));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nr = 1; }

          update_noise();
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'W': //ZZNW

      // set/read RX2 NR2
      if (receivers == 2) {
        if (command[4] == ';') {
          sprintf(reply, "ZZNW%d;", (receiver[1]->nr == 2));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nr = 2; }

          update_noise();
        }
      } else {
        implemented = FALSE;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'O': //ZZOx
    switch (command[3]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'P': //ZZPx
    switch (command[3]) {
    case 'A': //ZZPA

      // set/read preamp setting
      if (command[4] == ';') {
        int a = adc[receiver[0]->adc].attenuation;

        if (a == 0) {
          a = 1;
        } else if (a <= -30) {
          a = 4;
        } else if (a <= -20) {
          a = 0;
        } else if (a <= -10) {
          a = 2;
        } else {
          a = 3;
        }

        sprintf(reply, "ZZPA%d;", a);
        send_resp(client->fd, reply);
      } else if (command[5] == ';' && have_rx_att) {
        int a = atoi(&command[4]);

        switch (a) {
        case 0:
          adc[receiver[0]->adc].attenuation = -20;
          break;

        case 1:
          adc[receiver[0]->adc].attenuation = 0;
          break;

        case 2:
          adc[receiver[0]->adc].attenuation = -10;
          break;

        case 3:
          adc[receiver[0]->adc].attenuation = -20;
          break;

        case 4:
          adc[receiver[0]->adc].attenuation = -30;
          break;

        default:
          adc[receiver[0]->adc].attenuation = 0;
          break;
        }
      }

      break;

    case 'Y': // ZZPY

      // set/read Zoom
      if (command[4] == ';') {
        sprintf(reply, "ZZPY%d;", receiver[1]->zoom);
        send_resp(client->fd, reply);
      } else if (command[7] == ';') {
        int zoom = atoi(&command[4]);
        set_zoom(1, zoom);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Q': //ZZQx
    switch (command[3]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'R': //ZZRx
    switch (command[3]) {
    case 'C': //ZZRC

      // clear RIT frequency
      if (command[4] == ';') {
        schedule_action(RIT_CLEAR, PRESSED, 0);
      }

      break;

    case 'D': //ZZRD
      if (command[4] == ';') {
        // decrement RIT frequency
        if (vfo[VFO_A].mode == modeCWL || vfo[VFO_A].mode == modeCWU) {
          vfo_rit_incr(VFO_A, -10);
        } else {
          vfo_rit_incr(VFO_A, -rit_increment);
        }
      } else if (command[9] == ';') {
        // set RIT frequency
        vfo_rit_value(VFO_A, atoi(&command[4]));
      }

      break;

    case 'F': //ZZRF

      // set/read RIT frequency
      if (command[4] == ';') {
        sprintf(reply, "ZZRF%+5lld;", vfo[VFO_A].rit);
        send_resp(client->fd, reply);
      } else if (command[9] == ';') {
        vfo_rit_value(VFO_A, atoi(&command[4]));
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //ZZRM

      // read meter value. Ignore P1 value.
      if (command[5] == ';') {
        sprintf(reply, "ZZRM%d%20d;", smeter, (int)receiver[0]->meter);
        send_resp(client->fd, reply);
      }

      break;

    case 'S': //ZZRS

      // set/read RX2 enable
      if (command[4] == ';') {
        sprintf(reply, "ZZRS%d;", receivers == 2);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        int state = atoi(&command[4]);

        if (state) {
          radio_change_receivers(2);
        } else {
          radio_change_receivers(1);
        }
      }

      break;

    case 'T': //ZZRT

      // set/read RIT enable
      if (command[4] == ';') {
        sprintf(reply, "ZZRT%d;", vfo[VFO_A].rit_enabled);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        vfo_rit_onoff(VFO_A, SET(atoi(&command[4])));
      }

      break;

    case 'U': //ZZRU

      // increments RIT Frequency
      if (command[4] == ';') {
        if (vfo[VFO_A].mode == modeCWL || vfo[VFO_A].mode == modeCWU) {
          vfo_rit_incr(VFO_A, 10);
        } else {
          vfo_rit_incr(VFO_A, rit_increment);
        }
      } else if (command[9] == ';') {
        vfo_rit_value(VFO_A,  atoi(&command[4]));
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'S': //ZZSx
    switch (command[3]) {
    case 'A': //ZZSA

      // move VFO A down one step
      if (command[4] == ';') {
        vfo_id_step(VFO_A, -1);
      }

      break;

    case 'B': //ZZSB

      // move VFO A up one step
      if (command[4] == ';') {
        vfo_id_step(VFO_A, 1);
      }

      break;

    case 'D': //ZZSD
      implemented = FALSE;
      break;

    case 'F': //ZZSF
      implemented = FALSE;
      break;

    case 'G': //ZZSG

      // move VFO B down 1 step
      if (command[4] == ';') {
        vfo_id_step(VFO_B, -1);
      }

      break;

    case 'H': //ZZSH

      // move VFO B up 1 step
      if (command[4] == ';') {
        vfo_id_step(VFO_B, 1);
      }

      break;

    case 'M': //ZZSM

      // reads the S Meter (in dB)
      if (command[5] == ';') {
        int v = atoi(&command[4]);

        if (v >= 0 && v < receivers) {
          double m = receiver[v]->meter;
          m = fmax(-140.0, m);
          m = fmin(-10.0, m);
          sprintf(reply, "ZZSM%d%03d;", v, (int)((m + 140.0) * 2));
          send_resp(client->fd, reply);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'N': //ZZSN
      implemented = FALSE;
      break;

    case 'P': //ZZSP

      // set/read split
      if (command[4] == ';') {
        sprintf(reply, "ZZSP%d;", split);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int val = atoi(&command[4]);
        radio_set_split(val);
      }

      break;

    case 'R': //ZZSR
      implemented = FALSE;
      break;

    case 'S': //ZZSS
      implemented = FALSE;
      break;

    case 'T': //ZZST
      implemented = FALSE;
      break;

    case 'U': //ZZSU
      implemented = FALSE;
      break;

    case 'V': //ZZSV
      implemented = FALSE;
      break;

    case 'W': //ZZSW

      // set/read split
      if (command[4] == ';') {
        sprintf(reply, "ZZSW%d;", split);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int val = atoi(&command[4]);
        radio_set_split(val);
      }

      break;

    case 'Y': //ZZSY
      implemented = FALSE;
      break;

    case 'Z': //ZZSZ
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'T': //ZZTx
    switch (command[3]) {
    case 'A': //ZZTA
      implemented = FALSE;
      break;

    case 'B': //ZZTB
      implemented = FALSE;
      break;

    case 'F': //ZZTF
      implemented = FALSE;
      break;

    case 'H': //ZZTH
      implemented = FALSE;
      break;

    case 'I': //ZZTI
      implemented = FALSE;
      break;

    case 'L': //ZZTL
      implemented = FALSE;
      break;

    case 'M': //ZZTM
      implemented = FALSE;
      break;

    case 'O': //ZZTO
      implemented = FALSE;
      break;

    case 'P': //ZZTP
      implemented = FALSE;
      break;

    case 'S': //ZZTS
      implemented = FALSE;
      break;

    case 'U': //ZZTU

      // sets or reads TUN status
      if (command[4] == ';') {
        sprintf(reply, "ZZTU%d;", tune);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        tune_update(atoi(&command[4]));
      }

      break;

    case 'V': //ZZTV
      implemented = FALSE;
      break;

    case 'X': //ZZTX

      // sets or reads MOX status
      if (command[4] == ';') {
        sprintf(reply, "ZZTX%d;", mox);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        mox_update(atoi(&command[4]));
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'U': //ZZUx
    switch (command[3]) {
    case 'A': //ZZUA
      implemented = FALSE;
      break;

    case 'S': //ZZUS
      implemented = FALSE;
      break;

    case 'T': //ZZUT
      implemented = FALSE;
      break;

    case 'X': //ZZUX
      implemented = FALSE;
      break;

    case 'Y': //ZZUY
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'V': //ZZVx
    switch (command[3]) {
    case 'A': //ZZVA
      implemented = FALSE;
      break;

    case 'B': //ZZVB
      implemented = FALSE;
      break;

    case 'C': //ZZVC
      implemented = FALSE;
      break;

    case 'D': //ZZVD
      implemented = FALSE;
      break;

    case 'E': //ZZVE
      implemented = FALSE;
      break;

    case 'F': //ZZVF
      implemented = FALSE;
      break;

    case 'H': //ZZVH
      implemented = FALSE;
      break;

    case 'I': //ZZVI
      implemented = FALSE;
      break;

    case 'J': //ZZVJ
      implemented = FALSE;
      break;

    case 'K': //ZZVK
      implemented = FALSE;
      break;

    case 'L': //ZZVL
      // set/get VFO Lock
      locked = command[4] == '1';
      g_idle_add(ext_vfo_update, NULL);
      break;

    case 'M': //ZZVM
      implemented = FALSE;
      break;

    case 'N': //ZZVN
      implemented = FALSE;
      break;

    case 'O': //ZZVO
      implemented = FALSE;
      break;

    case 'P': //ZZVP
      implemented = FALSE;
      break;

    case 'Q': //ZZVQ
      implemented = FALSE;
      break;

    case 'R': //ZZVR
      implemented = FALSE;
      break;

    case 'S': { //ZZVS
      int i = atoi(&command[4]);

      if (i == 0) {
        vfo_a_to_b();
      } else if (i == 1) {
        vfo_b_to_a();
      } else {
        vfo_a_swap_b();
      }
    }
    break;

    case 'T': //ZZVT
      implemented = FALSE;
      break;

    case 'U': //ZZVU
      implemented = FALSE;
      break;

    case 'V': //ZZVV
      implemented = FALSE;
      break;

    case 'W': //ZZVW
      implemented = FALSE;
      break;

    case 'X': //ZZVX
      implemented = FALSE;
      break;

    case 'Y': //ZZVY
      implemented = FALSE;
      break;

    case 'Z': //ZZVZ
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'W': //ZZWx
    switch (command[3]) {
    case 'A': //ZZWA
      implemented = FALSE;
      break;

    case 'B': //ZZWB
      implemented = FALSE;
      break;

    case 'C': //ZZWC
      implemented = FALSE;
      break;

    case 'D': //ZZWD
      implemented = FALSE;
      break;

    case 'E': //ZZWE
      implemented = FALSE;
      break;

    case 'F': //ZZWF
      implemented = FALSE;
      break;

    case 'G': //ZZWG
      implemented = FALSE;
      break;

    case 'H': //ZZWH
      implemented = FALSE;
      break;

    case 'J': //ZZWJ
      implemented = FALSE;
      break;

    case 'K': //ZZWK
      implemented = FALSE;
      break;

    case 'L': //ZZWL
      implemented = FALSE;
      break;

    case 'M': //ZZWM
      implemented = FALSE;
      break;

    case 'N': //ZZWN
      implemented = FALSE;
      break;

    case 'O': //ZZWO
      implemented = FALSE;
      break;

    case 'P': //ZZWP
      implemented = FALSE;
      break;

    case 'Q': //ZZWQ
      implemented = FALSE;
      break;

    case 'R': //ZZWR
      implemented = FALSE;
      break;

    case 'S': //ZZWS
      implemented = FALSE;
      break;

    case 'T': //ZZWT
      implemented = FALSE;
      break;

    case 'U': //ZZWU
      implemented = FALSE;
      break;

    case 'V': //ZZWV
      implemented = FALSE;
      break;

    case 'W': //ZZWW
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'X': //ZZXx
    switch (command[3]) {
    case 'C': //ZZXC
      // clear transmitter XIT
      schedule_action(XIT_CLEAR, PRESSED, 0);
      break;

    case 'F': //ZZXF

      // set/read XIT
      if (command[4] == ';') {
        sprintf(reply, "ZZXT%+05lld;", vfo[get_tx_vfo()].xit);
        send_resp(client->fd, reply) ;
      } else if (command[9] == ';') {
        vfo_xit_value(atoi(&command[4]));
      }

      break;

    case 'H': //ZZXH
      implemented = FALSE;
      break;

    case 'N': //ZZXN

      // read combined RX1 status
      if (command[4] == ';') {
        int status = ((receiver[0]->agc) & 0x03);
        int a = adc[receiver[0]->adc].attenuation;

        if (a == 0) {
          a = 1;
        } else if (a <= -30) {
          a = 4;
        } else if (a <= -20) {
          a = 0;
        } else if (a <= -10) {
          a = 2;
        } else {
          a = 3;
        }

        status = status | ((a & 0x03) << 3);

        if (receiver[0]->squelch_enable) { status |=  0x0040; }

        if (receiver[0]->nb == 1) { status |=  0x0080; }

        if (receiver[0]->nb == 2) { status |=  0x0100; }

        if (receiver[0]->nr == 1) { status |=  0x0200; }

        if (receiver[0]->nr == 2) { status |=  0x0400; }

        if (receiver[0]->snb) { status |=  0x0800; }

        if (receiver[0]->anf) { status |=  0x1000; }

        sprintf(reply, "ZZXN%04d;", status);
        send_resp(client->fd, reply);
      }

      break;

    case 'O': //ZZXO

      // read combined RX2 status
      if (receivers == 2) {
        if (command[4] == ';') {
          int status = ((receiver[1]->agc) & 0x03);
          int a = adc[receiver[1]->adc].attenuation;

          if (a == 0) {
            a = 1;
          } else if (a <= -30) {
            a = 4;
          } else if (a <= -20) {
            a = 0;
          } else if (a <= -10) {
            a = 2;
          } else {
            a = 3;
          }

          status = status | ((a & 0x03) << 3);

          if (receiver[1]->squelch_enable) { status |=  0x0040; }

          if (receiver[1]->nb == 1) { status |=  0x0080; }

          if (receiver[1]->nb == 2) { status |=  0x0100; }

          if (receiver[1]->nr == 1) { status |=  0x0200; }

          if (receiver[1]->nr == 2) { status |=  0x0400; }

          if (receiver[1]->snb) { status |=  0x0800; }

          if (receiver[1]->anf) { status |=  0x1000; }

          sprintf(reply, "ZZXO%04d;", status);
          send_resp(client->fd, reply);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'S': //ZZXS

      /// set/read XIT enable
      if (command[4] == ';') {
        sprintf(reply, "ZZXS%d;", vfo[get_tx_vfo()].xit_enabled);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        vfo[get_tx_vfo()].xit_enabled = atoi(&command[4]);

        if (protocol == NEW_PROTOCOL) {
          schedule_high_priority();
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'T': //ZZXT
      implemented = FALSE;
      break;

    case 'V': //ZZXV

      // read combined VFO status
      if (command[4] == ';') {
        int status = 0;

        if (vfo[VFO_A].rit_enabled) {
          // cppcheck-suppress badBitmaskCheck
          status = status | 0x01;
        }

        if (locked) {
          status = status | 0x02;
          status = status | 0x04;
        }

        if (split) {
          status = status | 0x08;
        }

        if (vfo[VFO_A].ctun) {
          status = status | 0x10;
        }

        if (vfo[VFO_B].ctun) {
          status = status | 0x20;
        }

        if (mox) {
          status = status | 0x40;
        }

        if (tune) {
          status = status | 0x80;
        }

        sprintf(reply, "ZZXV%03d;", status);
        send_resp(client->fd, reply);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Y': //ZZYx
    switch (command[3]) {
    case 'A': //ZZYA
      implemented = FALSE;
      break;

    case 'B': //ZZYB
      implemented = FALSE;
      break;

    case 'C': //ZZYC
      implemented = FALSE;
      break;

    case 'R': //ZZYR

      // switch receivers
      if (command[5] == ';') {
        int v = atoi(&command[4]);

        if (v >= 0 && v < receivers) {
          schedule_action(v == 0 ? RX1 : RX2, PRESSED, 0);
        } else {
          implemented = FALSE;
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Z': //ZZZx
    switch (command[3]) {
    case 'A': //ZZZA
      implemented = FALSE;
      break;

    case 'B': //ZZZB
      implemented = FALSE;
      break;

    case 'D': //ZZZD  ANDROMEDA command applied to VFO of active receiver

      // move VFO down
      if (command[6] == ';') {
        static int steps = 0;
        steps += atoi(&command[4]);

        if (steps >= vfo_encoder_divisor) {
          vfo_id_step((active_receiver->id == 0) ? VFO_A : VFO_B, -steps / vfo_encoder_divisor);
          steps = 0;
        }
      }

      break;

    case 'E': //ZZZE ANDROMEDA commmand

      // Encoders
      if (command[7] == ';') {
        int v, p;

        if ((command[4] - 0x30) < 2) {
          p = (command[4] - 0x2b) * 10;
          v = 0;
        } else {
          p = (command[4] - 0x30) * 10;
          v = 1;
        }

        p += (command[5] - 0x30);

        if (!locked) switch (p) {
          case 51: // RX1 AF Gain
            schedule_action(AF_GAIN_RX1, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 52: // RX1 RF (better: AGC) Gain
            schedule_action(AGC_GAIN_RX1, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 53: // RX2 AF Gain
            schedule_action(AF_GAIN_RX2, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 54: // RX2 RF (better: AGC) Gain
            schedule_action(AGC_GAIN_RX2, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 55: // Filter Cut High
            schedule_action(FILTER_CUT_HIGH, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 56: // Filter Cut Low
            schedule_action(FILTER_CUT_LOW, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 57: // Diversity Gain
            if (diversity_enabled) { schedule_action(DIV_GAIN, RELATIVE, (v == 0) ? 1 : -1); }

            break;

          case 58: // Diversity Phase
            if (diversity_enabled) { schedule_action(DIV_PHASE, RELATIVE, (v == 0) ? 1 : -1); }

            break;

          case 59: // RIT of the VFO of the active receiver
            if (vfo[active_receiver->id].rit_enabled) {
              // cannot use schedule_action because we inspect rit_enabled immediately,
              // but the scheduled action may be deferred
              vfo_rit_incr(active_receiver->id, (v == 0) ? rit_increment : -rit_increment);

              if (!vfo[active_receiver->id].rit_enabled) {
                sprintf(reply, "ZZZI080;");
                send_resp(client->fd, reply);
              }
            }

            break;

          case 60: // XIT
            if (vfo[get_tx_vfo()].xit_enabled) {
              vfo_xit_incr((v == 0) ? rit_increment : -rit_increment);

              if (!vfo[get_tx_vfo()].xit_enabled) {
                sprintf(reply, "ZZZI090;");
                send_resp(client->fd, reply);
              }
            }

            break;

          case 61: // Mic Gain
            schedule_action(MIC_GAIN, RELATIVE, (v == 0) ? 1 : -1);
            break;

          case 62: // Drive
            schedule_action(DRIVE, RELATIVE, (v == 0) ? 1 : -1);
            break;
          }
      }

      break;

    case 'P': //ZZZP ANDROMEDA command

      // Push Buttons
      if (command[7] == ';') {
        static int numpad_active = 0;
        static int longpress = 0;
        int v = (command[6] - 0x30);
        int p = (command[4] - 0x30) * 10;
        p += command[5] - 0x30;

        if (!numpad_active) switch (p) {
          case 21: // Function Switches
          case 22:
          case 23:
          case 24:
          case 25:
          case 26:
          case 27:
          case 28:
            schedule_action(toolbar_switches[p - 21].switch_function, (v == 0) ? PRESSED : RELEASED, 0);
            sprintf(reply, "ZZZI11%d;", locked);
            send_resp(client->fd, reply);
            break;

          case 46: // SDR On
            if (v == 0) {
              if (longpress) {
                longpress = 0;
              } else {
                static int startstop = 1;
                startstop ^= 1;
                startstop ? protocol_run() : protocol_stop();
              }
            } else if (v == 2) {
              new_menu();
              longpress = 1;
            }

            break;
          }

        if (numpad_active && v == 0) switch (p) {
          case 30: // Band Buttons
            schedule_action(NUMPAD_1, PRESSED, 0);
            break;

          case 31:
            schedule_action(NUMPAD_2, PRESSED, 0);
            break;

          case 32:
            schedule_action(NUMPAD_3, PRESSED, 0);
            break;

          case 33:
            schedule_action(NUMPAD_4, PRESSED, 0);
            break;

          case 34:
            schedule_action(NUMPAD_5, PRESSED, 0);
            break;

          case 35:
            schedule_action(NUMPAD_6, PRESSED, 0);
            break;

          case 36:
            schedule_action(NUMPAD_7, PRESSED, 0);
            break;

          case 37:
            schedule_action(NUMPAD_8, PRESSED, 0);
            break;

          case 38:
            schedule_action(NUMPAD_9, PRESSED, 0);
            break;

          case 39:
            schedule_action(NUMPAD_DEC, PRESSED, 0);
            break;

          case 40:
            schedule_action(NUMPAD_0, PRESSED, 0);
            break;

          case 41: {
            schedule_action(NUMPAD_ENTER, PRESSED, 0);
            numpad_active = 0;
            locked = 0;
          }
          break;

          case 45: {
            schedule_action(NUMPAD_MHZ, PRESSED, 0);
            numpad_active = 0;
            locked = 0;
          }
          } else if (!locked) switch (p) {
            static int shift = 0;

          case 1: // Rx1 AF Mute
            if (v == 0) { receiver[0]->mute_radio ^= 1; }

            break;

          case 3: // Rx2 AF Mute
            if (v == 0) { receiver[1]->mute_radio ^= 1; }

            break;

          case 5: // Filter Cut Defaults
            schedule_action(FILTER_CUT_DEFAULT, (v == 0) ? PRESSED : RELEASED, 0);
            break;

          case 7: // Diversity Enable
            if (RECEIVERS == 2 && n_adc > 1) {
              schedule_action(DIV, (v == 0) ? PRESSED : RELEASED, 0);

              if (v == 0) {
                sprintf(reply, "ZZZI05%d;", diversity_enabled ^ 1);
                send_resp(client->fd, reply);
              }
            }

            break;

          case 9: // RIT/XIT Clear
            schedule_action(RIT_CLEAR, (v == 0) ? PRESSED : RELEASED, 0);
            schedule_action(XIT_CLEAR, (v == 0) ? PRESSED : RELEASED, 0);
            sprintf(reply, "ZZZI080;");
            send_resp(client->fd, reply);
            sprintf(reply, "ZZZI090;");
            send_resp(client->fd, reply);
            break;

          case 29: // Shift
            if (v == 0) {
              shift ^= 1;
              sprintf(reply, "ZZZI06%d;", shift);
              send_resp(client->fd, reply);
            }

            break;

          case 30: // Band Buttons
          case 31:
          case 32:
          case 33:
          case 34:
          case 35:
          case 36:
          case 37:
          case 38:
          case 39:
          case 40:
          case 41:
            if (shift && v == 0) {
              int band = band20;

              if (p == 30) { band = band160; }
              else if (p == 31) { band = band80; }
              else if (p == 32) { band = band60; }
              else if (p == 33) { band = band40; }
              else if (p == 34) { band = band30; }
              else if (p == 35) { band = band20; }
              else if (p == 36) { band = band17; }
              else if (p == 37) { band = band15; }
              else if (p == 38) { band = band12; }
              else if (p == 39) { band = band10; }
              else if (p == 40) { band = band6; }
              else if (p == 41) { band = bandGen; }

              vfo_band_changed(active_receiver->id ? VFO_B : VFO_A, band);
              shift = 0;
              sprintf(reply, "ZZZI060;");
              send_resp(client->fd, reply);
            } else if (v == 1) {
              if (p == 30) { start_tx(); }                                  // MODE DATA
              else if (p == 31) { schedule_action(MODE_PLUS, PRESSED, 0); } // MODE+
              else if (p == 32) { schedule_action(FILTER_PLUS, PRESSED, 0); } // FILTER+
              else if (p == 33) { radio_change_receivers(receivers == 1 ? 2 : 1); } // RX2
              else if (p == 34) { schedule_action(MODE_MINUS, PRESSED, 0); } // MODE-
              else if (p == 35) { schedule_action(FILTER_MINUS, PRESSED, 0); } // FILTER-
              else if (p == 36) { schedule_action(A_TO_B, PRESSED, 0); }    // A>B
              else if (p == 37) { schedule_action(B_TO_A, PRESSED, 0); }    // B>A
              else if (p == 38) { schedule_action(SPLIT, PRESSED, 0); }     // SPLIT
              else if (p == 39) { schedule_action(NB, PRESSED, 0); }        // U1 (use NB)
              else if (p == 40) { schedule_action(NR, PRESSED, 0); }        // U2 (use NR)
            } else if (p == 41) {
              if (v == 0 || v == 2) {
                numpad_active = 1;
                locked = 1;
                g_idle_add(ext_vfo_update, NULL);
                schedule_action(NUMPAD_CL, PRESSED, 0);               // U3 start Freq entry
              }
            }

            break;

          case 42: // RIT/XIT
            if (v == 0) {
              if (!vfo[active_receiver->id].rit_enabled && !vfo[get_tx_vfo()].xit_enabled) {
                // neither RIT nor XIT: ==> activate RIT
                vfo_rit_onoff(active_receiver->id, 1);
                sprintf(reply, "ZZZI081;");
                send_resp(client->fd, reply);
              } else if (vfo[active_receiver->id].rit_enabled && !vfo[get_tx_vfo()].xit_enabled) {
                // RIT but no XIT: ==> de-activate RIT and activate XIT
                vfo_rit_onoff(active_receiver->id, 0);
                vfo_xit_onoff(1);
                sprintf(reply, "ZZZI080;");
                send_resp(client->fd, reply);
                sprintf(reply, "ZZZI091;");
                send_resp(client->fd, reply);
              } else {
                // else deactivate both.
                vfo_rit_onoff(active_receiver->id, 0);
                vfo_xit_onoff(0);
                sprintf(reply, "ZZZI080;");
                send_resp(client->fd, reply);
                sprintf(reply, "ZZZI090;");
                send_resp(client->fd, reply);
              }

              g_idle_add(ext_vfo_update, NULL);
            }

            break;

          case 43: // switch receivers
            if (receivers == 2) {
              if (v == 0) {
                if (active_receiver->id == 0) {
                  schedule_action(RX2, PRESSED, 0);
                  sprintf(reply, "ZZZI07%d;", vfo[VFO_B].ctun);
                  send_resp(client->fd, reply);
                  sprintf(reply, "ZZZI08%d;", vfo[VFO_B].rit_enabled);
                  send_resp(client->fd, reply);
                  sprintf(reply, "ZZZI100;");
                } else {
                  schedule_action(RX1, PRESSED, 0);
                  sprintf(reply, "ZZZI07%d;", vfo[VFO_A].ctun);
                  send_resp(client->fd, reply);
                  sprintf(reply, "ZZZI08%d;", vfo[VFO_A].rit_enabled);
                  send_resp(client->fd, reply);
                  sprintf(reply, "ZZZI101;");
                }

                send_resp(client->fd, reply);
                g_idle_add(ext_vfo_update, NULL);
              }
            }

            break;

          case 45: // ctune
            if (v == 1) {
              schedule_action(CTUN, PRESSED, 0);
              sprintf(reply, "ZZZI07%d;", vfo[active_receiver->id].ctun ^ 1);
              send_resp(client->fd, reply);
              g_idle_add(ext_vfo_update, NULL);
            }

            break;

          case 47: // MOX
            if (v == 0) {
              sprintf(reply, "ZZZI01%d;", mox);
              send_resp(client->fd, reply);
            } else {
              mox_update(mox ^ 1);
            }

            break;

          case 48: // TUNE
            if (v == 0) {
              sprintf(reply, "ZZZI03%d;", tune);
              send_resp(client->fd, reply);
            } else {
              tune_update(tune ^ 1);
            }

            break;

          case 50: // TWO TONE
            schedule_action(TWO_TONE, (v == 0) ? PRESSED : RELEASED, 0);
            break;

          case 49: // PS ON
            if (v == 0) {
              if (longpress) {
                longpress = 0;
              } else {
                if (can_transmit) {
                  tx_set_ps(transmitter, transmitter->puresignal ^ 1);
                  sprintf(reply, "ZZZI04%d;", transmitter->puresignal);
                  send_resp(client->fd, reply);
                }
              }
            } else if (v == 2) {
              start_ps();
              longpress = 1;
            }

            break;
          }

        if (p == 44) { // VFO lock
          if (v == 0) {
            if (numpad_active) {
              schedule_action(NUMPAD_KHZ, PRESSED, 0);
              numpad_active = 0;
              locked = 0;
            } else {
              locked ^= 1;
              g_idle_add(ext_vfo_update, NULL);
              sprintf(reply, "ZZZI11%d;", locked);
              send_resp(client->fd, reply);
            }
          }
        }
      }

      break;

    case 'S': //ZZZS ANDROMEDA command

      // ANDROMEDA version info
      if (command[11] == ';') {
        t_print("RIGCTL:INFO: Andromeda FP Version: h/w:%c%c s/w:%c%c%c\n",
                command[6], command[7], command[8], command[9], command[10]);
      }

      break;

    case 'U': //ZZZU ANDROMEDA command operating on VFO of active receiver

      // move VFO up
      if (command[6] == ';') {
        static int steps = 0;
        steps += atoi(&command[4]);

        if (steps >= vfo_encoder_divisor) {
          vfo_id_step((active_receiver->id == 0) ? VFO_A : VFO_B, steps / vfo_encoder_divisor);
          steps = 0;
        }
      }

      break;

    case 'Z': //ZZZZ
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  default:
    implemented = FALSE;
    break;
  }

  return implemented;
}

// called with g_idle_add so that the processing is running on the main thread
int parse_cmd(void *data) {
  COMMAND *info = (COMMAND *)data;
  CLIENT *client = info->client;
  char *command = info->command;
  char reply[80];
  reply[0] = '\0';
  gboolean implemented = TRUE;

  switch (command[0]) {
  case '#':
    if (command[1] == 'S' && command[2] == ';') {
      stop_program();
      (void) system("shutdown -h -P now");
      _exit(0);
    } else {
      implemented = FALSE;
    }

    break;

  case 'A':
    switch (command[1]) {
    case 'C': //AC
      // set/read internal atu status
      implemented = FALSE;
      break;

    case 'G': //AG

      // set/read AF Gain
      if (command[3] == ';' && command[2] == '0') { // query, main receiver
        // send reply back (covert from -40...0dB to 0..255)
        sprintf(reply, "AG0%03d;", (int)(255.0 * pow(10.0, 0.05 * receiver[0]->volume)));
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';' && command[2] == '0') {
        int gain = atoi(&command[3]);
        receiver[0]->volume = 20 * log((double) gain / 255.0);
        update_af_gain();
      }

      break;

    case 'I': //AI

      // set/read Auto Information
      // many clients start the connection with an "AI0" command.
      // piHPSDR is constantly in an "AI0" state, therefore
      // silently ignore AI0 commands and flag an error for
      // all other possiblities
      if (command[2] == '0' && command[3] == ';') {
        // do nothing
      } else {
        implemented = FALSE;
      }

      break;

    case 'L': // AL
      // set/read Auto Notch level
      implemented = FALSE;
      break;

    case 'M': // AM
      // set/read Auto Mode
      implemented = FALSE;
      break;

    case 'N': // AN
      // set/read Antenna Connection
      implemented = FALSE;
      break;

    case 'S': // AS
      // set/read Auto Mode Function Parameters
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'B':
    switch (command[1]) {
    case 'C': //BC
      // set/read Beat Canceller
      implemented = FALSE;
      break;

    case 'D': //BD
      //band down 1 band
      band_minus(receiver[0]->id);
      break;

    case 'P': //BP
      // set/read Manual Beat Canceller frequency
      implemented = FALSE;
      break;

    case 'U': //BU
      //band up 1 band
      band_plus(receiver[0]->id);
      break;

    case 'Y': //BY
      // read busy signal
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'C':
    switch (command[1]) {
    case 'A': //CA
      // set/read CW Auto Tune
      implemented = FALSE;
      break;

    case 'G': //CG
      // set/read Carrier Gain
      implemented = FALSE;
      break;

    case 'I': //CI
      // sets the current frequency to the CALL Channel
      implemented = FALSE;
      break;

    case 'M': //CM
      // sets/reads the Packet Cluster Tune function
      implemented = FALSE;
      break;

    case 'N': //CN

      // sets/reads CTCSS function (frequency)
      if (can_transmit) {
        if (command[3] == ';') {
          sprintf(reply, "CN%02d;", transmitter->ctcss + 1);
          send_resp(client->fd, reply) ;
        } else if (command[4] == ';') {
          int i = atoi(&command[2]) - 1;
          transmitter_set_ctcss(transmitter, transmitter->ctcss_enabled, i);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    case 'T': //CT

      // sets/reads CTCSS status (on/off)
      if (can_transmit) {
        if (command[2] == ';') {
          sprintf(reply, "CT%d;", transmitter->ctcss_enabled);
          send_resp(client->fd, reply) ;
        } else if (command[3] == ';') {
          int state = atoi(&command[2]);
          transmitter_set_ctcss(transmitter, state, transmitter->ctcss);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'D':
    switch (command[1]) {
    case 'C': //DC
      // set/read TX band status
      implemented = FALSE;
      break;

    case 'N': //DN
      // move VFO A down 1 step size
      vfo_id_step(VFO_A, -1);
      break;

    case 'Q': //DQ
      // set/read DCS function status
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'E':
    switch (command[1]) {
    case 'X': //EX
      // set/read the extension menu
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'F':
    switch (command[1]) {
    case 'A': //FA

      // set/read VFO-A frequency
      if (command[2] == ';') {
        if (vfo[VFO_A].ctun) {
          sprintf(reply, "FA%011lld;", vfo[VFO_A].ctun_frequency);
        } else {
          sprintf(reply, "FA%011lld;", vfo[VFO_A].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[13] == ';') {
        long long f = atoll(&command[2]);
        vfo_set_frequency(VFO_A, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'B': //FB

      // set/read VFO-B frequency
      if (command[2] == ';') {
        if (vfo[VFO_B].ctun) {
          sprintf(reply, "FB%011lld;", vfo[VFO_B].ctun_frequency);
        } else {
          sprintf(reply, "FB%011lld;", vfo[VFO_B].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[13] == ';') {
        long long f = atoll(&command[2]);
        vfo_set_frequency(VFO_B, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'C': //FC
      // set/read the sub receiver VFO frequency menu
      implemented = FALSE;
      break;

    case 'D': //FD
      // set/read the filter display dot pattern
      implemented = FALSE;
      break;

    case 'R': //FR

      // set/read transceiver receive VFO
      if (command[2] == ';') {
        sprintf(reply, "FR%d;", active_receiver->id);
        send_resp(client->fd, reply) ;
      } else if (command[3] == ';') {
        int id = atoi(&command[2]);

        if (id >= 0 && id < receivers) {
          schedule_action(id == 0 ? RX1 : RX2, PRESSED, 0);
        } else {
          implemented = FALSE;
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'S': //FS
      // set/read the fine tune function status
      implemented = FALSE;
      break;

    case 'T': //FT

      // set/read transceiver transmit VFO
      if (command[2] == ';') {
        sprintf(reply, "FT%d;", split);
        send_resp(client->fd, reply) ;
      } else if (command[3] == ';') {
        int val = atoi(&command[2]);
        radio_set_split(val);
      }

      break;

    case 'W': //FW

      // set/read filter width. Switch to Var1 only when setting
      if (command[2] == ';') {
        int val = 0;
        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        const FILTER *filter = &mode_filters[vfo[VFO_A].filter];

        switch (vfo[VFO_A].mode) {
        case modeCWL:
        case modeCWU:
          val = filter->low * 2;
          break;

        case modeAM:
        case modeSAM:
          val = filter->low >= -4000;
          break;

        case modeFMN:
          val = vfo[VFO_A].deviation == 5000;
          break;

        default:
          implemented = FALSE;
          break;
        }

        if (implemented) {
          sprintf(reply, "FW%04d;", val);
          send_resp(client->fd, reply) ;
        }
      } else if (command[6] == ';') {
        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        int fw = atoi(&command[2]);
        filter->low = fw;

        switch (vfo[VFO_A].mode) {
        case modeCWL:
        case modeCWU:
          filter->low = fw / 2;
          filter->high = fw / 2;
          break;

        case modeFMN:
          if (fw == 0) {
            filter->low = -5500;
            filter->high = 5500;
            vfo[VFO_A].deviation = 2500;
          } else {
            filter->low = -8000;
            filter->high = 8000;
            vfo[VFO_A].deviation = 5000;
          }

          set_filter(receiver[0]);

          if (can_transmit) {
            tx_set_filter(transmitter);
          }

          g_idle_add(ext_vfo_update, NULL);
          break;

        case modeAM:
        case modeSAM:
          if (fw == 0) {
            filter->low = -4000;
            filter->high = 4000;
          } else {
            filter->low = -8000;
            filter->high = 8000;
          }

          break;

        default:
          implemented = FALSE;
          break;
        }

        if (implemented) {
          vfo_id_filter_changed(VFO_A, filterVar1);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'G':
    switch (command[1]) {
    case 'T': //GT

      // set/read RX1 AGC
      if (command[2] == ';') {
        sprintf(reply, "GT%03d;", receiver[0]->agc * 5);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        // update RX1 AGC
        receiver[0]->agc = atoi(&command[2]) / 5;
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'H':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'I':
    switch (command[1]) {
    case 'D': //ID
      // get ID
      strcpy(reply, "ID019;"); // TS-2000
      send_resp(client->fd, reply);
      break;

    case 'F': { //IF
      int mode = ts2000_mode(vfo[VFO_A].mode);
      int tx_xit_en = 0;
      int tx_ctcss_en = 0;
      int tx_ctcss = 0;

      if (can_transmit) {
        tx_xit_en   = vfo[get_tx_vfo()].xit_enabled;
        tx_ctcss    = transmitter->ctcss;
        tx_ctcss_en = transmitter->ctcss_enabled;
      }

      sprintf(reply, "IF%011lld%04lld%+06lld%d%d%d%02d%d%d%d%d%d%d%02d%d;",
              vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency,
              step, vfo[VFO_A].rit, vfo[VFO_A].rit_enabled, tx_xit_en,
              0, 0, isTransmitting(), mode, 0, 0, split, tx_ctcss_en ? 2 : 0, tx_ctcss, 0);
      send_resp(client->fd, reply);
    }
    break;

    case 'S': //IS

      // set/read IF shift
      if (command[2] == ';') {
        strcpy(reply, "IS 0000;");
        send_resp(client->fd, reply);
      } else {
        implemented = FALSE;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'J':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'K':
    switch (command[1]) {
    case 'S': //KS

      // set/read keying speed
      if (command[2] == ';') {
        sprintf(reply, "KS%03d;", cw_keyer_speed);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        int speed = atoi(&command[2]);

        if (speed >= 1 && speed <= 60) {
          cw_keyer_speed = speed;
          keyer_update();
          g_idle_add(ext_vfo_update, NULL);
        }
      } else {
      }

      break;

    case 'Y': //KY

      // convert the characters into Morse Code
      if (command[2] == ';') {
        //
        // reply "buffer full" condition if the buffer contains
        // more than (CW_BUF_SIZE-24) characters.
        //
        int avail = cw_buf_in - cw_buf_out;

        if (avail < 0) { avail += CW_BUF_SIZE; }

        if (avail < CW_BUF_SIZE - 24) {
          sprintf(reply, "KY0;");
        } else {
          sprintf(reply, "KY1;");
        }

        send_resp(client->fd, reply);
      } else {
        //
        // Recent versions of Hamlib send CW messages on character at a time.
        // So all trailing blanks have to be removed, and an entirely blank
        // message is interpreted as a inter-word distance.
        // Note we allow variable lengths of incoming messages here, although
        // the standard says they are exactly 24 characters long.
        //
        int j = 3;

        for (int i = 3; i < strlen(command); i++) {
          if (command[i] == ';') { break; }

          if (command[i] != ' ') { j = i; }
        }

        // j points to the last non-blank character, or to the first blank
        // in an empty string
        for (int i = 3; i <= j; i++) {
          int new = cw_buf_in + 1;

          if (new >= CW_BUF_SIZE) { new = 0; }

          if (new != cw_buf_out) {
            cw_buf[cw_buf_in] = command[i];
            cw_buf_in = new;
          }
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'L':
    switch (command[1]) {
    case 'K': //LK

      // set/read key lock
      if (command[2] == ';') {
        sprintf(reply, "LK%d%d;", locked, locked);
        send_resp(client->fd, reply);
      } else if (command[4] == ';') {
        locked = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //LM
      // set/read keyer recording status
      implemented = FALSE;
      break;

    case 'T': //LT
      // set/read ALT fucntion status
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'M':
    switch (command[1]) {
    case 'C': //MC
      // set/read Memory Channel
      implemented = FALSE;
      break;

    case 'D': //MD

      // set/read operating mode
      if (command[2] == ';') {
        int mode = ts2000_mode(vfo[VFO_A].mode);
        sprintf(reply, "MD%d;", mode);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        int mode = modeUSB;

        switch (atoi(&command[2])) {
        case 1:
          mode = modeLSB;
          break;

        case 2:
          mode = modeUSB;
          break;

        case 3:
          mode = modeCWU;
          break;

        case 4:
          mode = modeFMN;
          break;

        case 5:
          mode = modeAM;
          break;

        case 6:
          mode = modeDIGL;
          break;

        case 7:
          mode = modeCWL;
          break;

        case 9:
          mode = modeDIGU;
          break;

        default:
          break;
        }

        vfo_id_mode_changed(VFO_A, mode);
      }

      break;

    case 'F': //MF
      // set/read Menu
      implemented = FALSE;
      break;

    case 'G': //MG

      // set/read Menu Gain (-12..60 converts to 0..100)
      if (command[2] == ';') {
        sprintf(reply, "MG%03d;", (int)(((mic_gain + 12.0) / 72.0) * 100.0));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        double gain = (double)atoi(&command[2]);
        gain = ((gain / 100.0) * 72.0) - 12.0;
        set_mic_gain(gain);
      }

      break;

    case 'L': //ML
      // set/read Monitor Function Level
      implemented = FALSE;
      break;

    case 'O': //MO
      // set/read Monitor Function On/Off
      implemented = FALSE;
      break;

    case 'R': //MR
      // read Memory Channel
      implemented = FALSE;
      break;

    case 'U': //MU
      // set/read Memory Group
      implemented = FALSE;
      break;

    case 'W': //MW
      // Write Memory Channel
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'N':
    switch (command[1]) {
    case 'B': //NB

      // set/read noise blanker
      if (command[2] == ';') {
        sprintf(reply, "NB%d;", receiver[0]->nb);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        receiver[0]->nb = atoi(&command[2]);
        update_noise();
      }

      break;

    case 'L': //NL
      // set/read noise blanker level
      implemented = FALSE;
      break;

    case 'R': //NR

      // set/read noise reduction
      if (command[2] == ';') {
        sprintf(reply, "NR%d;", receiver[0]->nr);
        send_resp(client->fd, reply);
      } else if (command[3] == ';')  {
        receiver[0]->nr = atoi(&command[2]);
        update_noise();
      }

      break;

    case 'T': //NT

      // set/read ANF
      if (command[2] == ';') {
        sprintf(reply, "NT%d;", receiver[0]->anf);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        receiver[0]->anf = atoi(&command[2]);
        update_noise();
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'O':
    switch (command[1]) {
    case 'F': //OF
      // set/read offset frequency
      implemented = FALSE;
      break;

    case 'I': //OI
      // set/read offset frequency
      implemented = FALSE;
      break;

    case 'S': //OS
      // set/read offset function status
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'P':
    switch (command[1]) {
    case 'A': //PA

      // set/read preamp function status
      if (command[2] == ';') {
        sprintf(reply, "PA%d0;", receiver[0]->preamp);
        send_resp(client->fd, reply);
      } else if (command[4] == ';') {
        receiver[0]->preamp = command[2] == '1';
      }

      break;

    case 'B': //PB
      // set/read FRU-3A playback status
      implemented = FALSE;
      break;

    case 'C': //PC

      // set/read PA Power
      if (can_transmit) {
        if (command[2] == ';') {
          sprintf(reply, "PC%03d;", (int)transmitter->drive);
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          set_drive((double)atoi(&command[2]));
        }
      }

      break;

    case 'I': //PI
      // store in program memory channel
      implemented = FALSE;
      break;

    case 'K': //PK
      // read packet cluster data
      implemented = FALSE;
      break;

    case 'L': //PL

      // set/read speach processor input/output level
      if (can_transmit) {
        if (command[2] == ';') {
          sprintf(reply, "PL%03d000;", (int)((transmitter->compressor_level / 20.0) * 100.0));
          send_resp(client->fd, reply);
        } else if (command[8] == ';') {
          command[5] = '\0';
          double level = (double)atoi(&command[2]);
          level = (level / 100.0) * 20.0;
          transmitter_set_compressor_level(transmitter, level);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    case 'M': //PM
      // recall program memory
      implemented = FALSE;
      break;

    case 'R': //PR
      // set/read speech processor function
      implemented = FALSE;
      break;

    case 'S': //PS

      // set/read Power (always ON)
      if (command[2] == ';') {
        sprintf(reply, "PS1;");
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        int pwrc = atoi(&command[2]);
        if ( pwrc == 0 ) {
          sprintf(reply, "PS0;");
          stop_program();
          // shutdown OS if running embedded 
          if (xdma_dir == 1) 
            system("sudo /sbin/shutdown -P now");
        } else sprintf(reply, "PS1;");   
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Q':
    switch (command[1]) {
    case 'C': //QC
      // set/read DCS code
      implemented = FALSE;
      break;

    case 'I': //QI
      // store in quick memory
      implemented = FALSE;
      break;

    case 'R': //QR
      // set/read quick memory channel data
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'R':
    switch (command[1]) {
    case 'A': //RA

      // set/read Attenuator function
      if (command[2] == ';') {
        int att = 0;

        if (have_rx_gain) {
          // map gain value -12...48 to 0...99
          att = (int)(adc[receiver[0]->adc].attenuation + 12);
          att = (int)(((double)att / 60.0) * 99.0);
        }

        if (have_rx_att) {
          // map att value -31 ... 0 to 0...99
          att = (int)(adc[receiver[0]->adc].attenuation);
          att = (int)(((double)att / 31.0) * 99.0);
        }

        sprintf(reply, "RA%02d00;", att);
        send_resp(client->fd, reply);
      } else if (command[4] == ';') {
        int att = atoi(&command[2]);

        if (have_rx_gain) {
          // map 0...99 scale to -12...48
          att = (int)((((double)att / 99.0) * 60.0) - 12.0);
          set_rf_gain(VFO_A, (double)att);
        }

        if (have_rx_att) {
          // mapp 0...99 scale to 0...31
          att = (int)(((double)att / 99.0) * 31.0);
          set_attenuation_value((double)att);
        }
      }

      break;

    case 'C': //RC

      // clears RIT
      if (command[2] == ';') {
        vfo[VFO_A].rit = 0;
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'D': //RD

      // decrements RIT Frequency
      if (command[2] == ';') {
        if (vfo[VFO_A].mode == modeCWL || vfo[VFO_A].mode == modeCWU) {
          vfo[VFO_A].rit -= 10;
        } else {
          vfo[VFO_A].rit -= 50;
        }

        g_idle_add(ext_vfo_update, NULL);
      } else if (command[7] == ';') {
        vfo[VFO_A].rit = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'G': //RG
      // set/read RF gain status
      implemented = FALSE;
      break;

    case 'L': //RL
      // set/read noise reduction level
      implemented = FALSE;
      break;

    case 'M': //RM
      // set/read meter function
      implemented = FALSE;
      break;

    case 'T': //RT

      // set/read RIT enable
      if (command[2] == ';') {
        sprintf(reply, "RT%d;", vfo[VFO_A].rit_enabled);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        vfo[VFO_A].rit_enabled = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'U': //RU

      // increments RIT Frequency
      if (command[2] == ';') {
        if (vfo[VFO_A].mode == modeCWL || vfo[VFO_A].mode == modeCWU) {
          vfo[VFO_A].rit += 10;
        } else {
          vfo[VFO_A].rit += 50;
        }

        g_idle_add(ext_vfo_update, NULL);
      } else if (command[7] == ';') {
        vfo[VFO_A].rit = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'X': //RX

      // set transceiver to RX mode
      if (command[2] == ';') {
        mox_update(0);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'S':
    switch (command[1]) {
    case 'A': //SA

      // set/read stallite mode status
      if (command[2] == ';') {
        sprintf(reply, "SA%d%d%d%d%d%d%dSAT?    ;", (sat_mode == SAT_MODE) || (sat_mode == RSAT_MODE), 0, 0, 0,
                sat_mode == SAT_MODE, sat_mode == RSAT_MODE, 0);
        send_resp(client->fd, reply);
      } else if (command[9] == ';') {
        if (command[2] == '0') {
          sat_mode = SAT_NONE;
        } else if (command[2] == '1') {
          if (command[6] == '0' && command[7] == '0') {
            sat_mode = SAT_NONE;
          } else if (command[6] == '1' && command[7] == '0') {
            sat_mode = SAT_MODE;
          } else if (command[6] == '0' && command[7] == '1') {
            sat_mode = RSAT_MODE;
          } else {
            implemented = FALSE;
          }
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'B': //SB
      // set/read SUB,TF-W status
      implemented = FALSE;
      break;

    case 'C': //SC
      // set/read SCAN function status
      implemented = FALSE;
      break;

    case 'D': //SD

      // set/read CW break-in time delay
      if (command[2] == ';') {
        sprintf(reply, "SD%04d;", (int)fmin(cw_keyer_hang_time, 1000));
        send_resp(client->fd, reply);
      } else if (command[6] == ';') {
        int b = fmin(atoi(&command[2]), 1000);
        cw_breakin = b == 0;
        cw_keyer_hang_time = b;
      } else {
        implemented = FALSE;
      }

      break;

    case 'H': //SH

      // set/read filter high, switch to Var1 only when setting
      if (command[2] == ';') {
        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        const FILTER *filter = &mode_filters[vfo[VFO_A].filter];
        int fh = 5;
        int high = filter->high;

        if (vfo[VFO_A].mode == modeLSB) {
          high = abs(filter->low);
        }

        if (high <= 1400) {
          fh = 0;
        } else if (high <= 1600) {
          fh = 1;
        } else if (high <= 1800) {
          fh = 2;
        } else if (high <= 2000) {
          fh = 3;
        } else if (high <= 2200) {
          fh = 4;
        } else if (high <= 2400) {
          fh = 5;
        } else if (high <= 2600) {
          fh = 6;
        } else if (high <= 2800) {
          fh = 7;
        } else if (high <= 3000) {
          fh = 8;
        } else if (high <= 3400) {
          fh = 9;
        } else if (high <= 4000) {
          fh = 10;
        } else {
          fh = 11;
        }

        sprintf(reply, "SH%02d;", fh);
        send_resp(client->fd, reply) ;
      } else if (command[4] == ';') {
        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        int i = atoi(&command[2]);
        int fh = 100;

        switch (vfo[VFO_A].mode) {
        case modeLSB:
        case modeUSB:
        case modeFMN:
          switch (i) {
          case 0:
            fh = 1400;
            break;

          case 1:
            fh = 1600;
            break;

          case 2:
            fh = 1800;
            break;

          case 3:
            fh = 2000;
            break;

          case 4:
            fh = 2200;
            break;

          case 5:
            fh = 2400;
            break;

          case 6:
            fh = 2600;
            break;

          case 7:
            fh = 2800;
            break;

          case 8:
            fh = 3000;
            break;

          case 9:
            fh = 3400;
            break;

          case 10:
            fh = 4000;
            break;

          case 11:
            fh = 5000;
            break;

          default:
            fh = 100;
            break;
          }

          break;

        case modeAM:
        case modeSAM:
          switch (i) {
          case 0:
            fh = 10;
            break;

          case 1:
            fh = 100;
            break;

          case 2:
            fh = 200;
            break;

          case 3:
            fh = 500;
            break;

          default:
            fh = 100;
            break;
          }

          break;
        }

        if (vfo[VFO_A].mode == modeLSB) {
          filter->low = -fh;
        } else {
          filter->high = fh;
        }

        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'I': //SI
      // enter satellite memory name
      implemented = FALSE;
      break;

    case 'L': //SL

      // set/read filter low, switch to Var1 only when setting
      if (command[2] == ';') {
        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        const FILTER *filter = &mode_filters[vfo[VFO_A].filter];
        int fl = 2;
        int low = filter->low;

        if (vfo[VFO_A].mode == modeLSB) {
          low = abs(filter->high);
        }

        if (low <= 10) {
          fl = 0;
        } else if (low <= 50) {
          fl = 1;
        } else if (low <= 100) {
          fl = 2;
        } else if (low <= 200) {
          fl = 3;
        } else if (low <= 300) {
          fl = 4;
        } else if (low <= 400) {
          fl = 5;
        } else if (low <= 500) {
          fl = 6;
        } else if (low <= 600) {
          fl = 7;
        } else if (low <= 700) {
          fl = 8;
        } else if (low <= 800) {
          fl = 9;
        } else if (low <= 900) {
          fl = 10;
        } else {
          fl = 11;
        }

        sprintf(reply, "SL%02d;", fl);
        send_resp(client->fd, reply) ;
      } else if (command[4] == ';') {
        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        int i = atoi(&command[2]);
        int fl = 100;

        switch (vfo[VFO_A].mode) {
        case modeLSB:
        case modeUSB:
        case modeFMN:
          switch (i) {
          case 0:
            fl = 10;
            break;

          case 1:
            fl = 50;
            break;

          case 2:
            fl = 100;
            break;

          case 3:
            fl = 200;
            break;

          case 4:
            fl = 300;
            break;

          case 5:
            fl = 400;
            break;

          case 6:
            fl = 500;
            break;

          case 7:
            fl = 600;
            break;

          case 8:
            fl = 700;
            break;

          case 9:
            fl = 800;
            break;

          case 10:
            fl = 900;
            break;

          case 11:
            fl = 1000;
            break;

          default:
            fl = 100;
            break;
          }

          break;

        case modeAM:
        case modeSAM:
          switch (i) {
          case 0:
            fl = 10;
            break;

          case 1:
            fl = 100;
            break;

          case 2:
            fl = 200;
            break;

          case 3:
            fl = 500;
            break;

          default:
            fl = 100;
            break;
          }

          break;
        }

        if (vfo[VFO_A].mode == modeLSB) {
          filter->high = -fl;
        } else {
          filter->low = fl;
        }

        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //SM

      // read the S meter
      // Reply is of the form SMYxxxx; where Y = 0,1 and x is from 0 to 30
      // -127 dBm ==> x = 0000
      //  -73 dBm ==> x = 0015
      //  -19 dBm ==> x = 0030
      //
      if (command[3] == ';') {
        int id = atoi(&command[2]);

        if (id >= 0 && id < receivers) {
          int val = (int)((receiver[id]->meter + 127.0) * 0.277778);

          if (val > 30) { val = 30; }

          if (val < 0 ) { val = 0; }

          sprintf(reply, "SM%d%04d;", id, val);
          send_resp(client->fd, reply);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'Q': //SQ

      // set/read Squelch level
      if (command[3] == ';') {
        int p1 = atoi(&command[2]);

        if (p1 == 0) { // Main receiver
          sprintf(reply, "SQ%d%03d;", p1, (int)((double)receiver[0]->squelch / 100.0 * 255.0 + 0.5));
          send_resp(client->fd, reply);
        }
      } else if (command[6] == ';') {
        if (command[2] == '0') {
          int p2 = atoi(&command[3]);
          receiver[0]->squelch = (int)((double)p2 / 255.0 * 100.0 + 0.5);
          set_squelch(receiver[0]);
        }
      } else {
      }

      break;

    case 'R': //SR
      // reset transceiver
      implemented = FALSE;
      break;

    case 'S': //SS
      // set/read program scan pause frequency
      implemented = FALSE;
      break;

    case 'T': //ST
      // set/read MULTI/CH channel frequency steps
      implemented = FALSE;
      break;

    case 'U': //SU
      // set/read program scan pause frequency
      implemented = FALSE;
      break;

    case 'V': //SV
      // execute memory transfer function
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'T':
    switch (command[1]) {
    case 'C': //TC
      // set/read internal TNC mode
      implemented = FALSE;
      break;

    case 'D': //TD
      // send DTMF memory channel data
      implemented = FALSE;
      break;

    case 'I': //TI
      // read TNC LED status
      implemented = FALSE;
      break;

    case 'N': //TN
      // set/read sub-tone frequency
      implemented = FALSE;
      break;

    case 'O': //TO
      // set/read TONE function
      implemented = FALSE;
      break;

    case 'S': //TS
      // set/read TF-SET function
      implemented = FALSE;
      break;

    case 'X': //TX

      // set transceiver to TX mode
      if (command[2] == ';') {
        mox_update(1);
      }

      break;

    case 'Y': //TY
      // set/read microprocessor firmware type
      send_resp(client->fd, "TY000;");
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'U':
    switch (command[1]) {
    case 'L': //UL
      // detects the PLL unlock status
      implemented = FALSE;
      break;

    case 'P': //UP

      // move VFO A up by step
      if (command[2] == ';') {
        vfo_id_step(VFO_A, 1);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'V':
    switch (command[1]) {
    case 'D': //VD
      // set/read VOX delay time
      implemented = FALSE;
      break;

    case 'G': //VG

      // set/read VOX gain (0..9)
      if (command[2] == ';') {
        // convert 0.0..1.0 to 0..9
        sprintf(reply, "VG%03d;", (int)((vox_threshold * 100.0) * 0.9));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        // convert 0..9 to 0.0..1.0
        vox_threshold = atof(&command[2]) / 9.0;
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'R': //VR
      // emulate VOICE1 or VOICE2 key
      implemented = FALSE;
      break;

    case 'X': //VX

      // set/read VOX status
      if (command[2] == ';') {
        sprintf(reply, "VX%d;", vox_enabled);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        vox_enabled = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'W':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'X':
    switch (command[1]) {
    case 'T': //XT

      // set/read XIT enable
      if (can_transmit) {
        if (command[2] == ';') {
          sprintf(reply, "XT%d;", vfo[get_tx_vfo()].xit_enabled);
          send_resp(client->fd, reply);
        } else if (command[3] == ';') {
          vfo_xit_onoff(SET(atoi(&command[2])));
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Y':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Z':
    switch (command[1]) {
    case 'Z':
      implemented = parse_extended_cmd (command, client);
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  default:
    implemented = FALSE;
    break;
  }

  if (!implemented) {
    if (rigctl_debug) { t_print("RIGCTL: UNIMPLEMENTED COMMAND: %s\n", info->command); }

    send_resp(client->fd, "?;");
  }

  client->done = 1; // possibly inform server that command is finished
  g_free(info->command);
  g_free(info);
  return 0;
}

// Serial Port Launch
int set_interface_attribs (int fd, int speed, int parity) {
  struct termios tty;
  memset (&tty, 0, sizeof tty);

  if (tcgetattr (fd, &tty) != 0) {
    t_perror ("RIGCTL (tcgetattr):");
    return -1;
  }

  cfsetospeed (&tty, speed);
  cfsetispeed (&tty, speed);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
  // disable IGNBRK for mismatched speed tests; otherwise receive break
  // as \000 chars
  tty.c_iflag &= ~IGNBRK;         // disable break processing
  tty.c_lflag = 0;                // no signaling chars, no echo,
  // no canonical processing
  tty.c_oflag = 0;                // no remapping, no delays
  tty.c_cc[VMIN]  = 0;            // read doesn't block
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout
  //tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
  tty.c_iflag |= (IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
  tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
  // enable reading
  tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
  tty.c_cflag |= parity;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  if (tcsetattr (fd, TCSANOW, &tty) != 0) {
    t_perror( "RIGCTL (tcsetattr):");
    return -1;
  }

  return 0;
}

void set_blocking (int fd, int should_block) {
  struct termios tty;
  memset (&tty, 0, sizeof tty);
  int flags = fcntl(fd, F_GETFL, 0);

  if (should_block) {
    flags &= ~O_NONBLOCK;
  } else {
    flags |= O_NONBLOCK;
  }

  fcntl(fd, F_SETFL, flags);

  if (tcgetattr (fd, &tty) != 0) {
    t_perror ("RIGCTL (tggetattr):");
    return;
  }

  tty.c_cc[VMIN]  = SET(should_block);
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

  if (tcsetattr (fd, TCSANOW, &tty) != 0) {
    t_perror("RIGCTL (tcsetattr):");
  }
}

static gpointer serial_server(gpointer data) {
  // We're going to Read the Serial port and
  // when we get data we'll send it to parse_cmd
  CLIENT *client = (CLIENT *)data;
  char cmd_input[MAXDATASIZE];
  char *command = g_new(char, MAXDATASIZE);
  int command_index = 0;
  int i;
  fd_set fds;
  struct timeval tv;
  t_print("%s: Entering Thread\n", __FUNCTION__);
  g_mutex_lock(&mutex_a->m);
  cat_control++;

  if (rigctl_debug) { t_print("RIGCTL: SER INC cat_control=%d\n", cat_control); }

  g_mutex_unlock(&mutex_a->m);
  g_idle_add(ext_vfo_update, NULL);
  client->running = TRUE;

  while (client->running) {
    //
    // If the "serial line" is a FIFO, we must not drain it
    // by reading our own responses (they must go to the other
    // side). Therefore, wait until 50msec after the last
    // CAT command of this client has been processed.
    // If for some reason this does not happen, resume after
    // waiting for about 500 msec.
    // Check client->running after the "pause" and after returning
    // from "read".
    //
    while (client->fifo && client->busy > 0) {
      if (client->done) {
        // command done, possibly response sent:
        // wait 50 msec then resume listening
        usleep(50000L);
        break;
      }

      usleep(50000L);
      client->busy--;
    }

    client->busy = 0;
    client->done = 0;

    if (!client->running) { break; }

    //
    // Blocking I/O with a time-out
    //
    FD_ZERO(&fds);
    FD_SET(client->fd, &fds);
    tv.tv_usec = 250000; // 250 msec
    tv.tv_sec = 0;

    if (select(client->fd + 1, &fds, NULL, NULL, &tv) <= 0) {
      continue;
    }

    int numbytes = read (client->fd, cmd_input, sizeof cmd_input);

    //
    // On my MacOS using a FIFO, I have seen that numbytes can be -1
    // (with errno = EAGAIN) although the select() inidcated that data
    // is available. Therefore the serial thread is not shut down if
    // the read() failed -- it will try again and again until it is
    // shut down by the rigctl menu.

    if (!client->running) { break; }

    if (numbytes > 0) {
      for (i = 0; i < numbytes; i++) {
        command[command_index] = cmd_input[i];
        command_index++;

        if (cmd_input[i] == ';') {
          command[command_index] = '\0';

          if (rigctl_debug) { t_print("RIGCTL: serial command=%s\n", command); }

          COMMAND *info = g_new(COMMAND, 1);
          info->client = client;
          info->command = command;
          g_mutex_lock(&mutex_busy->m);
          client->busy = 10;
          g_idle_add(parse_cmd, info);
          g_mutex_unlock(&mutex_busy->m);
          command = g_new(char, MAXDATASIZE);
          command_index = 0;
        }
      }
    }
  }

  g_mutex_lock(&mutex_a->m);
  cat_control--;

  if (rigctl_debug) { t_print("RIGCTL: SER DEC - cat_control=%d\n", cat_control); }

  g_mutex_unlock(&mutex_a->m);
  g_idle_add(ext_vfo_update, NULL);
  t_print("%s: Exiting Thread, running=%d\n", __FUNCTION__, client->running);
  return NULL;
}

static int last_mox;
static int last_tune;
static int last_ps;
static int last_ctun;
static int last_lock;
static int last_div;
static int last_rit;
static int last_xit;
static int last_vfoa;

gboolean andromeda_handler(gpointer data) {
  //
  // This function is repeatedly called until it returns FALSE
  //
  //
  const CLIENT *client = (CLIENT *)data;
  char reply[64];

  if (!client->running) { return FALSE; }

  if (last_vfoa != active_receiver->id) {
    sprintf(reply, "ZZZI10%d;", active_receiver->id ^ 1);
    send_resp(client->fd, reply);
    last_vfoa = active_receiver->id;
  }

  if (last_div != diversity_enabled) {
    sprintf(reply, "ZZZI05%d;", diversity_enabled);
    send_resp(client->fd, reply);
    last_div = diversity_enabled;
  }

  if (last_mox != mox) {
    sprintf(reply, "ZZZI01%d;", mox);
    send_resp(client->fd, reply);
    last_mox = mox;
  }

  if (last_tune != tune) {
    sprintf(reply, "ZZZI03%d;", tune);
    send_resp(client->fd, reply);
    last_tune = tune;
  }

  if (can_transmit) {
    if (last_ps != transmitter->puresignal) {
      sprintf(reply, "ZZZI04%d;", transmitter->puresignal);
      send_resp(client->fd, reply);
      last_ps = transmitter->puresignal;
    }
  }

  if (last_ctun != vfo[active_receiver->id].ctun) {
    sprintf(reply, "ZZZI07%d;", vfo[active_receiver->id].ctun);
    send_resp(client->fd, reply);
    last_ctun = vfo[active_receiver->id].ctun;
  }

  if (last_rit != vfo[active_receiver->id].rit_enabled) {
    sprintf(reply, "ZZZI08%d;", vfo[active_receiver->id].rit_enabled);
    send_resp(client->fd, reply);
    last_rit = vfo[active_receiver->id].rit_enabled;
  }

  if (can_transmit) {
    int new_xit = vfo[get_tx_vfo()].xit_enabled;

    if (last_xit != new_xit) {
      sprintf(reply, "ZZZI09%d;", new_xit);
      send_resp(client->fd, reply);
      last_xit = new_xit;
    }
  }

  if (last_lock != locked) {
    sprintf(reply, "ZZZI11%d;", locked);
    send_resp(client->fd, reply);
    last_lock = locked;
  }

  return TRUE;
}

gboolean andromeda_init(gpointer data) {
  //
  // This function is put into the GTK idle queue
  // when an "andromeda" serial line is opened
  //
  const CLIENT *client = (CLIENT *)data;

  if (!client->running) { return FALSE; }

  // This triggers new results to be reported;
  last_mox = last_tune = last_ps = last_ctun = last_lock = last_div = last_rit = last_xit = last_vfoa = -999;
  // This triggers a reply (from Andromeda) to report its FP version
  send_resp(client->fd, "ZZZS;");
  return FALSE;
}

int launch_serial (int id) {
  int fd;
  int baud;
  t_print("%s: Open Serial Port %s\n", __FUNCTION__, SerialPorts[id].port);

  if (mutex_busy == NULL) {
    mutex_busy = g_new(GT_MUTEX, 1);
    g_mutex_init(&mutex_busy->m);
  }

  //
  // Use O_NONBLOCK to prevent "hanging" upon open(), set blocking mode
  // later.
  //
  fd = open (SerialPorts[id].port, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);

  if (fd < 0) {
    t_perror("RIGCTL (open serial):");
    return 0 ;
  }

  t_print("%s: serial port fd=%d\n", __FUNCTION__, fd);
  serial_client[id].fd = fd;
  serial_client[id].busy = 0;
  serial_client[id].fifo = 0;
  // hard-wired parity = NONE
  // if ANDROMEDA, hard-wired baud = 9600
  baud = SerialPorts[id].baud;

  if (SerialPorts[id].andromeda) { baud = B9600; }

  if (set_interface_attribs (fd, baud, 0) == 0) {
    set_blocking (fd, 1);                   // set blocking
  } else {
    //
    // This tells the server that fd is something else
    // than a serial line (most likely a FIFO), but it
    // can still be used.
    //
    t_print("%s: serial port is probably a FIFO\n", __FUNCTION__);
    serial_client[id].fifo = 1;
  }

  serial_client[id].running = 1;
  serial_client[id].andromeda_timer = 0;
  serial_client[id].thread_id = g_thread_new( "Serial server", serial_server, &serial_client[id]);
  //
  // If this is a serial line to an ANDROMEDA controller, initialize it and start a periodic GTK task
  //
  launch_andromeda(id);
  return 1;
}

void launch_andromeda (int id) {
  //
  // This is a no-op if the serial client is NOT running
  //
  if (SerialPorts[id].andromeda && serial_client[id].running) {
    t_print("%s: Enable ANDROMEDA on Port %s\n", __FUNCTION__, SerialPorts[id].port);
    usleep(700000L); // Need to wait for andromedas serial to settle, Andromeda FP Version: h/w:01 s/w:006
    g_idle_add(andromeda_init, &serial_client[id]);           // executed once
    serial_client[id].andromeda_timer = g_timeout_add(500, andromeda_handler, &serial_client[id]); // executed periodically
  }
}

// Serial Port close
void disable_serial (int id) {
  t_print("%s: Close Serial Port %s\n", __FUNCTION__, SerialPorts[id].port);
  disable_andromeda(id);
  serial_client[id].running = FALSE;

  if (serial_client[id].fifo) {
    //
    // If the "serial port" is a fifo then the serial thread
    // may hang in a blocking read().
    // Fortunately, we can set the thread free
    // by sending something to the FIFO
    //
    write (serial_client[id].fd, "ID;", 3);
  }

  // wait for the serial server actually terminating
  if (serial_client[id].thread_id) {
    g_thread_join(serial_client[id].thread_id);
    serial_client[id].thread_id = NULL;
  }

  if (serial_client[id].fd >= 0) {
    close(serial_client[id].fd);
    serial_client[id].fd = -1;
  }
}

void disable_andromeda (int id) {
  if (serial_client[id].andromeda_timer != 0) {
    t_print("%s: disable ANDROMEDA on port %s\n", __FUNCTION__, SerialPorts[id].port);
    g_source_remove(serial_client[id].andromeda_timer);
    serial_client[id].andromeda_timer = 0;
  }
}
//
// 2-25-17 - K5JAE - create each thread with the pointer to the port number
//                   (Port numbers now const ints instead of defines..)
//
void launch_rigctl () {
  t_print( "---- LAUNCHING RIGCTL ----\n");
  cat_control = 0;
  mutex_a = g_new(GT_MUTEX, 1); // memory leak
  g_mutex_init(&mutex_a->m);
  server_running = 1;
  rigctl_server_thread_id = g_thread_new( "rigctl server", rigctl_server, GINT_TO_POINTER(rigctl_port_base));
}
