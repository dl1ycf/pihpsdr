/* Copyright (C)
*  2026 - Christoph van Wüllen, DL1YCF
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

//
// This file contains all code for dynamic buffer allocation/release
// management. The buffers are all similar, but contain data blocks
// of different size (P1 Metis: 1032 bytes, P1 Ozy: 2048 bytes,
// P2: 1500 bytes).
//
// Buffers are allocated once and stay alive during the whole duration
// of the program. They are put in a linear linked list, such that
// the search for a free buffer can take much time (cycling through 
// the whole list). But this seemingly dumb method has two advantages:
// - no malloc/free/mutex/atomic test-and-set during allocating and release,
// - in normal operation, when only few buffers are used, the same
//   buffers are used again and again so they stay in L1 cache
//
// To address possible problems with the "O(N)" cycling through the
// buffer list, each buffer lists exists as eight siblings which are
// used in a round-robin fashion.
//

#include "buffer.h"
#include "main.h"
#include "message.h"

static metisbuffer *metisbuflist[8];
static unsigned int nummetisbuf;
static unsigned int metisbufcnt = 7;

metisbuffer *get_metisbuffer(void) {
  metisbufcnt = (metisbufcnt + 1) & 7;
  metisbuffer *mb = metisbuflist[metisbufcnt];
  while (mb) {
    if (mb->free) {
      mb->free = 0;
      return mb;
    }
    mb = mb->next;
  }
  // no free buffer found. Allocate 64 new ones
  for (unsigned int i = 0; i < 64; i++) {
    mb = g_new(metisbuffer, 1);
    if (!mb) {
      fatal_error("FATAL: P1: out of memory");
    } else {
      mb->free = 1;
      mb->next = metisbuflist[metisbufcnt];
      metisbuflist[metisbufcnt] = mb;
      nummetisbuf++;
    }
  }
  t_print("%s(%d): number of METIS buffers increased to %u\n",
          __func__, metisbufcnt, nummetisbuf);
  metisbuflist[metisbufcnt]->free = 0;
  return metisbuflist[metisbufcnt];
}

#ifdef USBOZY
static ozybuffer *ozybuflist[8];
static unsigned int numozybuf;
static unsigned int ozybufcnt = 7;

ozybuffer *get_ozybuffer(void) {
  ozybufcnt = (ozybufcnt + 1) & 7;
  ozybuffer *ob = ozybuflist[ozybufcnt];
  while (ob) {
    if (ob->free) {
      ob->free = 0;
      return ob;
    }
    ob = ob->next;
  }
  // no free buffer found. Allocate 32 new ones
  for (unsigned int i = 0; i < 32; i++) {
    ob = g_new(ozybuffer, 1);
    if (!ob) {
      fatal_error("FATAL: P1: out of memory");
    } else {
      ob->free = 1;
      ob->next = ozybuflist[ozybufcnt];
      ozybuflist[ozybufcnt] = ob;
      numozybuf++;
    }
  }
  t_print("%s(%d): number of OZY buffers increased to %u\n",
          __func__, ozybufcnt, numozybuf);
  ozybuflist[ozybufcnt]->free = 0;
  return ozybuflist[ozybufcnt];
}

#endif

static p2buffer *p2buflist[8];
static unsigned int p2numbuf;
static unsigned int p2bufcnt = 7;

p2buffer *get_p2buffer(void) {
  p2bufcnt = (p2bufcnt + 1) & 7;
  p2buffer *bp = p2buflist[p2bufcnt];
  while (bp) {
    if (bp->free) {
      // found free buffer. Mark as used and return that one.
      bp->free = 0;
      return bp;
    }
    bp = bp->next;
  }
  //
  // no free buffer found, allocate a bunch of new ones
  // and add to the head of the list
  //
  for (int i = 0; i < 64; i++) {
    bp = g_new(p2buffer, 1);
    if (!bp) {
      fatal_error("FATAL: P2: out of memory");
    } else {
      bp->free = 1;
      bp->next = p2buflist[p2bufcnt];
      p2buflist[p2bufcnt] = bp;
      p2numbuf++;
    }
  }
  t_print("%s(%d): number of P2 buffers increased to %u\n",
          __func__, p2bufcnt, p2numbuf);
  // Mark the first buffer in list as used and return that one.
  p2buflist[p2bufcnt]->free = 0;
  return p2buflist[p2bufcnt];
}

void mark_p2buffers_free(void) {
  //
  // This must only be called if the thread calling get_p2buffer()
  // is not running
  //
  for (unsigned int i = 0; i < 8; i++) {
    p2buffer *bp = p2buflist[i];
    while (bp) {
      bp->free = 1;
      bp = bp->next;
    }
  }
}

