#ifndef _MAPPED_TIME_H_
#define _MAPPED_TIME_H_

#define HZ 100

extern volatile struct mapped_time_value *mapped_time;
extern long long root_jiffies;

extern inline int
read_mapped_secs ()
{
  return mapped_time->seconds;
}

extern inline void
fill_timeval (struct timeval *tp)
{
  do
    {
      tp->tv_sec = mapped_time->seconds;
      tp->tv_usec = mapped_time->microseconds;
    }
  while (tp->tv_sec !=  mapped_time->check_seconds);
}

extern inline int
fetch_jiffies ()
{
  int secs, usecs;
  long long j;
  do
    {
      secs = mapped_time->seconds;
      usecs = mapped_time->microseconds;
    }
  while (secs != mapped_time->check_seconds);
  
  j = (long long) secs * HZ + ((long long) usecs * HZ) / 1000000;
  return j - root_jiffies;
}


#endif
