/* Deduce the shared portion of two hostnames

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

#include <string.h>

/* Returns a pointer into HOST1 that is the part of the domain shared with
   HOST2.  If the two do not share anything, the return value will point to
   the end of HOST1.  If either host is NULL, NULL is returned.  */
char *
shared_domain (char *host1, char *host2)
{
  char *shared, *e1, *e2;

  if (!host1 || !host2)
    return 0;

  /* Now compare HOST1 and HOST2 from the end.  */
  e2 = host2 + strlen (host2);
  e1 = host1 + strlen (host1);
  shared = e1;

  /* Ignore `absolute' syntax.  */
  if (*e1 == '.')
    e1--;
  if (*e2 == '.')
    e2--;

  while (e1 > host1 && e2 > host2 && *e2 == *e1)
    {
      if (*e1 == '.')
	shared = e1;		/* A common domain level has been passed. */
      e1--;
      e2--;
    }

  return shared;
}
