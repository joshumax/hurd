#ifndef _HACK_KERNEL_H
#define _HACK_KERNEL_H

#include <stdio.h>
#include <linux/sched.h>
#include <stdlib.h>
#include <assert.h>

#define printk printf

extern inline int
getname (const char *name, char **newp)
{
  *newp = malloc (strlen (name) + 1);
  strcpy (*newp, name);
  return 0;
}

extern inline void
putname (char *p)
{
  free (p);
}

/* These two functions are used only to send SIGURG.  But I can't
   find any SIGIO code at all.  So we'll just punt on that; clearly
   Linux is missing the point.  SIGURG should only be sent for 
   sockets that have explicitly requested it. */
extern inline int
kill_proc (int pid, int signo, int priv)
{
  assert (signo == SIGURG);
  return 0;
}

extern inline int 
kill_pg (int pgrp, int signo, int priv)
{
  assert (signo == SIGURG);
  return 0;
}


#endif
