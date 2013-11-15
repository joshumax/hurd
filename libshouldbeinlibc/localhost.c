/* A slightly more convenient wrapper for gethostname

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* Return the name of the localhost.  This is just a wrapper for gethostname,
   which takes care of allocating a big enough buffer, and caches the result
   after the first call (so the result should be copied before modification).
   If something goes wrong, 0 is returned, and errno set.  */
char *
localhost ()
{
  static char *buf = 0;
  static size_t buf_len = 0;

  if (! buf)
    {
      do {
	errno = 0;

	if (buf) {
	  char *new;
	  buf_len += buf_len;
	  new = realloc (buf, buf_len);
	  if (! new)
	    {
	      free (buf);
	      buf = 0;
	      errno = ENOMEM;
	      return 0;
	    }
	  else
	    buf = new;
	} else {
	  buf_len = 128;	/* Initial guess */
	  buf = malloc (buf_len);
	  if (! buf)
	    {
	      errno = ENOMEM;
	      return 0;
	    }
	}
      } while ((gethostname(buf, buf_len) == 0 && !memchr (buf, '\0', buf_len))
	       || errno == ENAMETOOLONG);

      if (errno)
	/* gethostname failed, abort.  */
	{
	  free (buf);
	  buf = 0;
	}
    }

  return buf;
}
