#ifndef _MAPPED_TIME_H_
#define _MAPPED_TIME_H_

#include <maptime.h>

#define HZ 100

extern volatile struct mapped_time_value *mapped_time;
extern long long root_jiffies;

static inline int
read_mapped_secs ()
{
  return mapped_time->seconds;
}

static inline int
fetch_jiffies ()
{
  struct timeval tv;
  long long j;

  maptime_read (mapped_time, &tv);

  j = (long long) tv.tv_sec * HZ + ((long long) tv.tv_usec * HZ) / 1000000;
  return j - root_jiffies;
}


#endif
