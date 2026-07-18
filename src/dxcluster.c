/* Copyright (C)
*  2026 - piHPSDR Modernisation, contribution from AL
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

#include "dxcluster.h"
#include "dxcluster_db.h"
#include "property.h"
#include "message.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

/* ── Module state ─────────────────────────────────────────────────────── */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static DXC_SETTINGS    settings;
static int             settings_initialised = 0;
static DXC_STATE       state = DXC_DISABLED;
static DX_SPOT         ring[DXC_MAX_SPOTS];
static int             ring_count = 0;
static int             ring_head  = 0;        /* next write position */
static int             session_spots = 0;

/* Worker thread */
static pthread_t       worker_tid;
static int             worker_running = 0;
static int             worker_stop    = 0;
static int             sock_fd        = -1;   /* protected by state_lock */
static pthread_cond_t  wake_cv        = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t wake_lock      = PTHREAD_MUTEX_INITIALIZER;

/* ── Helpers ──────────────────────────────────────────────────────────── */

static void set_state_locked(DXC_STATE s) {
  state = s;
}

static void set_state(DXC_STATE s) {
  pthread_mutex_lock(&state_lock);
  set_state_locked(s);
  pthread_mutex_unlock(&state_lock);
}

static void close_socket_locked(void) {
  if (sock_fd >= 0) {
    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    sock_fd = -1;
  }
}

/* Add a spot to the ring buffer. Caller must hold state_lock. */
static void ring_push_locked(const DX_SPOT *s) {
  ring[ring_head] = *s;
  ring_head = (ring_head + 1) % DXC_MAX_SPOTS;
  if (ring_count < DXC_MAX_SPOTS) { ring_count++; }
}

/* ── Cluster spot line parser ─────────────────────────────────────────── */
/*
 * Standard format we expect from VE7CC and most other clusters:
 *
 *   DX de SPOTTER:    14076.0  DXCALL       FT8 -12 dB into MA      1430Z
 *   012345678901234567890123456789012345678901234567890...
 *
 * Some clusters omit the leading "DX de", or use spaces differently. We
 * try to be liberal in accepting; strict in rejecting non-spot lines like
 * WWV announcements, CTYstart, etc.
 *
 * Returns 1 if a spot was parsed successfully, 0 otherwise.
 */
static int parse_spot_line(const char *line, DX_SPOT *out) {
  if (!line || !out) { return 0; }
  /* Skip non-spot lines */
  if (strncmp(line, "DX de ", 6) != 0 &&
      strncmp(line, "DX DE ", 6) != 0) {
    return 0;
  }
  const char *p = line + 6;
  /* Spotter callsign — up to ':' */
  const char *colon = strchr(p, ':');
  if (!colon) { return 0; }
  size_t spotter_len = (size_t)(colon - p);
  if (spotter_len == 0 || spotter_len >= DXC_CALL_LEN) { return 0; }
  memcpy(out->spotter, p, spotter_len);
  out->spotter[spotter_len] = '\0';
  /* Strip trailing spaces from spotter */
  while (spotter_len > 0 && out->spotter[spotter_len - 1] == ' ') {
    out->spotter[--spotter_len] = '\0';
  }
  /* After colon: frequency in kHz, then callsign, then comment, then time */
  p = colon + 1;
  while (*p == ' ') { p++; }
  /* Frequency in kHz, parse as double then convert to Hz */
  char *endp = NULL;
  double freq_khz = strtod(p, &endp);
  if (endp == p || freq_khz < 1.0 || freq_khz > 200000.0) { return 0; }
  out->freq_hz = (long long)(freq_khz * 1000.0 + 0.5);
  p = endp;
  while (*p == ' ') { p++; }
  /* DX callsign — up to next space */
  const char *call_start = p;
  while (*p && *p != ' ') { p++; }
  size_t call_len = (size_t)(p - call_start);
  if (call_len == 0 || call_len >= DXC_CALL_LEN) { return 0; }
  memcpy(out->dx_call, call_start, call_len);
  out->dx_call[call_len] = '\0';
  while (*p == ' ') { p++; }
  /*
   * Now we have a free-form remainder ending with a time stamp like 1430Z.
   * Take up to comment-len for the comment; try to extract a mode if the
   * remainder starts with one of our known modes followed by a space.
   */
  out->mode[0] = '\0';
  static const char * const known_modes[] = {
    "FT8", "FT4", "CW", "SSB", "USB", "LSB", "RTTY", "PSK", "PSK31",
    "JT65", "JT9", "MFSK", "OLIVIA", "SSTV", "FM", "AM", NULL
  };
  for (int i = 0; known_modes[i]; i++) {
    size_t ml = strlen(known_modes[i]);
    if (strncmp(p, known_modes[i], ml) == 0 &&
        (p[ml] == ' ' || p[ml] == '\0' || p[ml] == '\t')) {
      snprintf(out->mode, sizeof(out->mode), "%s", known_modes[i]);
      p += ml;
      while (*p == ' ') { p++; }
      break;
    }
  }
  /* The remaining string up to ~40 chars (or until time stamp / EOL) is
   * the comment. Strip trailing time stamp (NNNNZ) if present. */
  size_t remain = strlen(p);
  /* Strip trailing whitespace and CR/LF */
  while (remain > 0 && (p[remain - 1] == '\n' || p[remain - 1] == '\r' ||
                        p[remain - 1] == ' '  || p[remain - 1] == '\t')) {
    remain--;
  }
  /* Strip trailing NNNNZ time stamp */
  if (remain >= 5 && p[remain - 1] == 'Z') {
    int all_digits = 1;
    for (int i = 2; i <= 5; i++) {
      if (p[remain - i] < '0' || p[remain - i] > '9') { all_digits = 0; break; }
    }
    if (all_digits) {
      remain -= 5;
      while (remain > 0 && p[remain - 1] == ' ') { remain--; }
    }
  }
  //
  // UTF characters are not valid for PANGO, so we filter them out
  //
  char *q = out->comment;
  for (;;) {
    char c = *p++;
    if (c & 0x80) { continue; }
    *q++ = c;
    if (c == '\0') { break; }
    if ((q - out->comment) == (DXC_COMMENT_LEN - 1)) {
      *q = '\0';
      break;
    }
  }
  out->when = time(NULL);
  return 1;
}

/* ── Filtering ────────────────────────────────────────────────────────── */
/*
 * Region from callsign prefix. Coarse mapping — good enough for the filter.
 * Reference: ITU prefix list and amateur band conventions.
 */
static int spotter_in_region(const char *call, const DXC_SETTINGS *s) {
  if (!call || !call[0]) { return 1; }   /* no filtering possible */
  char c0 = call[0];
  char c1 = call[1] ? call[1] : '\0';
  /* North America: K*, W*, N*, AA-AL, VA-VE (Canada), XE (Mexico) */
  if (c0 == 'K' || c0 == 'W' || c0 == 'N') { return s->region_na; }
  if (c0 == 'A' && c1 >= 'A' && c1 <= 'L') { return s->region_na; }
  if (c0 == 'V' && (c1 >= 'A' && c1 <= 'E')) { return s->region_na; }
  if (c0 == 'X' && c1 == 'E') { return s->region_na; }
  /* Europe: G, M, 2 (UK), DA-DR (DE), F, ON, OK, OZ, OH, OE, SM, EA, I, ... */
  if (c0 == 'G' || c0 == 'M' || c0 == '2') { return s->region_eu; }
  if (c0 == 'D' && (c1 >= 'A' && c1 <= 'R')) { return s->region_eu; }
  if (c0 == 'F' || c0 == 'I') { return s->region_eu; }
  if (c0 == 'O' && (c1 == 'N' || c1 == 'K' || c1 == 'Z' ||
                    c1 == 'H' || c1 == 'E')) { return s->region_eu; }
  if (c0 == 'S' && (c1 == 'M' || c1 == 'P')) { return s->region_eu; }
  if (c0 == 'E' && c1 == 'A') { return s->region_eu; }
  if (c0 == 'C' && c1 == 'T') { return s->region_eu; }   /* Portugal */
  /* Asia: J (Japan), B (China), HL (Korea), VR (HK), VU (India), 9V, 9M */
  if (c0 == 'J') { return s->region_as; }
  if (c0 == 'B') { return s->region_as; }
  if (c0 == 'H' && c1 == 'L') { return s->region_as; }
  if (c0 == 'V' && (c1 == 'R' || c1 == 'U')) { return s->region_as; }
  if (c0 == '9' && (c1 == 'V' || c1 == 'M')) { return s->region_as; }
  /* South America: PY/PP/PT (Brazil), CE (Chile), LU (Argentina), HK (Colombia) */
  if (c0 == 'P' && (c1 == 'Y' || c1 == 'P' || c1 == 'T')) { return s->region_sa; }
  if (c0 == 'C' && c1 == 'E') { return s->region_sa; }
  if (c0 == 'L' && c1 == 'U') { return s->region_sa; }
  if (c0 == 'H' && c1 == 'K') { return s->region_sa; }
  /* Oceania: VK (Australia), ZL (NZ), KH6 (Hawaii) */
  if (c0 == 'V' && c1 == 'K') { return s->region_oc; }
  if (c0 == 'Z' && c1 == 'L') { return s->region_oc; }
  /* Africa: 5x and 6x prefixes, ZS (S. Africa), CN (Morocco), V5 (Namibia) etc */
  if (c0 == 'Z' && c1 == 'S') { return s->region_af; }
  if (c0 == 'C' && c1 == 'N') { return s->region_af; }
  if (c0 == '5' || c0 == '6' || c0 == '7') { return s->region_af; }
  /* Default: assume EU (most common cluster spotters) */
  return s->region_eu;
}

static int mode_passes_filter(const char *mode, const DXC_SETTINGS *s) {
  if (!mode || !mode[0]) { return s->mode_other; }
  if (!strcmp(mode, "FT8"))  { return s->mode_ft8;  }
  if (!strcmp(mode, "FT4"))  { return s->mode_ft4;  }
  if (!strcmp(mode, "CW"))   { return s->mode_cw;   }
  if (!strcmp(mode, "SSB") || !strcmp(mode, "USB") || !strcmp(mode, "LSB")) {
    return s->mode_ssb;
  }
  if (!strcmp(mode, "RTTY")) { return s->mode_rtty; }
  return s->mode_other;
}

static int call_in_prefix_list(const char *call, const char *list) {
  if (!list || !list[0]) { return 0; }
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", list);
  for (char *tok = strtok(buf, ", "); tok; tok = strtok(NULL, ", ")) {
    size_t len = strlen(tok);
    if (len > 0 && strncasecmp(call, tok, len) == 0) { return 1; }
  }
  return 0;
}

/* Returns 1 if spot should be visible under current settings (snapshot). */
static int spot_passes_filters(const DX_SPOT *spot, const DXC_SETTINGS *s,
                               time_t now) {
  /* Age */
  if ((now - spot->when) > s->age_limit_sec) { return 0; }
  /* Mode */
  if (!mode_passes_filter(spot->mode, s)) { return 0; }
  /* Spotter region */
  if (!spotter_in_region(spot->spotter, s)) { return 0; }
  /* Whitelist takes precedence: if set, only matching callsigns pass */
  if (s->whitelist[0]) {
    if (!call_in_prefix_list(spot->dx_call, s->whitelist)) { return 0; }
  }
  /* Blacklist */
  if (s->blacklist[0]) {
    if (call_in_prefix_list(spot->dx_call, s->blacklist)) { return 0; }
  }
  return 1;
}

/* ── Worker thread: TCP client + parser loop ──────────────────────────── */

static int tcp_connect(const char *host, int port,
                       char *err_buf, int err_buf_len) {
  if (err_buf) { err_buf[0] = '\0'; }
  struct addrinfo hints = {0};
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);
  struct addrinfo *res = NULL;
  int gai = getaddrinfo(host, port_str, &hints, &res);
  if (gai != 0 || !res) {
    if (err_buf) {
      snprintf(err_buf, err_buf_len, "Hostname lookup failed: %s",
               gai_strerror(gai));
    }
    return -1;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    if (err_buf) { snprintf(err_buf, err_buf_len, "socket(): %s", strerror(errno)); }
    freeaddrinfo(res);
    return -1;
  }
  /* Non-blocking connect with a select() timeout.
   * Don't use SO_SNDTIMEO/SO_RCVTIMEO before connect — Linux's implementation
   * makes the connect non-blocking and returns EINPROGRESS, which is hard
   * to distinguish from a real error. Use proper non-blocking + select. */
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int crc = connect(fd, res->ai_addr, res->ai_addrlen);
  if (crc < 0 && errno != EINPROGRESS) {
    if (err_buf) { snprintf(err_buf, err_buf_len, "connect(): %s", strerror(errno)); }
    close(fd);
    freeaddrinfo(res);
    return -1;
  }
  if (crc < 0) {
    /* Wait for connect to finish — re-poll if needed since some kernels
     * wake select() before connect completes, returning EINPROGRESS from
     * SO_ERROR. Total max wait ~15s. */
    int max_polls = 15;
    int connected = 0;
    while (max_polls-- > 0 && !connected) {
      fd_set wfds, efds;
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);
      FD_ZERO(&efds);
      FD_SET(fd, &efds);
      struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
      int sr = select(fd + 1, NULL, &wfds, &efds, &tv);
      if (sr < 0) {
        if (err_buf) { snprintf(err_buf, err_buf_len, "select(): %s", strerror(errno)); }
        close(fd);
        freeaddrinfo(res);
        return -1;
      }
      if (sr == 0) { continue; }   /* 1s tick, keep polling */
      /* Socket woke — check actual status */
      int soerr = 0;
      socklen_t soerr_len = sizeof(soerr);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &soerr_len) < 0) {
        if (err_buf) { snprintf(err_buf, err_buf_len, "getsockopt(): %s", strerror(errno)); }
        close(fd);
        freeaddrinfo(res);
        return -1;
      }
      if (soerr == 0) {
        connected = 1;
        break;
      }
      if (soerr == EINPROGRESS || soerr == EALREADY) {
        /* Kernel says still working — wait another tick */
        continue;
      }
      /* Real failure */
      if (err_buf) { snprintf(err_buf, err_buf_len, "connect(): %s", strerror(soerr)); }
      close(fd);
      freeaddrinfo(res);
      return -1;
    }
    if (!connected) {
      if (err_buf) snprintf(err_buf, err_buf_len,
                              "Connection timed out (no response from %s:%d after 15s).\n"
                              "Check the hostname/port and that your firewall allows "
                              "outbound TCP to that address.", host, port);
      close(fd);
      freeaddrinfo(res);
      return -1;
    }
  }
  /* Restore blocking mode and freeaddrinfo */
  fcntl(fd, F_SETFL, flags);
  freeaddrinfo(res);
  /* Set a 60s recv timeout for the steady-state read loop so the worker
   * thread can occasionally check worker_stop even when the cluster is quiet. */
  struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  return fd;
}

/* Read at most max_len bytes; null-terminate; return bytes read or -1.
 * Wraps recv to be friendlier about EINTR/timeouts. */
static int sock_read(int fd, char *buf, int max_len) {
  ssize_t n = recv(fd, buf, max_len - 1, 0);
  if (n <= 0) { return -1; }
  buf[n] = '\0';
  return (int)n;
}

static void sock_writeln(int fd, const char *s) {
  if (fd < 0) { return; }
  send(fd, s, strlen(s), MSG_NOSIGNAL);
  send(fd, "\r\n", 2, MSG_NOSIGNAL);
}

/* Process one full line received from the cluster. */
static void handle_line(const char *line) {
  DX_SPOT spot = {0};
  if (parse_spot_line(line, &spot)) {
    pthread_mutex_lock(&state_lock);
    ring_push_locked(&spot);
    session_spots++;
    pthread_mutex_unlock(&state_lock);
    /* Mirror into SQLite history (handles its own locking) */
    dxcluster_db_insert(&spot);
  }
  /* Non-spot lines (announcements, prompts) are ignored. */
}

/* Sleep up to `seconds`, interruptible by worker_stop or settings change. */
static void interruptible_sleep(int seconds) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += seconds;
  pthread_mutex_lock(&wake_lock);
  while (!worker_stop) {
    int rc = pthread_cond_timedwait(&wake_cv, &wake_lock, &ts);
    if (rc != 0) { break; }   /* timeout or signaled */
  }
  pthread_mutex_unlock(&wake_lock);
}

static void *worker_main(void *arg) {
  int backoff = 5;
  char line_buf[1024];
  int  line_pos = 0;
  char rxbuf[1024];
  while (!worker_stop) {
    /* Take a snapshot of connection settings */
    pthread_mutex_lock(&state_lock);
    DXC_SETTINGS s = settings;
    pthread_mutex_unlock(&state_lock);
    if (!s.enabled) {
      set_state(DXC_DISABLED);
      interruptible_sleep(2);
      continue;
    }
    if (strlen(s.callsign) < 1) {
      set_state(DXC_ERROR);
      continue;
    }
    set_state(DXC_CONNECTING);
    t_print("dxcluster: connecting to %s:%d\n", s.server, s.port);
    int fd = tcp_connect(s.server, s.port, NULL, 0);
    if (fd < 0) {
      set_state(DXC_ERROR);
      t_print("dxcluster: connect failed; backoff %ds\n", backoff);
      interruptible_sleep(backoff);
      backoff = backoff * 2;
      if (backoff > 1800) { backoff = 1800; }     /* cap at 30 min */
      continue;
    }
    /* Wait briefly for the "login:" prompt then send callsign */
    int n = sock_read(fd, rxbuf, sizeof(rxbuf));
    if (n > 0 && s.callsign[0]) {
      sock_writeln(fd, s.callsign);
    }
    pthread_mutex_lock(&state_lock);
    sock_fd = fd;
    set_state_locked(DXC_CONNECTED);
    pthread_mutex_unlock(&state_lock);
    backoff = 5;   /* reset backoff on successful connect */
    line_pos = 0;
    t_print("dxcluster: connected\n");
    /* Read loop */
    while (!worker_stop) {
      n = sock_read(fd, rxbuf, sizeof(rxbuf));
      if (n <= 0) {
        /* Distinguish timeout (recv returns -1 with EAGAIN) from peer close */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /* No data within 60s — that's fine, keep waiting */
          continue;
        }
        break;
      }
      /* Pull lines out of the rx buffer */
      for (int i = 0; i < n; i++) {
        char c = rxbuf[i];
        if (c == '\r') { continue; }
        if (c == '\n') {
          line_buf[line_pos] = '\0';
          if (line_pos > 0) { handle_line(line_buf); }
          line_pos = 0;
        } else if (line_pos < (int)sizeof(line_buf) - 1) {
          line_buf[line_pos++] = c;
        }
      }
      /* Check if settings disabled mid-run */
      pthread_mutex_lock(&state_lock);
      int still_enabled = settings.enabled;
      pthread_mutex_unlock(&state_lock);
      if (!still_enabled) { break; }
    }
    /* Disconnected */
    pthread_mutex_lock(&state_lock);
    close_socket_locked();
    pthread_mutex_unlock(&state_lock);
    set_state(DXC_DISCONNECTED);
    t_print("dxcluster: disconnected\n");
    if (worker_stop) { break; }
    /* Reload settings — user may have disabled */
    pthread_mutex_lock(&state_lock);
    s = settings;
    pthread_mutex_unlock(&state_lock);
    if (!s.auto_reconnect || !s.enabled) {
      set_state(s.enabled ? DXC_DISCONNECTED : DXC_DISABLED);
      interruptible_sleep(2);
      continue;
    }
    interruptible_sleep(backoff);
    backoff = backoff * 2;
    if (backoff > 1800) { backoff = 1800; }
  }
  set_state(DXC_DISABLED);
  return NULL;
}

static void wake_worker(void) {
  pthread_mutex_lock(&wake_lock);
  pthread_cond_signal(&wake_cv);
  pthread_mutex_unlock(&wake_lock);
}

/* ── Public API ───────────────────────────────────────────────────────── */

static void set_default_settings(DXC_SETTINGS *s) {
  memset(s, 0, sizeof(*s));
  s->enabled            = 0;
  snprintf(s->server, sizeof(s->server), "%s", "ve7cc.net");
  s->port               = 23;
  s->callsign[0]        = '\0';
  s->auto_reconnect     = 1;
  s->show_on_panadapter = 1;
  s->age_limit_sec      = 600;    /* 10 minutes */
  s->mode_ft8 = s->mode_ft4 = s->mode_cw = s->mode_ssb =
                                s->mode_rtty = s->mode_other = 1;
  s->region_na = s->region_eu = s->region_as = s->region_sa =
                                  s->region_af = s->region_oc = 1;
  s->whitelist[0] = '\0';
  s->blacklist[0] = '\0';
}

//
// Note this is called after dxcluster_restore_state
//
void dxcluster_init(void) {
  //
  // dxcluster_restore_state has already been called,
  // but just in case . . .
  //
  if (!settings_initialised) {
    pthread_mutex_lock(&state_lock);
    set_default_settings(&settings);
    settings_initialised = 1;
    pthread_mutex_unlock(&state_lock);
  }
  dxcluster_db_init();
  /* Load persisted ring buffer */
  FILE *f = fopen("dxspots.csv", "r");
  if (f) {
    char ln[512];
    while (fgets(ln, sizeof(ln), f)) {
      DX_SPOT sp = {0};
      long long freq = 0;
      long when = 0;
      int matched = sscanf(ln, "%lld,%15[^,],%15[^,],%7[^,],%39[^,],%ld",
                           &freq, sp.dx_call, sp.spotter, sp.mode,
                           sp.comment, &when);
      if (matched >= 5) {
        sp.freq_hz = freq;
        sp.when    = when;
        pthread_mutex_lock(&state_lock);
        ring_push_locked(&sp);
        pthread_mutex_unlock(&state_lock);
      }
    }
    fclose(f);
  }
  /* Always start the worker — it loops on state.enabled internally */
  worker_running = 1;
  worker_stop    = 0;
  pthread_create(&worker_tid, NULL, worker_main, NULL);
}

void dxcluster_shutdown(void) {
  //
  // This is called when the program terminates.
  // Do not wait too long for threads etc.
  //
  if (!worker_running) { return; }
  worker_stop = 1;
  pthread_mutex_lock(&state_lock);
  close_socket_locked();
  pthread_mutex_unlock(&state_lock);
  wake_worker();
  pthread_join(worker_tid, NULL);
  worker_running = 0;
  /* Persist ring buffer */
  FILE *f = fopen("dxspots.csv", "w");
  if (f) {
    pthread_mutex_lock(&state_lock);
    int n = ring_count;
    int idx = (ring_head - ring_count + DXC_MAX_SPOTS) % DXC_MAX_SPOTS;
    for (int i = 0; i < n; i++) {
      const DX_SPOT *sp = &ring[idx];
      fprintf(f, "%lld,%s,%s,%s,%s,%ld\n",
              sp->freq_hz, sp->dx_call, sp->spotter, sp->mode,
              sp->comment[0] ? sp->comment : "", (long)sp->when);
      idx = (idx + 1) % DXC_MAX_SPOTS;
    }
    pthread_mutex_unlock(&state_lock);
    fclose(f);
  }
  dxcluster_db_shutdown();
}

void dxcluster_apply_settings(const DXC_SETTINGS *s) {
  if (!s) { return; }
  pthread_mutex_lock(&state_lock);
  int reconnect_needed =
    (settings.enabled != s->enabled) ||
    (strcmp(settings.server, s->server) != 0) ||
    (settings.port != s->port) ||
    (strcmp(settings.callsign, s->callsign) != 0);
  settings = *s;
  if (reconnect_needed) {
    close_socket_locked();
  }
  pthread_mutex_unlock(&state_lock);
  wake_worker();
}

void dxcluster_get_settings(DXC_SETTINGS *out) {
  pthread_mutex_lock(&state_lock);
  if (!settings_initialised) {
    set_default_settings(&settings);
    settings_initialised = 1;
  }
  *out = settings;
  pthread_mutex_unlock(&state_lock);
}

DXC_STATE dxcluster_get_state(void) {
  DXC_STATE s;
  pthread_mutex_lock(&state_lock);
  s = state;
  pthread_mutex_unlock(&state_lock);
  return s;
}

int dxcluster_spots_received_this_session(void) {
  int n;
  pthread_mutex_lock(&state_lock);
  n = session_spots;
  pthread_mutex_unlock(&state_lock);
  return n;
}

static int dxcluster_query_spots(DX_SPOT *out, int max_out,
                                 long long lo_hz, long long hi_hz) {
  if (!out || max_out <= 0) { return 0; }
  pthread_mutex_lock(&state_lock);
  DXC_SETTINGS s = settings;
  if (!s.show_on_panadapter || !s.enabled) {
    pthread_mutex_unlock(&state_lock);
    return 0;
  }
  time_t now = time(NULL);
  /* Walk ring buffer newest first, output spots passing filters + range */
  int count = 0;
  int idx = (ring_head - 1 + DXC_MAX_SPOTS) % DXC_MAX_SPOTS;
  for (int i = 0; i < ring_count && count < max_out; i++) {
    const DX_SPOT *sp = &ring[idx];
    if (sp->freq_hz >= lo_hz && sp->freq_hz <= hi_hz &&
        spot_passes_filters(sp, &s, now)) {
      out[count++] = *sp;
    }
    idx = (idx - 1 + DXC_MAX_SPOTS) % DXC_MAX_SPOTS;
  }
  pthread_mutex_unlock(&state_lock);
  return count;
}

void dxcluster_save_state(void) {
  DXC_SETTINGS s;
  pthread_mutex_lock(&state_lock);
  s = settings;
  pthread_mutex_unlock(&state_lock);
  SetPropI0("dxcluster.enabled",            s.enabled);
  SetPropS0("dxcluster.server",             s.server);
  SetPropI0("dxcluster.port",               s.port);
  SetPropS0("dxcluster.callsign",           s.callsign);
  SetPropI0("dxcluster.auto_reconnect",     s.auto_reconnect);
  SetPropI0("dxcluster.show_on_panadapter", s.show_on_panadapter);
  SetPropI0("dxcluster.age_limit_sec",      s.age_limit_sec);
  SetPropI0("dxcluster.mode_ft8",           s.mode_ft8);
  SetPropI0("dxcluster.mode_ft4",           s.mode_ft4);
  SetPropI0("dxcluster.mode_cw",            s.mode_cw);
  SetPropI0("dxcluster.mode_ssb",           s.mode_ssb);
  SetPropI0("dxcluster.mode_rtty",          s.mode_rtty);
  SetPropI0("dxcluster.mode_other",         s.mode_other);
  SetPropI0("dxcluster.region_na",          s.region_na);
  SetPropI0("dxcluster.region_eu",          s.region_eu);
  SetPropI0("dxcluster.region_as",          s.region_as);
  SetPropI0("dxcluster.region_sa",          s.region_sa);
  SetPropI0("dxcluster.region_af",          s.region_af);
  SetPropI0("dxcluster.region_oc",          s.region_oc);
  SetPropS0("dxcluster.whitelist",          s.whitelist);
  SetPropS0("dxcluster.blacklist",          s.blacklist);
}

void dxcluster_restore_state(void) {
  DXC_SETTINGS s;
  set_default_settings(&s);
  GetPropI0("dxcluster.enabled",            s.enabled);
  GetPropS0("dxcluster.server",             s.server);
  GetPropI0("dxcluster.port",               s.port);
  GetPropS0("dxcluster.callsign",           s.callsign);
  GetPropI0("dxcluster.auto_reconnect",     s.auto_reconnect);
  GetPropI0("dxcluster.show_on_panadapter", s.show_on_panadapter);
  GetPropI0("dxcluster.age_limit_sec",      s.age_limit_sec);
  GetPropI0("dxcluster.mode_ft8",           s.mode_ft8);
  GetPropI0("dxcluster.mode_ft4",           s.mode_ft4);
  GetPropI0("dxcluster.mode_cw",            s.mode_cw);
  GetPropI0("dxcluster.mode_ssb",           s.mode_ssb);
  GetPropI0("dxcluster.mode_rtty",          s.mode_rtty);
  GetPropI0("dxcluster.mode_other",         s.mode_other);
  GetPropI0("dxcluster.region_na",          s.region_na);
  GetPropI0("dxcluster.region_eu",          s.region_eu);
  GetPropI0("dxcluster.region_as",          s.region_as);
  GetPropI0("dxcluster.region_sa",          s.region_sa);
  GetPropI0("dxcluster.region_af",          s.region_af);
  GetPropI0("dxcluster.region_oc",          s.region_oc);
  GetPropS0("dxcluster.whitelist",          s.whitelist);
  GetPropS0("dxcluster.blacklist",          s.blacklist);
  /* Sanity: restore defaults if persisted values are obviously bogus.
   * GetPropS0 happily overwrites a default with "" if the props file has
   * an empty entry — defend against that for fields where empty makes
   * no sense. */
  if (s.server[0] == '\0') {
    snprintf(s.server, sizeof(s.server), "%s", "ve7cc.net");
  }
  if (s.port <= 0 || s.port > 65535) {
    s.port = 23;
  }
  if (s.age_limit_sec < 60 || s.age_limit_sec > 86400) {
    s.age_limit_sec = 600;
  }
  pthread_mutex_lock(&state_lock);
  settings = s;
  settings_initialised = 1;
  pthread_mutex_unlock(&state_lock);
}

/* ── Panadapter draw + hit-test ───────────────────────────────────────── */
#include <cairo.h>

/* Each rendered group on the current frame. Stored as flat list — at most
 * a few dozen entries per frame on a busy band. */
#define DXC_DRAW_CACHE_MAX  128
typedef struct {
  int       rx_id;
  int       x;          /* pixel x position of the triangle tip */
  int       y_top;      /* y of the label text */
  int       n_spots;
  DX_SPOT   spots[DXC_MAX_GROUP];
} DRAW_ENTRY;
static DRAW_ENTRY draw_cache[DXC_DRAW_CACHE_MAX];
static int        draw_cache_n[2] = {0, 0};   /* one per RX */
static pthread_mutex_t draw_lock = PTHREAD_MUTEX_INITIALIZER;

void dxcluster_draw_spots(cairo_t *cr, int rx_id,
                          long long frequency,
                          double cAp, double cBp,
                          int width, int height) {
  if (!cr) { return; }
  /* Pull current settings + spots */
  pthread_mutex_lock(&state_lock);
  DXC_SETTINGS s = settings;
  if (!s.show_on_panadapter || !s.enabled) {
    pthread_mutex_unlock(&state_lock);
    return;
  }
  pthread_mutex_unlock(&state_lock);
  /* Compute visible frequency window in Hz at the panadapter edges.
   *   x = cBp + (freq - frequency) * cAp
   *  => freq_at_x = frequency + (x - cBp) / cAp
   * Visible range is x=0 to x=width. */
  if (cAp == 0.0) { return; }
  long long lo_hz = frequency + (long long)((0.0    - cBp) / cAp);
  long long hi_hz = frequency + (long long)((width  - cBp) / cAp);
  if (lo_hz > hi_hz) {
    long long tmp = lo_hz;
    lo_hz = hi_hz;
    hi_hz = tmp;
  }
  DX_SPOT visible[DXC_MAX_SPOTS];
  int n = dxcluster_query_spots(visible, DXC_MAX_SPOTS, lo_hz, hi_hz);
  if (n == 0) { return; }
  /* Layout: bucket spots by pixel x position, group if within 30px */
  typedef struct {
    int      x;
    DX_SPOT  list[DXC_MAX_GROUP];
    int      count;
    int      newest_idx;
    time_t   newest_ts;
  } LAY;
  LAY *lay = g_new0(LAY, n + 1);
  int n_groups = 0;
  for (int i = 0; i < n; i++) {
    int x = (int)(cBp + (visible[i].freq_hz - frequency) * cAp + 0.5);
    if (x < 0 || x >= width) { continue; }
    /* Find an existing group within 30px */
    int g = -1;
    for (int k = 0; k < n_groups; k++) {
      if (abs(lay[k].x - x) <= 30) { g = k; break; }
    }
    if (g < 0) {
      g = n_groups++;
      lay[g].x = x;
      lay[g].count = 0;
      lay[g].newest_ts = 0;
    }
    if (lay[g].count < DXC_MAX_GROUP) {
      lay[g].list[lay[g].count] = visible[i];
      if (visible[i].when > lay[g].newest_ts) {
        lay[g].newest_ts = visible[i].when;
        lay[g].newest_idx = lay[g].count;
      }
      lay[g].count++;
    }
  }
  /* Update click cache */
  if (rx_id >= 0 && rx_id < 2) {
    pthread_mutex_lock(&draw_lock);
    int base = (rx_id == 0) ? 0 : DXC_DRAW_CACHE_MAX / 2;
    int cap  = DXC_DRAW_CACHE_MAX / 2;
    int kept = 0;
    for (int g = 0; g < n_groups && kept < cap; g++) {
      DRAW_ENTRY *e = &draw_cache[base + kept];
      e->rx_id = rx_id;
      e->x     = lay[g].x;
      e->y_top = 4;
      e->n_spots = lay[g].count;
      for (int k = 0; k < lay[g].count && k < DXC_MAX_GROUP; k++) {
        e->spots[k] = lay[g].list[k];
      }
      kept++;
    }
    draw_cache_n[rx_id] = kept;
    pthread_mutex_unlock(&draw_lock);
  }
  /* Draw */
  cairo_save(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 10.0);
  time_t now = time(NULL);
  for (int g = 0; g < n_groups; g++) {
    LAY *L = &lay[g];
    if (L->count == 0) { continue; }
    /* Use newest spot in the group for label + age */
    const DX_SPOT *sp = &L->list[L->newest_idx];
    long age = now - L->newest_ts;
    /* Age-based opacity: full ≤5min → 40% by 30min, 0% at age limit */
    double alpha = 1.0;
    if (age > 300) {
      alpha = 1.0 - 0.6 * ((double)(age - 300) / 1500.0);
      if (alpha < 0.4) { alpha = 0.4; }
    }
    /* Own callsign override */
    int is_own = (s.callsign[0] &&
                  strcasecmp(sp->dx_call, s.callsign) == 0);
    /* Choose colour. Default is bright yellow; brighter for stacked groups;
     * a warmer gold for own callsign. */
    double r = 1.0, gc = 0.92, bc = 0.20;       /* yellow */
    if (is_own)            { r = 1.0;  gc = 0.78; bc = 0.20; alpha = 1.0; }
    else if (L->count > 1) { r = 1.0;  gc = 1.0;  bc = 0.40; }  /* paler yellow */
    /* Label text */
    char label[24];
    if (L->count > 1) { snprintf(label, sizeof(label), "%d spots", L->count); }
    else { snprintf(label, sizeof(label), "%s", sp->dx_call); }
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);
    double label_x = (double)L->x - te.width * 0.5;
    /* Placed at y=30 to clear the panadapter's frequency labels (drawn at y≈10) */
    double label_y = 30.0;
    //
    // Coloring the DX spot is so involved here that we keep this and
    // do not use COLOUR_DXSPOT
    //
    cairo_set_source_rgba(cr, r, gc, bc, alpha);
    cairo_move_to(cr, label_x, label_y);
    cairo_show_text(cr, label);
    /* Triangle: tip at (x, 36), base 5px wide at y=32 */
    cairo_move_to(cr, L->x,     36);
    cairo_line_to(cr, L->x - 4, 32);
    cairo_line_to(cr, L->x + 4, 32);
    cairo_close_path(cr);
    cairo_fill(cr);
    /* Faint vertical line down to spectrum for stacked groups */
    if (L->count > 1) {
      cairo_set_source_rgba(cr, r, gc, bc, alpha * 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, L->x, 36);
      cairo_line_to(cr, L->x, height - 16);
      cairo_stroke(cr);
    }
  }
  cairo_restore(cr);
  g_free(lay);
}

int dxcluster_hit_test(int rx_id, int click_x, int click_y,
                       DX_SPOT *out_spots, int *out_n_spots) {
  if (!out_spots || !out_n_spots) { return 0; }
  if (rx_id < 0 || rx_id > 1) { return 0; }
  pthread_mutex_lock(&draw_lock);
  int base = (rx_id == 0) ? 0 : DXC_DRAW_CACHE_MAX / 2;
  int n    = draw_cache_n[rx_id];
  int found = 0;
  /* Hit-area: x within ±8px of triangle, y within 0..22 (label + tri zone) */
  for (int i = 0; i < n; i++) {
    DRAW_ENTRY *e = &draw_cache[base + i];
    if (abs(e->x - click_x) <= 8 && click_y >= 18 && click_y <= 40) {
      *out_n_spots = e->n_spots;
      for (int k = 0; k < e->n_spots; k++) {
        out_spots[k] = e->spots[k];
      }
      found = 1;
      break;
    }
  }
  pthread_mutex_unlock(&draw_lock);
  return found;
}
