/* Augment failing assertions with backtraces.

   Copyright (C) 1994-2015 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef __ASSERT_BACKTRACE__
#define __ASSERT_BACKTRACE__

#ifdef NDEBUG

#define assert_backtrace(expr)		((void) 0)
#define assert_perror_backtrace(errnum)	((void) 0)

#else /* NDEBUG */

#include <sys/cdefs.h>

/* This prints an "Assertion failed" message, prints a stack trace,
   and aborts.	*/
void __assert_fail_backtrace (const char *assertion,
			      const char *file,
			      unsigned int line,
			      const char *function)
  __attribute__ ((noreturn, unused));

/* Likewise, but prints the error text for ERRNUM.  */
void __assert_perror_fail_backtrace (int errnum,
				     const char *file,
				     unsigned int line,
				     const char *function)
  __attribute__ ((noreturn, unused));

#define assert_backtrace(expr)						\
  ((expr)								\
   ? (void) 0								\
   : __assert_fail_backtrace (__STRING(expr),				\
			      __FILE__, __LINE__,			\
			      __PRETTY_FUNCTION__))

#define assert_perror_backtrace(expr)					\
  ((expr == 0)								\
   ? (void) 0								\
   : __assert_perror_fail_backtrace (expr,				\
				     __FILE__, __LINE__,		\
				     __PRETTY_FUNCTION__))

#endif /* NDEBUG */
#endif /* __ASSERT_BACKTRACE__ */
