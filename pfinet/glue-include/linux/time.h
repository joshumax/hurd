#ifndef _HACK_TIME_H_
#define _HACK_TIME_H_

#include <sys/time.h>
#include "mapped-time.h"

#define do_gettimeofday(tp)	maptime_read (mapped_time, (tp))
#define get_fast_time(tp)	maptime_read (mapped_time, (tp))

#endif
