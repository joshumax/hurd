#ifndef _HACK_TIMER_H_
#define _HACK_TIMER_H_

#include <cthreads.h>

struct timer_list
{
  thread_t thread;
  int foobiebletch;
  unsigned long expires;
  unsigned long data;
  void (*function)(unsigned long);
};

void add_timer (struct timer_list *);
int del_timer (struct timer_list *);
void init_timer (struct timer_list *);

#endif
