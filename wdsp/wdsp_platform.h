/*  wdsp_platform.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2026 Warren Pratt, NR0V

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

warren@wpratt.com

*/

/*  WHAT THIS IS

    The one place where WDSP chooses the operating system.  comm.h includes it
    first, ahead of everything else, so every WDSP source file sees it.

    WDSP is written in the Win32 vocabulary:  critical sections, semaphores,
    events, _beginthread(), the Interlocked functions, Sleep(), and so on.
    That vocabulary stays the library's internal API on every platform.

    WINDOWS.  This header includes exactly the system headers that comm.h
    included through 2.11, in the same order, and defines PORT exactly as
    comm.h did.  The only additions are the two small flush-to-zero functions
    below.  Neither WDSP nor ChannelMaster (which includes comm.h) sees anything
    else change.

    LINUX AND macOS.  This header supplies the same Win32 names, mapped onto
    POSIX threads.  The few that need code are implemented in
    wdsp_platform_posix.c, which is built only on these platforms (by CMake);
    it is not part of wdsp.vcxproj.  Nothing else in WDSP needs an #ifdef.

    [The implementations are named wdsp_xxx, and the Win32 names are macros
    that call them.  So libwdsp.so never defines a symbol named Sleep or
    _beginthread, and cannot collide with a console that has its own versions
    of these (as the existing Linux ports do).  They are also marked hidden,
    so they are never exported, whatever visibility the build uses.]

    THIS HEADER IS INTERNAL.  Consoles use wdsp.h, which needs nothing from
    here.

    WHERE THE POSIX VERSIONS DIFFER FROM WINDOWS

        thread priority     AvSetMmThreadCharacteristics() ("Pro Audio") and
                            SetThreadPriority() do nothing.  [Real-time
                            scheduling on Linux needs privileges that a
                            console usually doesn't have, and the existing
                            Linux ports don't set it either.]

        QueueUserWorkItem   Windows runs each work item on a thread from a
                            pool;  here each item gets a new, detached thread,
                            as in the existing Linux ports.  The analyzer is
                            the only user:  one item per FFT.

        console             AllocConsole() and FreeConsole() do nothing, and
                            freopen_s() leaves stdout alone when asked to
                            point it at "conout$" (wisdom.c does this for its
                            progress messages).  stdout already goes to the
                            terminal.

        OutputDebugStringA  writes to stderr, but only when the environment
                            variable WDSP_DEBUG is set (to anything).  [It is
                            how dprintf() reaches the Visual Studio Output
                            window on Windows, which a user doesn't see.  So
                            a console started from a terminal isn't filled
                            with WDSP's debugging messages (the NNR models
                            report themselves at every OpenChannel ()).]

        dprintf             POSIX has its own dprintf (int fd, ...), so WDSP's
                            is renamed to wdsp_dprintf by a macro.  The
                            spelling in the source is unchanged.
*/

#ifndef _wdsp_platform_h
#define _wdsp_platform_h

#if defined(_WIN32) || defined(_WIN64)

/********************************************************************************************************
*																										*
*												Windows													*
*																										*
********************************************************************************************************/

// [The same headers, in the same order, that comm.h included through 2.11.  windows.h is
// spelled in lower case, as MinGW's is, so that WDSP can also be cross-compiled for
// Windows on Linux, where file names are case-sensitive.  Windows itself ignores case.]
#include <windows.h>
#include <process.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <avrt.h>
#include <assert.h>

// PORT marks a function that the DLL exports.  [Spelled exactly as before, and as
// in ChannelMaster's obbuffs.h, so the two definitions never conflict.]
#ifndef PORT
#define PORT							__declspec( dllexport )
#endif

// FLUSH-TO-ZERO.  The processor's flush-to-zero mode is a per-thread setting (bit 15
// of MXCSR on x86/x64), and a new thread starts with it off.  Each DSP thread turns
// it on for itself, at its start.
//
//		flush_to_zero_on ()		turns it on for the calling thread;  returns 1 if it
//								was already on, 0 if it was off
//		flush_to_zero_off ()	turns it off for the calling thread
//
// [A thread that WDSP creates just calls flush_to_zero_on ().  A work item runs on a
// thread that belongs to the system's thread pool, so it turns the mode off again
// at its end if it was off at its start, and leaves the thread as it found it.]
//
// [Through 2.11, OpenChannel() set it, which affected only the thread that called
// OpenChannel(), not the DSP threads.]
//
// [static __inline functions rather than macros, so a call whose result isn't used
// draws no warning.  _mm_getcsr () and _mm_setcsr () come from <intrin.h>.]
static __inline int flush_to_zero_on (void)
{
	unsigned int csr = _mm_getcsr ();
	_mm_setcsr (csr | _MM_FLUSH_ZERO_ON);
	return (csr & _MM_FLUSH_ZERO_MASK) != 0;
}

static __inline void flush_to_zero_off (void)
{
	_mm_setcsr (_mm_getcsr () & ~_MM_FLUSH_ZERO_MASK);
}

#else

/********************************************************************************************************
*																										*
*										POSIX (Linux, macOS)											*
*																										*
********************************************************************************************************/

// [On Windows, Windows.h and the CRT headers bring these in.]
#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "wdsp_align.h"									// _aligned_malloc (), _aligned_free ()

// The implementations in wdsp_platform_posix.c are never exported.
#define WDSP_INTERNAL					__attribute__ ((visibility ("hidden")))

// ---------------------------------------- export -----------------------------------------------------

// PORT marks a function that the library exports.  [A build with -fvisibility=hidden
// then exports exactly the functions that the Windows DLL exports.]
#ifndef PORT
#define PORT							__attribute__ ((visibility ("default")))
#endif

// __declspec (x) becomes __wdsp_declspec_x, so the existing spellings work unchanged.
// [WDSP uses __declspec (align (16)) on a few arrays.]
#define __declspec(x)					__wdsp_declspec_##x
#define __wdsp_declspec_dllexport		__attribute__ ((visibility ("default")))
#define __wdsp_declspec_dllimport
#define __wdsp_declspec_align(n)		__attribute__ ((aligned (n)))

// ---------------------------------------- types and keywords -----------------------------------------

typedef void*							HANDLE;
typedef uint32_t						DWORD;
typedef long							LONG;
typedef int								BOOL;
typedef unsigned char					byte;

#ifndef TRUE
#define TRUE							1
#endif
#ifndef FALSE
#define FALSE							0
#endif

#define TEXT(s)							s
#define WINAPI
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif

// [Windows.h defines these as macros (unless NOMINMAX is defined).]
#ifndef min
#define min(a, b)						(((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)						(((a) > (b)) ? (a) : (b))
#endif

// ---------------------------------------- critical sections ------------------------------------------

// A Win32 critical section is recursive:  the thread that holds it may enter it
// again.  So it is a recursive mutex here.  [The spin count is a Windows tuning
// hint, and is ignored.]
typedef pthread_mutex_t					CRITICAL_SECTION, *LPCRITICAL_SECTION;

WDSP_INTERNAL void wdsp_InitializeCriticalSection (CRITICAL_SECTION* cs);
WDSP_INTERNAL BOOL wdsp_InitializeCriticalSectionAndSpinCount (CRITICAL_SECTION* cs, DWORD spin);

#define InitializeCriticalSection(cs)						wdsp_InitializeCriticalSection (cs)
#define InitializeCriticalSectionAndSpinCount(cs, spin)		wdsp_InitializeCriticalSectionAndSpinCount ((cs), (spin))
#define EnterCriticalSection(cs)							pthread_mutex_lock (cs)
#define LeaveCriticalSection(cs)							pthread_mutex_unlock (cs)
#define DeleteCriticalSection(cs)							pthread_mutex_destroy (cs)

// ---------------------------------------- interlocked operations -------------------------------------

// Each is a full memory barrier and returns what the Win32 function returns:
//		InterlockedIncrement, InterlockedDecrement		the new value
//		InterlockedExchange, InterlockedAnd				the old value
//		InterlockedBitTestAndSet / AndReset				the old value of the bit
// [The compiler's __atomic built-ins take any integer type, so they work on WDSP's
// long, LONG and int flags alike.  long is 64 bits here and 32 bits on Windows.
// That would matter only to InterlockedAnd (p, 0xFFFFFFFF), which WDSP uses to read
// a few counters (underflows, overflows) and flags;  those are never negative, so it
// doesn't.]
#define InterlockedIncrement(p)				__atomic_add_fetch ((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p)				__atomic_sub_fetch ((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v)			__atomic_exchange_n ((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedAnd(p, m)				__atomic_fetch_and ((p), (m), __ATOMIC_SEQ_CST)
#define _InterlockedAnd(p, m)				__atomic_fetch_and ((p), (m), __ATOMIC_SEQ_CST)
#define InterlockedBitTestAndSet(p, b)		wdsp_bit (__atomic_fetch_or  ((p),  (1L << (b)), __ATOMIC_SEQ_CST), (b))
#define InterlockedBitTestAndReset(p, b)	wdsp_bit (__atomic_fetch_and ((p), ~(1L << (b)), __ATOMIC_SEQ_CST), (b))

// [Bit b of the old value.  A function, not an expression in the macros above, so
// that WDSP's many calls that don't use the result draw no "value computed is not
// used" warning.]
static inline int wdsp_bit (long old_value, long b)
{
	return (int)((old_value >> b) & 1);
}

// ---------------------------------------- semaphores, events, waits ----------------------------------

// What WDSP uses:
//		CreateSemaphore, CreateSemaphoreW	counting semaphores (maximum 1000) and binary ones (maximum 1)
//		CreateEvent							auto-reset events
//		ReleaseSemaphore, SetEvent, ResetEvent, CloseHandle
//		WaitForSingleObject					timeouts INFINITE, 0, 1 ms, 500 ms, and a caller-supplied value
//		WaitForMultipleObjects				wait-any, INFINITE, on five binary semaphores (calcc.c)
//
// Each object is a mutex, a condition variable and a count, so all of these behave
// as on Windows, including a semaphore's maximum count and an event's auto-reset.
// No wait polls.  [macOS has neither unnamed POSIX semaphores nor sem_timedwait (),
// so sem_t is not used.]  Names, security attributes and manual-reset events are
// accepted and ignored or handled as Win32 does;  see wdsp_platform_posix.c.

#define INFINITE						0xFFFFFFFFu
#define WAIT_OBJECT_0					0x00000000u
#define WAIT_TIMEOUT					0x00000102u
#define WAIT_FAILED						0xFFFFFFFFu

WDSP_INTERNAL HANDLE	wdsp_CreateSemaphore		(long initial_count, long maximum_count);
WDSP_INTERNAL HANDLE	wdsp_CreateEvent			(int manual_reset, int initial_state);
WDSP_INTERNAL BOOL		wdsp_ReleaseSemaphore		(HANDLE h, long release_count, long* previous_count);
WDSP_INTERNAL BOOL		wdsp_SetEvent				(HANDLE h);
WDSP_INTERNAL BOOL		wdsp_ResetEvent				(HANDLE h);
WDSP_INTERNAL DWORD		wdsp_WaitForSingleObject	(HANDLE h, DWORD ms);
WDSP_INTERNAL DWORD		wdsp_WaitForMultipleObjects	(DWORD count, HANDLE const* h, BOOL wait_all, DWORD ms);
WDSP_INTERNAL BOOL		wdsp_CloseHandle			(HANDLE h);

#define CreateSemaphore(attr, init, max, name)		wdsp_CreateSemaphore ((init), (max))
#define CreateSemaphoreW(attr, init, max, name)		wdsp_CreateSemaphore ((init), (max))
#define CreateEvent(attr, manual, init, name)		wdsp_CreateEvent ((manual), (init))
#define ReleaseSemaphore(h, n, prev)				wdsp_ReleaseSemaphore ((h), (n), (prev))
#define SetEvent(h)									wdsp_SetEvent (h)
#define ResetEvent(h)								wdsp_ResetEvent (h)
#define WaitForSingleObject(h, ms)					wdsp_WaitForSingleObject ((h), (DWORD)(ms))
#define WaitForMultipleObjects(n, h, all, ms)		wdsp_WaitForMultipleObjects ((n), (HANDLE const*)(h), (all), (DWORD)(ms))
#define CloseHandle(h)								wdsp_CloseHandle (h)

// ---------------------------------------- threads ----------------------------------------------------

// _beginthread () starts a detached thread, as on Windows, and returns -1 if it
// can't.  [A stack_size of 0 means at least 1 MB, the Windows default.  macOS
// gives a new thread only 512 KB.]
WDSP_INTERNAL uintptr_t	wdsp_beginthread				(void (*start) (void*), unsigned stack_size, void* arg);
WDSP_INTERNAL BOOL		wdsp_SetThreadPriority			(HANDLE thread, int priority);
WDSP_INTERNAL HANDLE	wdsp_AvSetMmThreadCharacteristics	(const char* task, DWORD* task_index);
WDSP_INTERNAL BOOL		wdsp_AvSetMmThreadPriority		(HANDLE h, int priority);
WDSP_INTERNAL BOOL		wdsp_AvRevertMmThreadCharacteristics	(HANDLE h);
WDSP_INTERNAL BOOL		wdsp_QueueUserWorkItem			(DWORD (*fn) (void*), void* context, unsigned long flags);

#define _beginthread(start, stack_size, arg)		wdsp_beginthread ((start), (stack_size), (arg))
#define _endthread()								pthread_exit (NULL)
#define GetCurrentThread()							((HANDLE)(uintptr_t)pthread_self ())
#define THREAD_PRIORITY_HIGHEST						2
#define SetThreadPriority(thread, priority)			wdsp_SetThreadPriority ((thread), (priority))
#define AvSetMmThreadCharacteristics(task, index)	wdsp_AvSetMmThreadCharacteristics ((task), (index))
#define AvSetMmThreadPriority(h, priority)			wdsp_AvSetMmThreadPriority ((h), (priority))
#define AvRevertMmThreadCharacteristics(h)			wdsp_AvRevertMmThreadCharacteristics (h)
#define QueueUserWorkItem(fn, context, flags)		wdsp_QueueUserWorkItem ((fn), (context), (flags))

// ---------------------------------------- time, console, debugging output ----------------------------

WDSP_INTERNAL void		wdsp_Sleep				(DWORD ms);
WDSP_INTERNAL void		wdsp_OutputDebugStringA	(const char* s);
WDSP_INTERNAL BOOL		wdsp_AllocConsole		(void);
WDSP_INTERNAL BOOL		wdsp_FreeConsole		(void);
WDSP_INTERNAL int		wdsp_freopen_s			(FILE** pf, const char* name, const char* mode, FILE* stream);

#define Sleep(ms)									wdsp_Sleep (ms)
#define OutputDebugStringA(s)						wdsp_OutputDebugStringA (s)
#define AllocConsole()								wdsp_AllocConsole ()
#define FreeConsole()								wdsp_FreeConsole ()
#define freopen_s(pf, name, mode, stream)			wdsp_freopen_s ((pf), (name), (mode), (stream))

// [After <stdio.h>, which declares POSIX's own dprintf.]
#define dprintf										wdsp_dprintf

// ---------------------------------------- flush-to-zero ----------------------------------------------

// As on Windows (see above):  flush_to_zero_on () turns the mode on for the calling
// thread and returns 1 if it was already on;  flush_to_zero_off () turns it off.
// [x86/x64:  MXCSR bit 15.  ARM64 (Apple Silicon, 64-bit Raspberry Pi):  FPCR bit
// 24.  32-bit ARM with VFP:  FPSCR bit 24.  On ARM the one bit also treats denormal
// inputs as zero.  Any other processor:  nothing is done, and flush_to_zero_on ()
// returns 1, so nothing is "restored" either.]
WDSP_INTERNAL int		wdsp_flush_to_zero_on	(void);
WDSP_INTERNAL void		wdsp_flush_to_zero_off	(void);

#define flush_to_zero_on()							wdsp_flush_to_zero_on ()
#define flush_to_zero_off()							wdsp_flush_to_zero_off ()

#endif

#endif
