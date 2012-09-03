/* Hacks to make boot work under UX

   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <mach.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#include "ux.h"

#if 0
static int (* const _sc)(int, ...) = &syscall;
int _sc_print = 1;

#define syscall(num, args...) \
 ({ int _rv, _num = (num), _pr = _sc_print; \
    _sc_print = 0; \
    if (_pr) printf ("syscall (%d) start\r\n", _num); \
    _rv = (*_sc) (_num , ##args); \
    if (_pr) printf ("syscall (%d) end\r\n", _num); \
    _sc_print = _pr; \
    _rv; \
   })
#endif

extern void __mach_init ();
void	(*mach_init_routine)() = __mach_init;

/* These will prevent the Hurd-ish versions from being used */

struct free_reply_port
{
  mach_port_t port;
  struct free_reply_port *next;
};
static struct free_reply_port *free_reply_ports = NULL;
static pthread_spinlock_t free_reply_ports_lock = PTHREAD_SPINLOCK_INITIALIZER;

mach_port_t __mig_get_reply_port ()
{
  pthread_spin_lock (&free_reply_ports_lock);
  if (free_reply_ports == NULL)
    {
      pthread_spin_unlock (&free_reply_ports_lock);
      return __mach_reply_port ();
    }
  else
    {
      struct free_reply_port *frp = free_reply_ports;
      mach_port_t reply_port = frp->port;
      free_reply_ports = free_reply_ports->next;
      pthread_spin_unlock (&free_reply_ports_lock);
      free (frp);
      return reply_port;
    }
}
mach_port_t mig_get_reply_port ()
{
  return __mig_get_reply_port ();
}
void __mig_put_reply_port (mach_port_t port)
{
  struct free_reply_port *frp = malloc (sizeof (struct free_reply_port));
  frp->port = port;
  pthread_spin_lock (&free_reply_ports_lock);
  frp->next = free_reply_ports;
  free_reply_ports = frp;
  pthread_spin_unlock (&free_reply_ports_lock);
}
void mig_put_reply_port (mach_port_t port)
{
  __mig_put_reply_port (port);
}
void __mig_dealloc_reply_port (mach_port_t port)
{
  mach_port_mod_refs (__mach_task_self (), port,
		      MACH_PORT_RIGHT_RECEIVE, -1);
}
void mig_dealloc_reply_port (mach_port_t port)
{
  __mig_dealloc_reply_port (port);
}
void __mig_init (void *stack) {}
void mig_init (void *stack) {}

int
task_by_pid (int pid)
{
  return syscall (-33, pid);
}

int
write (int fd,
       const void *buf,
       int buflen)
{
  return syscall (4, fd, buf, buflen);
}

int
read (int fd,
      void *buf,
      int buflen)
{
  return syscall (3, fd, buf, buflen);
}

int
open (const char *name,
      int flags,
      int mode)
{
  return syscall (5, name, flags, mode);
}

int
uxfstat (int fd, struct uxstat *buf)
{
  return syscall (62, fd, buf);
}

int
close (int fd)
{
  return syscall (6, fd);
}

int
lseek (int fd,
       int off,
       int whence)
{
  return syscall (19, fd, off, whence);
}

int
uxexit (int code)
{
  return syscall (1, code);
}

int
getpid ()
{
  return syscall (20);
}

int
ioctl (int fd, int code, void *buf)
{
  return syscall (54, fd, code, buf);
}

int
sigblock (int mask)
{
  return syscall (109, mask);
}

int
sigsetmask (int mask)
{
  return syscall (110, mask);
}

int
sigpause (int mask)
{
  return syscall (111, mask);
}


#if 0
void
sigreturn ()
{
  asm volatile ("movl $0x67,%eax\n"
		"lcall $0x7, $0x0\n"
		"ret");
}

void
_sigreturn ()
{
  asm volatile ("addl $0xc, %%esp\n"
		"call %0\n"
		"ret"::"m" (sigreturn));
}

int
sigvec (int sig, struct sigvec *vec, struct sigvec *ovec)
{
  asm volatile ("movl $0x6c,%%eax\n"
		"movl %0, %%edx\n"
		"orl $0x80000000, %%edx\n"
		"lcall $0x7,$0x0\n"
		"ret"::"g" (_sigreturn));
}
#else
int sigvec ();
#endif

void get_privileged_ports (mach_port_t *host_port, mach_port_t *device_port)
{
  *host_port = task_by_pid (-1);
  *device_port = task_by_pid (-2);
}

/* A *really* stupid printf that only understands %s & %d.  */
int
printf (const char *fmt, ...)
{
  va_list ap;
  const char *p = fmt, *q = p;

  void flush (const char *new)
    {
      if (p > q)
	write (1, q, p - q);
      q = p = new;
    }

  va_start (ap, fmt);
  while (*p)
    if (*p == '%' && p[1] == 's')
      {
	char *str = va_arg (ap, char *);
	flush (p + 2);
	write (1, str, strlen (str));
      }
    else if (*p == '%' && p[1] == 'd')
      {
	int i = va_arg (ap, int);
	char rbuf[20], *e = rbuf + sizeof (rbuf), *b = e; 

	if (i == 0)
	  *--b = '0';
	else
	  while (i)
	    {
	      *--b = i % 10 + '0';
	      i /= 10;
	    }

	flush (p + 2);
	write (1, b, e - b);
      }
    else
      p++;
  va_end (ap);

  flush (0);

  return 0;
}

static struct sgttyb term_sgb;
static int localbits;

void
init_termstate ()
{
  struct sgttyb sgb;
  int bits;
  ioctl (0, TIOCGETP, &term_sgb);
  ioctl (0, TIOCLGET, &localbits);
  /* Enter raw made.  Rather than try and interpret these bits,
     we just do what emacs does in .../emacs/src/sysdep.c for
     an old style terminal driver. */
  bits = localbits | LDECCTQ | LLITOUT | LPASS8 | LNOFLSH;
  ioctl (0, TIOCLSET, &bits);
  sgb = term_sgb;
  sgb.sg_flags &= ~ECHO;
  sgb.sg_flags |= RAW | ANYP;
  ioctl (0, TIOCSETN, &sgb);
}

void
restore_termstate ()
{
  ioctl (0, TIOCLSET, &localbits);
  ioctl (0, TIOCSETN, &term_sgb);
}
