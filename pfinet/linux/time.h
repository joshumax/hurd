#ifndef _HACK_TIME_H_
#define _HACK_TIME_H_

#include <sys/time.h>
#include "mapped-time.h"

extern inline void
do_gettimeofday (struct timeval *tp)
{
  maptime_read (mapped_time, &_xtime_buf);
}

#endif
