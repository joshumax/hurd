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

#ifndef __IDVEC_H__
#define __IDVEC_H__

#include <sys/types.h>
#include <errno.h>

struct idvec 
{
  uid_t *ids;
  unsigned num, alloced;
};

/* Return a new idvec, or NULL if there wasn't enough memory.  */
struct idvec *make_idvec ();

/* Insert ID into IDVEC at position POS, returning ENOMEM if there wasn't
   enough memory, or 0.  */
error_t idvec_insert (struct idvec *idvec, unsigned pos, uid_t id);

/* Add ID onto the end of IDVEC, returning ENOMEM if there's not enough memory,
   or 0.  */
error_t idvec_add (struct idvec *idvec, uid_t id);

/* Returns true if IDVEC contains ID.  */
int idvec_contains (struct idvec *idvec, uid_t id);

#endif /* __IDVEC_H__ */
