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

/* Returns the number of strings in ARGZ.  */
int
argz_count (char *argz, int len)
{
  int count = 0;
  while (len > 0)
    {
      int part_len = strlen(argz);
      argz += part_len + 1;
      len -= part_len + 1;
      count++;
    }
  return count;
}

/* ---------------------------------------------------------------- */

/* Puts pointers to each string in ARGZ into ARGV, which must be large enough
   to hold them all.  */
void
argz_extract (char *argz, int len, char **argv)
{
  while (len > 0)
    {
      int part_len = strlen(argz);
      *argv++ = argz;
      argz += part_len + 1;
      len -= part_len + 1;
    }
}

/* ---------------------------------------------------------------- */

/* Make '\0' separated arg vector ARGZ printable by converting all the '\0's
   except the last into the character SEP.  */
void
argz_stringify(char *argz, int len, int sep)
{
  while (len > 0)
    {
      int part_len = strlen(argz);
      argz += part_len;
      len -= part_len + 1;
      if (len > 0)
	*argz++ = sep;
    }
}

/* Add BUF, of length BUF_LEN to the argz vector in ARGZ & ARGZ_LEN.  */
error_t
argz_append (char **argz, unsigned *argz_len, char *buf, unsigned buf_len)
{
  unsigned new_argz_len = *argz_len + buf_len;
  char *new_argz = realloc (*argz, new_argz_len);
  if (new_argz)
    {
      bcopy (buf, new_argz + *argz_len, buf_len);
      *argz = new_argz;
      *argz_len = new_argz_len;
      return 0;
    }
  else
    return ENOMEM;
}

/* Add STR to the argz vector in ARGZ & ARGZ_LEN.  This should be moved into
   argz.c in libshouldbelibc.  */
error_t
argz_add (char **argz, unsigned *argz_len, char *str)
{
  return argz_append (argz, argz_len, str, strlen (str) + 1);
}

/* Remove ENTRY from ARGZ & ARGZ_LEN, if any.  */
void
argz_remove (char **argz, unsigned *argz_len, char *entry)
{
  if (entry)
    /* Get rid of the old value for NAME.  */
    {
      unsigned entry_len = strlen (entry) + 1;
      *argz_len -= entry_len;
      bcopy (entry + entry_len, entry, *argz_len - (entry - *argz));
    }
}

/* Insert ENTRY into ARGZ & ARGZ_LEN at position N.  */
error_t
argz_insert (char **argz, unsigned *argz_len, unsigned n, char *entry)
{
  char *place = *argz;
  unsigned left = *argz_len;
  while (n > 0 && left > 0)
    {
      int len = strlen (place) + 1;
      place += len;
      left -= len;
      n--;
    }
  if (left < 0 || n > 0)
    return EINVAL;
  else
    {
      unsigned entry_len = strlen  (entry) + 1;
      unsigned new_argz_len = *argz_len + entry_len;
      char *new_argz = realloc (*argz, new_argz_len);
      if (new_argz)
	{
	  place = new_argz + (place - *argz);
	  bcopy (place, place + entry_len, left);
	  bcopy (entry, place, entry_len);
	  *argz = new_argz;
	  *argz_len = new_argz_len;
	  return 0;
	}
      else
	return ENOMEM;
    }
}
