#ifndef _HACK_TIME_H_
#define _HACK_TIME_H_

#include <sys/time.h>
#include "mapped-time.h"

extern inline void 
do_gettimeofday (struct timeval *tp)
{
  fill_timeval (tp);
}

#endif
