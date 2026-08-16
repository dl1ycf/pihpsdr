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

#include "atomic.h"

#ifndef _BUFFER_H_
#define _BUFFER_H_

#define P2_BUFFER_SIZE    1500

#define METIS_BUFFER_SIZE 1032
struct metisbuffer_ {
  struct metisbuffer_ *next;
  volatile atomic_int free;
  unsigned char buffer[METIS_BUFFER_SIZE];
};

typedef struct metisbuffer_ metisbuffer;

#ifdef USBOZY
#define EP6_BUFFER_SIZE   2048
struct ozybuffer_ {
  struct ozybuffer_ *next;
  volatile atomic_int free;
  unsigned char buffer[EP6_BUFFER_SIZE];
};

typedef struct ozybuffer_ ozybuffer;
#endif

#define P2_BUFFER_SIZE    1500
struct p2buffer_ {
  struct p2buffer_ *next;
  volatile atomic_int free;
  unsigned char buffer[P2_BUFFER_SIZE];
};

typedef struct p2buffer_ p2buffer;

extern metisbuffer *get_metisbuffer(void);
#ifdef USBOZY
extern ozybuffer *get_ozybuffer(void);
#endif
extern p2buffer *get_p2buffer(void);

extern void mark_p2buffers_free(void);

#endif
