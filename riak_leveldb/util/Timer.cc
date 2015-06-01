#include "util/Timer.h"

#include <sys/time.h>

using namespace leveldb;

/**.......................................................................
 * Constructor.
 */
Timer::Timer() 
{
  startMs_      = 0;
  stopMs_       = 0;
  diffMs_       = 0;
  integratedMs_ = 0;
}

/**.......................................................................
 * Destructor.
 */
Timer::~Timer() {}

void Timer::start()
{
  startMs_ = currentMs();
}

void Timer::stop()
{
  stopMs_        = currentMs();
  diffMs_        = stopMs_ - startMs_;
  integratedMs_ += diffMs_;
}

uint64_t Timer::deltaMicroSeconds()
{
  return diffMs_;
}

uint64_t Timer::currentMs() 
{
#if _POSIX_TIMERS >= 200801L

  struct timespec ts;
  
  // this is rumored to be faster that gettimeofday(), and sometimes
  // shift less ... someday use CLOCK_MONOTONIC_RAW

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec/1000;

#else

  struct timeval tv;
  gettimeofday(&tv, NULL);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;

#endif
}

