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

#ifndef __IVEC_H__
#define __IVEC_H__

#include <errno.h>

struct ivec 
{
  int *ints;
  unsigned num, alloced;
};

/* Return a new ivec, or NULL if there wasn't enough memory.  */
struct ivec *make_ivec ();

/* Insert I into IVEC at position POS, returning ENOMEM if there wasn't
   enough memory, or 0.  */
error_t ivec_insert (struct ivec *ivec, unsigned pos, int i);

/* Add I onto the end of IVEC, returning ENOMEM if there's not enough memory,
   or 0.  */
error_t ivec_add (struct ivec *ivec, int i);

/* Returns true if IVEC contains I.  */
int ivec_contains (struct ivec *ivec, int i);

#endif /* __IVEC_H__ */
