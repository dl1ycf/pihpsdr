/* Copyright (C)
* 2026 - Christoph van Wüllen, DL1YCF
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

#ifndef _ATOMIC_H_
#define _ATOMIC_H_

//
// Atomic updates. Memory barrier only used if atomic updates are not available
//
#if __STDC_VERSION__ < 201112L || __STDC_NO_ATOMICS__ == 1
  //
  // Macro for a memory barrier, preventing changing the execution order
  // or memory accesses. The MEMORY_BARRIER
  // pseudo-statements tells the compiler not to re-order memory
  // accesses across this point. This is used in ring buffer updates
  // if there are no atomic operations.A producer must place a MEMORY_BARRIER
  // after the buffer update and before the write pointer update, and
  // a consumer must place it after it has retrieved data from the buffer
  // and before it updates the read pointer.
  // This is not necessary if we use atomic updates since an atomic updates
  // has an implicit memory barrier.
  //
  #define MEMORY_BARRIER asm volatile ("" ::: "memory")
  #define atomic_int int
#else
  #define MEMORY_BARRIER
  #include <stdatomic.h>
#endif

#endif
