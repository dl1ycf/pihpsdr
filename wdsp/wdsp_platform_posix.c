/*  wdsp_platform_posix.c

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

/*  The POSIX (Linux, macOS) implementations of the Win32 functions that
    wdsp_platform.h maps.  See that header for what is here and why.

    This file is built only on Linux and macOS (by CMake).  It is not part of
    wdsp.vcxproj, and on Windows it compiles to nothing.  [It is the one WDSP
    source file that tests the platform, apart from wdsp_platform.h itself.]
*/

#if !defined(_WIN32) && !defined(_WIN64)

// [Feature-test macros must come before any system header.  POSIX.1-2008 covers
// everything here on Linux.  macOS needs _DARWIN_C_SOURCE instead, for
// pthread_cond_timedwait_relative_np ().]
#if defined(__APPLE__)
	#define _DARWIN_C_SOURCE
#elif !defined(_GNU_SOURCE) && !defined(_POSIX_C_SOURCE)
	#define _POSIX_C_SOURCE 200809L
#endif

// [Before wdsp_platform.h, so that its min/max and __declspec macros can't reach
// into a system header.]
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
	#include <xmmintrin.h>								// _mm_getcsr (), _mm_setcsr ()
#endif
#include <errno.h>
#include <sched.h>
#include <strings.h>									// strcasecmp ()

#include "wdsp_platform.h"

/********************************************************************************************************
*																										*
*											Critical Sections											*
*																										*
********************************************************************************************************/

void wdsp_InitializeCriticalSection (CRITICAL_SECTION* cs)
{
	pthread_mutexattr_t at;
	pthread_mutexattr_init (&at);
	pthread_mutexattr_settype (&at, PTHREAD_MUTEX_RECURSIVE);	// [Win32 critical sections are recursive]
	pthread_mutex_init (cs, &at);
	pthread_mutexattr_destroy (&at);
}

BOOL wdsp_InitializeCriticalSectionAndSpinCount (CRITICAL_SECTION* cs, DWORD spin)
{
	(void)spin;									// [a Windows tuning hint]
	wdsp_InitializeCriticalSection (cs);
	return TRUE;
}

/********************************************************************************************************
*																										*
*									Semaphores, Events and Waits										*
*																										*
********************************************************************************************************/

/*  Every semaphore and event is one of these:  a mutex, a condition variable
    and a count.  For an event, the count is 1 (signaled) or 0.

    WaitForSingleObject () waits on the object's own condition variable.

    WaitForMultipleObjects () can't wait on several condition variables at
    once, so the waiting thread has its own (a 'waiter') and registers it with
    each object it waits on.  ReleaseSemaphore () and SetEvent () then wake it
    directly.  No wait ever polls.

    Timed waits use CLOCK_MONOTONIC, so a change of the system clock doesn't
    shorten or lengthen them.

    Lock order is always object, then waiter.
*/

typedef struct _waiter							// one thread inside WaitForMultipleObjects ()
{
	pthread_mutex_t m;
	pthread_cond_t c;
	int fired;									// set by a release or set of any registered object
} waiter;

typedef struct _wlink							// registers a waiter with one of the objects it waits on
{
	waiter* w;
	struct _wlink* next;
} wlink;

typedef struct _wdsp_sync
{
	pthread_mutex_t m;
	pthread_cond_t c;
	long count;									// semaphore count;  1 or 0 for an event
	long max;									// semaphore maximum count;  1 for an event
	int is_event;
	int manual;									// manual-reset event:  a satisfied wait doesn't reset it
	int nwait;									// threads blocked in WaitForSingleObject () on this object
	wlink* multi;								// threads blocked in WaitForMultipleObjects () on this object
} wdsp_sync;

#define WDSP_MAXIMUM_WAIT_OBJECTS		64		// [as Win32's MAXIMUM_WAIT_OBJECTS]

static void cond_init (pthread_cond_t* c)
{
#if defined(__APPLE__)
	pthread_cond_init (c, NULL);				// [macOS can't set the clock;  relative timed waits are used instead]
#else
	pthread_condattr_t a;
	pthread_condattr_init (&a);
	pthread_condattr_setclock (&a, CLOCK_MONOTONIC);
	pthread_cond_init (c, &a);
	pthread_condattr_destroy (&a);
#endif
}

static struct timespec deadline_after (DWORD ms)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	t.tv_sec  += ms / 1000u;
	t.tv_nsec += (long)(ms % 1000u) * 1000000L;
	if (t.tv_nsec >= 1000000000L)
	{
		t.tv_sec++;
		t.tv_nsec -= 1000000000L;
	}
	return t;
}

// Returns ETIMEDOUT once the deadline has passed.  Any other return is a wake-up,
// possibly a spurious one.
static int cond_wait_until (pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* deadline)
{
#if defined(__APPLE__)
	struct timespec now, rel;
	clock_gettime (CLOCK_MONOTONIC, &now);
	rel.tv_sec  = deadline->tv_sec  - now.tv_sec;
	rel.tv_nsec = deadline->tv_nsec - now.tv_nsec;
	if (rel.tv_nsec < 0)
	{
		rel.tv_sec--;
		rel.tv_nsec += 1000000000L;
	}
	if (rel.tv_sec < 0) return ETIMEDOUT;
	return pthread_cond_timedwait_relative_np (c, m, &rel);
#else
	return pthread_cond_timedwait (c, m, deadline);
#endif
}

// Called with s->m held.  Takes one count (or, for an auto-reset event, resets it)
// if the object is signaled, and returns 1;  returns 0 if it isn't signaled.
static int try_take (wdsp_sync* s)
{
	if (s->count <= 0) return 0;
	if (!s->manual) s->count--;
	return 1;
}

// Called with s->m held, after the count has been raised by n.
static void wake (wdsp_sync* s, long n)
{
	wlink* l;
	if (s->nwait)
	{
		if (n == 1 && !s->manual) pthread_cond_signal (&s->c);
		else                      pthread_cond_broadcast (&s->c);
	}
	for (l = s->multi; l; l = l->next)
	{
		pthread_mutex_lock (&l->w->m);
		l->w->fired = 1;
		pthread_cond_signal (&l->w->c);
		pthread_mutex_unlock (&l->w->m);
	}
}

static wdsp_sync* make_sync (long count, long max, int is_event, int manual)
{
	wdsp_sync* s = (wdsp_sync*) calloc (1, sizeof (wdsp_sync));
	if (!s) return NULL;
	pthread_mutex_init (&s->m, NULL);
	cond_init (&s->c);
	s->count = count;
	s->max = max;
	s->is_event = is_event;
	s->manual = manual;
	return s;
}

HANDLE wdsp_CreateSemaphore (long initial_count, long maximum_count)
{
	if (maximum_count <= 0 || initial_count < 0 || initial_count > maximum_count) return NULL;
	return make_sync (initial_count, maximum_count, 0, 0);
}

HANDLE wdsp_CreateEvent (int manual_reset, int initial_state)
{
	return make_sync (initial_state ? 1 : 0, 1, 1, manual_reset ? 1 : 0);
}

BOOL wdsp_ReleaseSemaphore (HANDLE h, long release_count, long* previous_count)
{
	wdsp_sync* s = (wdsp_sync*) h;
	if (!s || s->is_event || release_count <= 0) return FALSE;
	pthread_mutex_lock (&s->m);
	if (s->count > s->max - release_count)		// [as Win32:  it fails, and the count is unchanged]
	{
		pthread_mutex_unlock (&s->m);
		return FALSE;
	}
	if (previous_count) *previous_count = s->count;
	s->count += release_count;
	wake (s, release_count);
	pthread_mutex_unlock (&s->m);
	return TRUE;
}

BOOL wdsp_SetEvent (HANDLE h)
{
	wdsp_sync* s = (wdsp_sync*) h;
	if (!s || !s->is_event) return FALSE;
	pthread_mutex_lock (&s->m);
	if (s->count == 0)
	{
		s->count = 1;
		wake (s, 1);
	}
	pthread_mutex_unlock (&s->m);
	return TRUE;
}

BOOL wdsp_ResetEvent (HANDLE h)
{
	wdsp_sync* s = (wdsp_sync*) h;
	if (!s || !s->is_event) return FALSE;
	pthread_mutex_lock (&s->m);
	s->count = 0;
	pthread_mutex_unlock (&s->m);
	return TRUE;
}

// [As on Windows, no thread may still be waiting on h.]
BOOL wdsp_CloseHandle (HANDLE h)
{
	wdsp_sync* s = (wdsp_sync*) h;
	if (!s) return FALSE;
	pthread_cond_destroy (&s->c);
	pthread_mutex_destroy (&s->m);
	free (s);
	return TRUE;
}

DWORD wdsp_WaitForSingleObject (HANDLE h, DWORD ms)
{
	wdsp_sync* s = (wdsp_sync*) h;
	struct timespec deadline;
	if (!s) return WAIT_FAILED;
	pthread_mutex_lock (&s->m);
	if (!try_take (s))
	{
		if (ms == 0)
		{
			pthread_mutex_unlock (&s->m);
			return WAIT_TIMEOUT;
		}
		if (ms != INFINITE) deadline = deadline_after (ms);
		s->nwait++;
		for (;;)
		{
			int rc = (ms == INFINITE) ? pthread_cond_wait (&s->c, &s->m)
			                          : cond_wait_until (&s->c, &s->m, &deadline);
			if (try_take (s)) break;
			if (rc == ETIMEDOUT)
			{
				s->nwait--;
				pthread_mutex_unlock (&s->m);
				return WAIT_TIMEOUT;
			}
		}
		s->nwait--;
	}
	pthread_mutex_unlock (&s->m);
	return WAIT_OBJECT_0;
}

// Wait-any, the only form WDSP uses (wait_all must be FALSE).  Returns WAIT_OBJECT_0
// plus the index of the object taken;  if several are signaled, the lowest-numbered
// one, as on Windows.
DWORD wdsp_WaitForMultipleObjects (DWORD count, HANDLE const* h, BOOL wait_all, DWORD ms)
{
	wdsp_sync* const* s = (wdsp_sync* const*) h;
	wlink lk[WDSP_MAXIMUM_WAIT_OBJECTS];
	waiter w;
	struct timespec deadline;
	DWORD i, result = WAIT_TIMEOUT;

	if (count == 0 || count > WDSP_MAXIMUM_WAIT_OBJECTS || wait_all) return WAIT_FAILED;
	for (i = 0; i < count; i++)
		if (!s[i]) return WAIT_FAILED;

	for (i = 0; i < count; i++)					// fast path:  something is already signaled
	{
		int got;
		pthread_mutex_lock (&s[i]->m);
		got = try_take (s[i]);
		pthread_mutex_unlock (&s[i]->m);
		if (got) return WAIT_OBJECT_0 + i;
	}
	if (ms == 0) return WAIT_TIMEOUT;

	pthread_mutex_init (&w.m, NULL);
	cond_init (&w.c);
	w.fired = 0;
	if (ms != INFINITE) deadline = deadline_after (ms);
	for (i = 0; i < count; i++)					// register with every object
	{
		pthread_mutex_lock (&s[i]->m);
		lk[i].w = &w;
		lk[i].next = s[i]->multi;
		s[i]->multi = &lk[i];
		pthread_mutex_unlock (&s[i]->m);
	}

	for (;;)
	{
		int timed_out = 0;
		for (i = 0; i < count; i++)				// anything released before (or while) we registered is seen here
		{
			int got;
			pthread_mutex_lock (&s[i]->m);
			got = try_take (s[i]);
			pthread_mutex_unlock (&s[i]->m);
			if (got)
			{
				result = WAIT_OBJECT_0 + i;
				goto done;
			}
		}
		pthread_mutex_lock (&w.m);
		while (!w.fired && !timed_out)
		{
			int rc = (ms == INFINITE) ? pthread_cond_wait (&w.c, &w.m)
			                          : cond_wait_until (&w.c, &w.m, &deadline);
			if (rc == ETIMEDOUT) timed_out = 1;
		}
		if (w.fired) timed_out = 0;				// [a release that raced the timeout still counts]
		w.fired = 0;
		pthread_mutex_unlock (&w.m);
		if (timed_out)
		{
			for (i = 0; i < count; i++)			// a last look before reporting the timeout
			{
				int got;
				pthread_mutex_lock (&s[i]->m);
				got = try_take (s[i]);
				pthread_mutex_unlock (&s[i]->m);
				if (got)
				{
					result = WAIT_OBJECT_0 + i;
					goto done;
				}
			}
			result = WAIT_TIMEOUT;
			goto done;
		}
	}

done:
	for (i = 0; i < count; i++)					// unregister;  no release can reach w after this
	{
		wlink** pp;
		pthread_mutex_lock (&s[i]->m);
		for (pp = &s[i]->multi; *pp && *pp != &lk[i]; pp = &(*pp)->next) ;
		if (*pp) *pp = lk[i].next;
		pthread_mutex_unlock (&s[i]->m);
	}
	pthread_cond_destroy (&w.c);
	pthread_mutex_destroy (&w.m);
	return result;
}

/********************************************************************************************************
*																										*
*										Threads and Work Items											*
*																										*
********************************************************************************************************/

typedef struct _thread_start
{
	void (*fn) (void*);
	void* arg;
} thread_start;

static void* thread_trampoline (void* p)
{
	thread_start ts = *(thread_start*)p;
	free (p);
	ts.fn (ts.arg);
	return NULL;
}

uintptr_t wdsp_beginthread (void (*start) (void*), unsigned stack_size, void* arg)
{
	const size_t min_stack = 1024 * 1024;		// [the Windows default]
	pthread_t t;
	pthread_attr_t at;
	size_t size = 0;
	thread_start* ts = (thread_start*) malloc (sizeof (thread_start));
	if (!ts) return (uintptr_t)-1;
	ts->fn = start;
	ts->arg = arg;
	pthread_attr_init (&at);
	pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
	if (stack_size)
		pthread_attr_setstacksize (&at, stack_size);		// [if it's too small, it fails and the default is used]
	else if (pthread_attr_getstacksize (&at, &size) == 0 && size < min_stack)
		pthread_attr_setstacksize (&at, min_stack);
	if (pthread_create (&t, &at, thread_trampoline, ts) != 0)
	{
		free (ts);
		pthread_attr_destroy (&at);
		return (uintptr_t)-1;
	}
	pthread_attr_destroy (&at);
	return (uintptr_t)t;
}

// Thread priority is left to the operating system;  see wdsp_platform.h.  [main.c
// asks for "Pro Audio" first, and falls back to SetThreadPriority () when that
// returns NULL.]
BOOL wdsp_SetThreadPriority (HANDLE thread, int priority)
{
	(void)thread;
	(void)priority;
	return TRUE;
}

HANDLE wdsp_AvSetMmThreadCharacteristics (const char* task, DWORD* task_index)
{
	(void)task;
	if (task_index) *task_index = 0;
	return NULL;
}

BOOL wdsp_AvSetMmThreadPriority (HANDLE h, int priority)
{
	(void)h;
	(void)priority;
	return TRUE;
}

BOOL wdsp_AvRevertMmThreadCharacteristics (HANDLE h)
{
	(void)h;
	return TRUE;
}

typedef struct _work_item
{
	DWORD (*fn) (void*);
	void* context;
} work_item;

static void work_trampoline (void* p)
{
	work_item wi = *(work_item*)p;
	free (p);
	wi.fn (wi.context);
}

// Each work item runs on a new, detached thread.  [Windows uses a thread pool.]
BOOL wdsp_QueueUserWorkItem (DWORD (*fn) (void*), void* context, unsigned long flags)
{
	work_item* wi = (work_item*) malloc (sizeof (work_item));
	(void)flags;
	if (!wi) return FALSE;
	wi->fn = fn;
	wi->context = context;
	if (wdsp_beginthread (work_trampoline, 0, wi) == (uintptr_t)-1)
	{
		free (wi);
		return FALSE;
	}
	return TRUE;
}

/********************************************************************************************************
*																										*
*									Time, Console, Debugging Output										*
*																										*
********************************************************************************************************/

void wdsp_Sleep (DWORD ms)
{
	struct timespec ts;
	if (ms == 0)								// [as Windows:  give up the rest of the time slice]
	{
		sched_yield ();
		return;
	}
	ts.tv_sec  = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	while (nanosleep (&ts, &ts) == -1 && errno == EINTR) ;
}

// To stderr, only when WDSP_DEBUG is set;  see wdsp_platform.h.  [Windows shows these
// messages only to a debugger.]
void wdsp_OutputDebugStringA (const char* s)
{
	if (getenv ("WDSP_DEBUG"))
		fputs (s, stderr);
}

// [Windows opens a console window here;  stdout already goes to the terminal.]
BOOL wdsp_AllocConsole (void)
{
	return TRUE;
}

BOOL wdsp_FreeConsole (void)
{
	return TRUE;
}

// "conout$" is the Windows console.  [wisdom.c opens a console window for its
// progress messages and points stdout at it.  Here stdout already goes to the
// terminal, so it is left alone.  Any other name is opened as a file, as usual.]
int wdsp_freopen_s (FILE** pf, const char* name, const char* mode, FILE* stream)
{
	if (strcasecmp (name, "conout$") == 0)
	{
		*pf = stream;
		return 0;
	}
	*pf = freopen (name, mode, stream);
	return *pf ? 0 : errno;
}

/********************************************************************************************************
*																										*
*											Flush-to-Zero												*
*																										*
********************************************************************************************************/

// [See wdsp_platform.h.  These are the same bits that the Windows versions set:
// MXCSR bit 15 (FTZ) on x86/x64, and the FZ bit, bit 24, of FPCR or FPSCR on ARM.]

int wdsp_flush_to_zero_on (void)
{
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
	unsigned int csr = _mm_getcsr ();
	_mm_setcsr (csr | _MM_FLUSH_ZERO_ON);
	return (csr & _MM_FLUSH_ZERO_MASK) != 0;
#elif defined(__aarch64__)
	uint64_t fpcr;
	__asm__ __volatile__ ("mrs %0, fpcr" : "=r" (fpcr));
	__asm__ __volatile__ ("msr fpcr, %0" : : "r" (fpcr | (1ull << 24)));
	return (fpcr & (1ull << 24)) != 0;
#elif defined(__arm__) && defined(__ARM_FP)
	uint32_t fpscr;
	__asm__ __volatile__ ("vmrs %0, fpscr" : "=r" (fpscr));
	__asm__ __volatile__ ("vmsr fpscr, %0" : : "r" (fpscr | (1u << 24)));
	return (fpscr & (1u << 24)) != 0;
#else
	return 1;									// [no flush-to-zero mode here;  nothing to restore]
#endif
}

void wdsp_flush_to_zero_off (void)
{
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
	_mm_setcsr (_mm_getcsr () & ~_MM_FLUSH_ZERO_MASK);
#elif defined(__aarch64__)
	uint64_t fpcr;
	__asm__ __volatile__ ("mrs %0, fpcr" : "=r" (fpcr));
	__asm__ __volatile__ ("msr fpcr, %0" : : "r" (fpcr & ~(1ull << 24)));
#elif defined(__arm__) && defined(__ARM_FP)
	uint32_t fpscr;
	__asm__ __volatile__ ("vmrs %0, fpscr" : "=r" (fpscr));
	__asm__ __volatile__ ("vmsr fpscr, %0" : : "r" (fpscr & ~(1u << 24)));
#endif
}

#endif
