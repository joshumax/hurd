#ifndef _HACK_TIMER_H_
#define _HACK_TIMER_H_

struct timer_list
{
  struct timer_list *next, *prev;
  u_long expires;
  u_long data;
  void (*function)(unsigned long);
};

void add_timer (struct timer_list *);
int del_timer (struct timer_list *);
void init_timer (struct timer_list *);

#endif
