/* Routines for dealing with '\0' separated arg vectors.

   Copyright (C) 1995 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ---------------------------------------------------------------- */

/* Make a '\0' separated arg vector from a unix argv vector, returning it in
   ARGZ, and the total length in LEN.  If a memory allocation error occurs,
   ENOMEM is returned, otherwise 0.  */
error_t
argz_create(char **argv, char **argz, int *len)
{
  int tlen = 0;
  char **argp;

  for (argp = argv; *argp != NULL; argp++)
    tlen += strlen(*argp) + 1;

  *len = tlen;

  if (tlen == 0)
    *argz = NULL;
  else
    {
      *argz = malloc(tlen);
      if (*argz == NULL)
	return ENOMEM;

      while (tlen > 0)
	{
	  tlen -= strlen(*--argp) + 1;
	  strcpy(*argz + tlen, *argp);
	}
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Make '\0' separated arg vector ARGZ printable by converting all the '\0's
   except the last into spaces.  */
void
argz_stringify(char *argz, int len)
{
  while (len > 0)
    {
      int part_len = strlen(argz);
      argz += part_len;
      len -= part_len + 1;
      if (len > 0)
	*argz++ = ' ';
    }
}
