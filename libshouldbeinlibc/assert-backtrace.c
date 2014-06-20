/* Augment failing assertions with backtraces.

   Copyright (C) 2015,2016 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef NDEBUG

#include <error.h>
#include <errno.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "assert-backtrace.h"

static void __attribute__ ((noreturn))
__assert_fail_base_backtrace (const char *fmt,
			      const char *assertion,
			      const char *file,
			      unsigned int line,
			      const char *function)
{
  const size_t size = 128;
  const size_t skip = 2;
  int nptrs;
  void *buffer[size];

  nptrs = backtrace(buffer, size);
  if (nptrs == 0)
    error (1, *__errno_location (), "backtrace");

  fprintf (stderr,
	   fmt, program_invocation_name, file, line, function, assertion);
  backtrace_symbols_fd (&buffer[skip], nptrs - skip, STDERR_FILENO);
  fflush (stderr);

  /* Die.  */
  abort ();
}

void
__assert_fail_backtrace (const char *assertion, const char *file,
			 unsigned int line, const char *function)
{
  __assert_fail_base_backtrace ("%s: %s:%u: %s: Assertion '%s' failed.\n",
				assertion, file, line, function);
}

void
__assert_perror_fail_backtrace (int errnum,
				const char *file,
				unsigned int line,
				const char *function)
{
  char errbuf[1024];

  char *e = strerror_r (errnum, errbuf, sizeof errbuf);
  __assert_fail_base_backtrace ("%s: %s:%u: %s: Unexpected error: %s.\n",
				e, file, line, function);

}

#endif /* ! defined NDEBUG */
