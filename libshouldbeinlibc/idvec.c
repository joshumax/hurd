/* Routines for vectors of uids/gids

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
#include <idvec.h>

/* Return a new idvec, or NULL if there wasn't enough memory.  */
struct idvec *
make_idvec ()
{
  struct idvec *idvec = malloc (sizeof (struct idvec));
  if (idvec)
    {
      idvec->ids = 0;
      idvec->num = idvec->alloced = 0;
    }
  return idvec;
}

/* Insert ID into IDVEC at position POS, returning ENOMEM if there wasn't
   enough memory, or 0.  */
error_t
idvec_insert (struct idvec *idvec, unsigned pos, uid_t id)
{
  if (idvec->alloced == idvec->num)
    {
      unsigned new_size = idvec->alloced * 2 + 1;
      int *new_ids = realloc (idvec->ids, new_size * sizeof (int));

      if (! new_ids)
	return ENOMEM;
	  
      idvec->alloced = new_size;
      idvec->ids = new_ids;
    }

  if (pos < idvec->num)
    bcopy (idvec->ids + pos, idvec->ids + pos + 1, idvec->num - pos);

  idvec->ids[pos] = id;
  idvec->num++;

  return 0;
}

/* Add ID onto the end of IDVEC, returning ENOMEM if there's not enough memory,
   or 0.  */
error_t
idvec_add (struct idvec *idvec, uid_t id)
{
  return idvec_insert (idvec, idvec->num, id);
}

/* Returns true if IDVEC contains ID.  */
int
idvec_contains (struct idvec *idvec, uid_t id)
{
  unsigned i;
  for (i = 0; i < idvec->num; i++)
    if (idvec->ids[i] == id)
      return 1;
  return 0;
}
