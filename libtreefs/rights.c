/* Functions for making send rights in various ways

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* Return in PORT a send right for a new protid, pointing at the peropen PO,
   with rights initialized from AUTH.  */
error_t
treefs_peropen_create_right (struct treefs_peropen *po,
			     struct treefs_auth *auth,
			     mach_port_t *port)
{
  struct treefs_node *node = po->node;
  struct treefs_fsys *fsys = node->fsys;
  struct treefs_handle *handle =
    ports_allocate_port (fsys->port_bucket,
			 sizeof (struct treefs_handle),
			 fsys->handle_port_class);

  if (handle == NULL)
    return MACH_PORT_NULL;

  handle->po = po;
  po->refs++;
  handle->auth = auth;
  auth->refs++;

  err = treefs_node_init_handle (node, handle);
  if (err)
    {
      po->refs--;
      auth->refs--;
    }

  *port = ports_get_right (handle);

  return 0;
}

/* Return in PORT a send right for a new handle and a new peropen, pointing
   at NODE, with rights initialized from AUTH.  FLAGS and PARENT_PORT are used
   to initialize the corresponding fields in the new peropen.  */
error_t
treefs_node_create_right (struct treefs_node *node, int flags,
			  mach_port_t parent_port, struct treefs_auth *auth,
			  mach_port_t *port)
{
  struct treefs_peropen *po = malloc (sizeof (struct treefs_peropen));

  if (po == NULL)
    return ENOMEM;

  /* Initialize the peropen structure.  */
  po->refs = 0;
  po->node = node;
  po->open_flags = flags;
  po->user_lock_state = LOCK_UN;
  po->parent_port = parent_port;
  if (parent_port != MACH_PORT_NULL)
    mach_port_mod_refs (mach_task_self (),
			parent_port, MACH_PORT_RIGHT_SEND, 1);

  treefs_node_ref (node);

  err = treefs_node_init_peropen (node, po, flags, auth);
  if (err)
    goto puke;

  err = treefs_peropen_create_right (po, auth, port);
  if (err)
    goto puke;

  return 0;

 puke:
  treefs_node_unref (node);
  free (po);
  return err;
}
