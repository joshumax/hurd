/* Routines for vectors of integers

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

#include <malloc.h>
#include <string.h>
#include <ivec.h>

/* Return a new ivec, or NULL if there wasn't enough memory.  */
struct ivec *
make_ivec ()
{
  struct ivec *ivec = malloc (sizeof (struct ivec));
  if (ivec)
    {
      ivec->ints = 0;
      ivec->num = ivec->alloced = 0;
    }
  return ivec;
}

/* Insert I into IVEC at position POS, returning ENOMEM if there wasn't
   enough memory, or 0.  */
error_t
ivec_insert (struct ivec *ivec, unsigned pos, int i)
{
  if (ivec->alloced == ivec->num)
    {
      unsigned new_size = ivec->alloced * 2 + 1;
      int *new_ints = realloc (ivec->ints, new_size * sizeof (int));

      if (! new_ints)
	return ENOMEM;
	  
      ivec->alloced = new_size;
      ivec->ints = new_ints;
    }

  if (pos < ivec->num)
    bcopy (ivec->ints + pos, ivec->ints + pos + 1, ivec->num - pos);

  ivec->ints[pos] = i;
  ivec->num++;

  return 0;
}

/* Add I onto the end of IVEC, returning ENOMEM if there's not enough memory,
   or 0.  */
error_t
ivec_add (struct ivec *ivec, int i)
{
  return ivec_insert (ivec, ivec->num, i);
}

/* Returns true if IVEC contains I.  */
int
ivec_contains (struct ivec *ivec, int i)
{
  unsigned j;
  for (j = 0; j < ivec->num; j++)
    if (ivec->ints[j] == i)
      return 1;
  return 0;
}
