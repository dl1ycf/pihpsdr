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

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This file contains all code for dynamic buffer allocation/release          //
// management. The buffers are all similar, but contain data blocks           //
// of different size (P1 Metis: 1032 bytes, P1 Ozy: 2048 bytes,               //
// P2: 1500 bytes).                                                           //
//                                                                            //
// Buffers are allocated once and stay alive during the whole duration        //
// of the program. They are put in a linear linked list, such that            //
// the search for a free buffer can take much time (cycling through           //
// the whole list). But this seemingly dumb method has two advantages:        //
// - no malloc/free/mutex/atomic test-and-set during allocating and release,  //
// - in normal operation, when only few buffers are used, the same            //
//   buffers are used again and again so they stay in L1 cache                //
//                                                                            //
// To address possible problems with the "O(N)" cycling through the           //
// buffer list, each buffer lists exists as eight siblings which are          //
// used in a round-robin fashion.                                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "buffer.h"
#include "main.h"
#include "message.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                             METIS buffers                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static metisbuffer *metisbuflist[8];
static unsigned int nummetisbuf;
static unsigned int metisbufcnt = 7;

metisbuffer *get_metisbuffer(void) {
  //
  // Allocate a buffer from the METIS buffer queue
  //
  metisbufcnt = (metisbufcnt + 1) & 7;
  metisbuffer *bp = metisbuflist[metisbufcnt];
  while (bp) {
    if (bp->free) {
      bp->free = 0;
      return bp;
    }
    bp = bp->next;
  }
  // no free buffer found. Allocate 64 new ones
  for (unsigned int i = 0; i < 64; i++) {
    bp = g_new(metisbuffer, 1);
    if (!bp) {
      fatal_error("FATAL: P1: out of memory");
    } else {
      bp->free = 1;
      bp->next = metisbuflist[metisbufcnt];
      metisbuflist[metisbufcnt] = bp;
      nummetisbuf++;
    }
  }
  t_print("%s(%d): number of METIS buffers increased to %u\n",
          __func__, metisbufcnt, nummetisbuf);
  metisbuflist[metisbufcnt]->free = 0;
  return metisbuflist[metisbufcnt];
}

#ifdef USBOZY
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                            USB OZY buffers                                 //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static ozybuffer *ozybuflist[8];
static unsigned int numozybuf;
static unsigned int ozybufcnt = 7;

ozybuffer *get_ozybuffer(void) {
  //
  // Allocate a buffer from the OZY buffer queue
  //
  ozybufcnt = (ozybufcnt + 1) & 7;
  ozybuffer *bp = ozybuflist[ozybufcnt];
  while (bp) {
    if (bp->free) {
      bp->free = 0;
      return bp;
    }
    bp = bp->next;
  }
  // no free buffer found. Allocate 32 new ones
  for (unsigned int i = 0; i < 32; i++) {
    bp = g_new(ozybuffer, 1);
    if (!bp) {
      fatal_error("FATAL: P1: out of memory");
    } else {
      bp->free = 1;
      bp->next = ozybuflist[ozybufcnt];
      ozybuflist[ozybufcnt] = bp;
      numozybuf++;
    }
  }
  t_print("%s(%d): number of OZY buffers increased to %u\n",
          __func__, ozybufcnt, numozybuf);
  ozybuflist[ozybufcnt]->free = 0;
  return ozybuflist[ozybufcnt];
}

#endif

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                               P2 buffers                                   //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static p2buffer *p2buflist[8];
static unsigned int p2numbuf;
static unsigned int p2bufcnt = 7;

p2buffer *get_p2buffer(void) {
  //
  // Allocate a buffer from the P2 buffer queue
  //
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
  for (unsigned int i = 0; i < 64; i++) {
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

void prealloc_p2buffers(void) {
  //
  // Take care buffers are allocated in all buffer lists
  // To this end, allocate 8 buffers (they then come from
  // all buffer lists), then mark them free.
  //
  p2buffer *bp[8];
  for (unsigned int i = 0; i < 8; i++) {
    bp[i] = get_p2buffer();
  }
  for (unsigned int i = 0; i < 8; i++) {
    bp[i]->free = 1;
  }
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

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                             P2 XDMA buffers                                //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
//
// The XDMA code allocates buffers from three independend threads
// HighPrio, MicAudio, and a combined thread for all DDCs).
//
// To avoid thread synchronization (mutexes), different buffer pools
// must be used for the three cases. We use three different functions so we can
// do pre-allocation in different amounts.  // We allocate buffers in bunches of 64 for the DDC thread, and in smaller
// bunches of 16 for the HP and MicAudio thread. So the minimum number of
// buffers allocated at program start is 512 for DDC, 128 for HP, and 128 for MicAudio.
//
////////////////////////////////////////////////////////////////////////////////

static p2buffer *satrxbuflist[8];   // for XMDA DDC thread buffers
static p2buffer *sathpbuflist[8];   // for XDMA high-prio thread buffers
static p2buffer *satmicbuflist[8];  // for XDMA mic-audio thread buffers
static unsigned int satnumrxbuf;
static unsigned int satnumhpbuf;
static unsigned int satnummicbuf;
static unsigned int satrxbufcnt = 7;
static unsigned int sathpbufcnt = 7;
static unsigned int satmicbufcnt = 7;

p2buffer *get_satrxbuffer() {
  //
  // Allocate a buffer from the RX buffer queue
  //
  satrxbufcnt = (satrxbufcnt + 1) & 7;
  p2buffer *bp = satrxbuflist[satrxbufcnt];
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
      bp->next = satrxbuflist[satrxbufcnt];
      satrxbuflist[satrxbufcnt] = bp;
      satnumrxbuf++;
    }
  }
  t_print("%s(%d): number of RX buffers increased to %u\n",
          __func__, satrxbufcnt, satnumrxbuf);
  // Mark the first buffer in list as used and return that one.
  satrxbuflist[satrxbufcnt]->free = 0;
  return satrxbuflist[satrxbufcnt];

}

p2buffer *get_sathpbuffer() {
  //
  // Allocate a buffer from the HP buffer queue
  //
  sathpbufcnt = (sathpbufcnt + 1) & 7;
  p2buffer *bp = sathpbuflist[sathpbufcnt];
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
  for (int i = 0; i < 16; i++) {
    bp = g_new(p2buffer, 1);
    if (!bp) {
      fatal_error("FATAL: P2: out of memory");
    } else {
      bp->free = 1;
      bp->next = sathpbuflist[sathpbufcnt];
      sathpbuflist[sathpbufcnt] = bp;
      satnumhpbuf++;
    }
  }
  t_print("%s(%d): number of HP buffers increased to %u\n",
          __func__, sathpbufcnt, satnumhpbuf);
  // Mark the first buffer in list as used and return that one.
  sathpbuflist[sathpbufcnt]->free = 0;
  return sathpbuflist[sathpbufcnt];

}

p2buffer *get_satmicbuffer() {
  //
  // Allocate a buffer from the MIC buffer queue
  //
  satmicbufcnt = (satmicbufcnt + 1) & 7;
  p2buffer *bp = satmicbuflist[satmicbufcnt];
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
  for (int i = 0; i < 16; i++) {
    bp = g_new(p2buffer, 1);
    if (!bp) {
      fatal_error("FATAL: P2: out of memory");
    } else {
      bp->free = 1;
      bp->next = satmicbuflist[satmicbufcnt];
      satmicbuflist[satmicbufcnt] = bp;
      satnummicbuf++;
    }
  }
  t_print("%s(%d): number of MIC buffers increased to %u\n",
          __func__, satmicbufcnt, satnummicbuf);
  // Mark the first buffer in list as used and return that one.
  satmicbuflist[satmicbufcnt]->free = 0;
  return satmicbuflist[satmicbufcnt];
}

void prealloc_satbuffers(void) {
  //
  // Take care buffers are allocated in all buffer lists
  // To this end, allocate 8 buffers (they then come from
  // all buffer lists) then then mark them free.
  // This function does the job for all three buffer queues (RX, HP, MIC)
  //
  p2buffer *rxbuf[8], *hpbuf[8], *micbuf[8];
  for (unsigned int i = 0; i < 8; i++) {
    rxbuf[i] = get_satrxbuffer();
    hpbuf[i] = get_sathpbuffer();
    micbuf[i] = get_satmicbuffer();
  }
  for (unsigned int i = 0; i < 8; i++) {
    rxbuf[i]->free = 1;
    hpbuf[i]->free = 1;
    micbuf[i]->free = 1;
  }
}

void mark_satbuffers_free(void) {
  //
  // This must only be called if the threads allocating the buffers
  // are not running.
  // This function does the job for all three buffer queues (RX, HP, MIC)
  //
  p2buffer *bp;

  for (unsigned int i = 0; i < 8; i++) {
    bp = satrxbuflist[i];
    while (bp) {
      bp->free = 1;
      bp = bp->next;
    }
    bp = sathpbuflist[i];
    while (bp) {
      bp->free = 1;
      bp = bp->next;
    }
    bp = satmicbuflist[i];
    while (bp) {
      bp->free = 1;
      bp = bp->next;
    }
  }
}
