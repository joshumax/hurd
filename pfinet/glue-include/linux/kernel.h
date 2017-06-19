#ifndef _HACK_KERNEL_H
#define _HACK_KERNEL_H

#include <stdio.h>
#include <stdlib.h>
#include <assert-backtrace.h>


/* These don't actually matter since our locking protocols are different.  */
#define barrier()	((void)0) /*__asm__ __volatile__("": : :"memory") */

#define NORET_TYPE	/**/
#define ATTRIB_NORET	__attribute__((noreturn))
#define NORET_AND	noreturn,
#define FASTCALL(x)	x

/* XXX do something syslogy */
#define	KERN_EMERG
#define	KERN_ALERT
#define	KERN_CRIT
#define	KERN_ERR
#define	KERN_WARNING
#define	KERN_NOTICE
#define	KERN_INFO
#define	KERN_DEBUG

#define panic(str...)	(printk (str), assert_backtrace (!"panic"))

/*
 *      Display an IP address in readable format.
 */

#define NIPQUAD(addr) \
	((unsigned char *)&addr)[0], \
	((unsigned char *)&addr)[1], \
	((unsigned char *)&addr)[2], \
	((unsigned char *)&addr)[3]


#include <linux/sched.h>
#include <linux/bitops.h>

#define printk printf

static inline int
getname (const char *name, char **newp)
{
  *newp = malloc (strlen (name) + 1);
  strcpy (*newp, name);
  return 0;
}

static inline void
putname (char *p)
{
  free (p);
}

/* These two functions are used only to send SIGURG.  But I can't
   find any SIGIO code at all.  So we'll just punt on that; clearly
   Linux is missing the point.  SIGURG should only be sent for
   sockets that have explicitly requested it. */
static inline int
kill_proc (int pid, int signo, int priv)
{
  assert_backtrace (signo == SIGURG);
  return 0;
}

static inline int
kill_pg (int pgrp, int signo, int priv)
{
  assert_backtrace (signo == SIGURG);
  return 0;
}


#endif
