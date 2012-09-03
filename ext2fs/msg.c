/* Message printing functions

   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.

   Converted for ext2fs by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <stdio.h>
#include <stdarg.h>

#include "ext2fs.h"

pthread_mutex_t printf_lock = PTHREAD_MUTEX_INITIALIZER; /* XXX */


int printf (const char *fmt, ...)
{
  va_list arg;
  int done;
  va_start (arg, fmt);
  pthread_mutex_lock (&printf_lock);
  done = vprintf (fmt, arg);
  pthread_mutex_unlock (&printf_lock);
  va_end (arg);
  return done;
}

static char error_buf[1024];

void _ext2_error (const char * function, const char * fmt, ...)
{
  va_list args;

  pthread_mutex_lock (&printf_lock);

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  fprintf (stderr, "ext2fs: %s: %s: %s\n", diskfs_disk_name, function, error_buf);

  pthread_mutex_unlock (&printf_lock);
}

void _ext2_panic (const char * function, const char * fmt, ...)
{
  va_list args;

  pthread_mutex_lock (&printf_lock);

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  fprintf(stderr, "ext2fs: %s: panic: %s: %s\n",
	  diskfs_disk_name, function, error_buf);

  pthread_mutex_unlock (&printf_lock);

  exit (1);
}

void ext2_warning (const char * fmt, ...)
{
  va_list args;

  pthread_mutex_lock (&printf_lock);

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  fprintf (stderr, "ext2fs: %s: warning: %s\n", diskfs_disk_name, error_buf);

  pthread_mutex_unlock (&printf_lock);
}
