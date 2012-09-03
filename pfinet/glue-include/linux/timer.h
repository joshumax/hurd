#ifndef _HACK_TIMER_H_
#define _HACK_TIMER_H_

#include <pthread.h>

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
  struct timer_list *next, **prev; /* things like to test "T->prev != NULL" */
  unsigned long expires;
  unsigned long data;
  void (*function)(unsigned long);
};

void add_timer (struct timer_list *);
int del_timer (struct timer_list *);
void mod_timer (struct timer_list *, unsigned long);
void init_timer (struct timer_list *);


#define time_after(a,b)		((long)(b) - (long)(a) < 0)
#define time_before(a,b)	time_after(b,a)

#define time_after_eq(a,b)	((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)	time_after_eq(b,a)


#endif
