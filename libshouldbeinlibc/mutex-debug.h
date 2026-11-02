/* Error-checking mutexes, for --enable-mutex-debug.

   Copyright (C) 2026 Free Software Foundation, Inc.
   Written by Milos Nikic.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* A default mutex records no owner: its unlock clears the lock whoever
   calls it, so an unlock by a thread that does not hold the mutex, or of
   one nobody holds, passes silently, and takes the lock from a thread
   that holds it.  An error-checking mutex records its owner: its unlock
   by another thread fails with EPERM, and its relock by the owner with
   EDEADLK.

   With --enable-mutex-debug, every C file of the tree is compiled with
   this header included first (Makeconf).  Every mutex initialized with
   no attributes, or with PTHREAD_MUTEX_INITIALIZER, is then an
   error-checking one, and a lock or unlock that fails stops the program
   with a backtrace.  */

#ifndef _HURD_MUTEX_DEBUG_H
#define _HURD_MUTEX_DEBUG_H

/* Before the macros below, so that its declarations are not expanded.  */
#include <pthread.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Print the failure and a backtrace through stderr, and abort.  Not
   through descriptor 2, as backtrace_symbols_fd and assert_backtrace
   do: a bootstrap filesystem's stderr goes to the Mach console.  The
   addresses resolve with addr2line against the unstripped program.  */
static void __attribute__ ((noreturn, unused))
mutex_debug_fail (const char *what, int err, const char *file, int line)
{
  void *bt[64];
  int n = backtrace (bt, 64);
  char **names = backtrace_symbols (bt, n);

  fprintf (stderr, "mutex-debug: %s:%d: pthread_mutex_%s: %s\n",
	   file, line, what, strerror (err));
  for (int i = 1; i < n; i++)
    fprintf (stderr, "  #%d %p %s\n", i, bt[i], names ? names[i] : "");
  fflush (stderr);
  abort ();
}

static inline int
mutex_debug_init (pthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
  pthread_mutexattr_t a;
  int err;

  if (attr)
    return (pthread_mutex_init) (m, attr);
  pthread_mutexattr_init (&a);
  pthread_mutexattr_settype (&a, PTHREAD_MUTEX_ERRORCHECK);
  err = (pthread_mutex_init) (m, &a);
  pthread_mutexattr_destroy (&a);
  return err;
}

#define pthread_mutex_init(m, attr) mutex_debug_init ((m), (attr))

#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER __PTHREAD_ERRORCHECK_MUTEX_INITIALIZER

#define pthread_mutex_lock(m)						\
  ({ int __mutex_debug_err = (pthread_mutex_lock) (m);			\
     if (__mutex_debug_err)						\
       mutex_debug_fail ("lock", __mutex_debug_err, __FILE__, __LINE__);	\
     __mutex_debug_err; })

#define pthread_mutex_unlock(m)						\
  ({ int __mutex_debug_err = (pthread_mutex_unlock) (m);		\
     if (__mutex_debug_err)						\
       mutex_debug_fail ("unlock", __mutex_debug_err, __FILE__, __LINE__); \
     __mutex_debug_err; })

#endif /* _HURD_MUTEX_DEBUG_H */
