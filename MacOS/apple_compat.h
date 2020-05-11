/*
 * APPLE Compatibility
 */

#ifdef __APPLE__

// macOS < 10.12 doesn't have clock_gettime()
#include <time.h>
#if !defined(CLOCK_REALTIME) && !defined(CLOCK_MONOTONIC)

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 6
typedef int clockid_t;

#include <sys/time.h>
#include <mach/mach_time.h>

// here to avoid problem on linking
static inline int clock_gettime( clockid_t clk_id, struct timespec *ts )
{
  int ret = -1;
  if ( ts )
  {
    if      ( CLOCK_REALTIME == clk_id )
    {
      struct timeval tv;
      ret = gettimeofday(&tv, NULL);
      ts->tv_sec  = tv.tv_sec;
      ts->tv_nsec = tv.tv_usec * 1000;
    }
    else if ( CLOCK_MONOTONIC == clk_id )
    {
      const uint64_t t = mach_absolute_time();
      mach_timebase_info_data_t timebase;
      mach_timebase_info(&timebase);
      const uint64_t tdiff = t * timebase.numer / timebase.denom;
      ts->tv_sec  = tdiff / 1000000000;
      ts->tv_nsec = tdiff % 1000000000;
      ret = 0;
    }
  }
  return ret;
}

#endif // CLOCK_REALTIME and CLOCK_MONOTONIC

#endif // __APPLE__
