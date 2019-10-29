/* Make a new trivfs peropen/protid

   Copyright (C) 1993-1996, 1999, 2018-2019 Free Software Foundation, Inc.

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

#include <string.h>		/* For bcopy() */

#include "priv.h"

/* Return a new protid pointing to a new peropen in CRED, with REALNODE as
   the underlying node reference, with the given identity, and open flags in
   FLAGS.  CNTL is the trivfs control object.  */
error_t
trivfs_open (struct trivfs_control *cntl,
	     struct iouser *user,
	     unsigned flags,
	     mach_port_t realnode,
	     struct trivfs_protid **cred)
{
  error_t err = 0;
  struct trivfs_peropen *po = malloc (sizeof (struct trivfs_peropen));

  if (!po)
    return ENOMEM;

  ports_port_ref (cntl);

  refcount_init (&po->refcnt, 1);
  po->cntl = cntl;
  po->openmodes = flags;
  po->hook = 0;

  if (trivfs_peropen_create_hook)
    err = (*trivfs_peropen_create_hook) (po);
  if (!err)
    {
      struct trivfs_protid *new;

      err = ports_create_port (cntl->protid_class, cntl->protid_bucket,
			       sizeof (struct trivfs_protid), &new);
      if (! err)
	{
	  new->user = user;
	  new->isroot = _is_privileged (user->uids);

	  new->po = po;
	  new->hook = 0;
	  new->realnode = realnode;

	  if (!err && trivfs_protid_create_hook)
	    err = (*trivfs_protid_create_hook) (new);

	  if (err)
	    {
	      /* Setting REALNODE to null signals the clean routine not to
		 call the destroy hook, which we deallocate below anyway.  */
	      new->realnode = MACH_PORT_NULL;
	      ports_port_deref (new);
	    }
	  else
	    *cred = new;
	}
    }

  if (err)
    {
      ports_port_deref (cntl);
      free (po);
    }

  return err;
}
