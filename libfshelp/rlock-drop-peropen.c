/*
   Copyright (C) 2001, 2014-2019 Free Software Foundation

   Written by Neal H Walfield <neal@cs.uml.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "fshelp.h"
#include "rlock.h"

#include <stdlib.h>
#include <unistd.h>

error_t
fshelp_rlock_drop_peropen (struct rlock_peropen *po)
{
  struct rlock_list *l;
  struct rlock_list *t;

  for (l = *po->locks; l; l = t)
    {
      if (l->waiting)
	{
	  l->waiting = 0;
	  pthread_cond_broadcast (&l->wait);
	}

      list_unlink (node, l);

      t = l->po.next;
      free (l);
    }

  return 0;
}
