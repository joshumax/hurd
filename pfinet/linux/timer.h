#ifndef _HACK_TIMER_H_
#define _HACK_TIMER_H_

#include <cthreads.h>

enum tstate
{
  TIMER_INACTIVE,
  TIMER_STARTING,
  TIMER_STARTED,
  TIMER_EXPIRED,
  TIMER_FUNCTION_RUNNING,
};
 
struct timer_list
{
  struct timer_list *next, **prevp;
  unsigned long expires;
  unsigned long data;
  void (*function)(unsigned long);
};

void add_timer (struct timer_list *);
int del_timer (struct timer_list *);
void init_timer (struct timer_list *);

#endif
