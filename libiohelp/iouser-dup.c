/* 
   Copyright (C) 1996 Free Software Foundation

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

#include "iohelp.h"

struct iouser *
iohelp_dup_iouser (struct iouser *iouser)
{
  struct iouser *new;
  error_t err = 0;

  new = malloc (sizeof (struct iouser));
  if (!new)
    return 0;

  new->uids = make_idvec ();
  new->gids = make_idvec ();
  if (!new->uids || !new->gids)
    goto lose;

  err = idvec_set (new->uids, iouser->uids);
  if (!err)
    err = idvec_set (new->gids, iouser->gids);

  if (err)
    {
    lose:
      if (new->uids)
	idvec_free (new->uids);
      if (new->gids)
	idvec_free (new->gids);
      free (new);
      return 0;
    }
  return new;
}
