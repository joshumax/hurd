/* vcons-open.c - Open a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <pthread.h>

#include <hurd.h>
#include <mach.h>

#include "cons.h"

/* Open the virtual console for VCONS_ENTRY.  CONS is locked.
   Afterwards, R_VCONS will be locked.  */
error_t
cons_vcons_open (cons_t cons, vcons_list_t vcons_entry, vcons_t *r_vcons)
{
  error_t err = 0;
  char *name;
  file_t vconsp = MACH_PORT_NULL;
  file_t file = MACH_PORT_NULL;
  int fd = -1;
  struct stat statbuf;
  mach_port_t notify = MACH_PORT_NULL;
  vcons_t vcons;

  if (asprintf (&name, "%u", vcons_entry->id) < 0)
    return err;

  /* Set up the port we receive notification messages on.  */
  err = ports_create_port (cons_port_class, cons_port_bucket,
                           sizeof (*vcons), &vcons);
  if (err)
    goto err;
  vcons->notify.cons = NULL;
  vcons->cons = cons;
  vcons->vcons_entry = vcons_entry;
  vcons->id = vcons_entry->id;
  pthread_mutex_init (&vcons->lock, NULL);
  vcons->input = -1;
  vcons->display = MAP_FAILED;
  vcons->scrolling = 0;

  /* Open the directory port of the virtual console.  */
  vconsp = file_name_lookup_under (cons->dirport, name,
				   O_DIRECTORY | O_RDONLY, 0);
  if (vconsp == MACH_PORT_NULL)
    {
      err = errno;
      goto err;
    }

  /* Within that directory, open the input node.  */
  file = file_name_lookup_under (vconsp, "input", O_WRONLY /* | O_NONBLOCK */, 0);
  if (file == MACH_PORT_NULL)
    err = errno;
  else
    {
      vcons->input = openport (file, O_WRONLY /* | O_NONBLOCK */);
      if (vcons->input < 0)
	err = errno;
      else
	/* openport() consumed the reference.  */
	file = MACH_PORT_NULL;
    }
  if (err)
    goto err;

  /* Within that directory, also open the display node.  */
  file = file_name_lookup_under (vconsp, "display", O_RDONLY, 0);
  if (file == MACH_PORT_NULL)
    err = errno;
  else
    {
      /* Acquire an additional reference for openport().  */
      err = mach_port_mod_refs (mach_task_self (), file,
				MACH_PORT_RIGHT_SEND, +1);
      if (err)
	goto err;
      fd = openport (file, O_RDONLY);
      if (fd < 0)
	err = errno;
    }
  if (err)
    goto err;

  /* Map the whole file.  */
  if (fstat (fd, &statbuf) < 0)
    {
      err = errno;
      goto err;
    }
  vcons->display_size = statbuf.st_size;
  vcons->display = mmap (0, vcons->display_size, PROT_READ, MAP_SHARED, fd, 0);
  if (vcons->display == MAP_FAILED)
    {
      err = errno;
      goto err;
    }

  if (vcons->display->magic != CONS_MAGIC
      || vcons->display->version >> CONS_VERSION_MAJ_SHIFT != 0)
    {
      err = EINVAL;
      goto err;
    }
  vcons->state.screen.width = vcons->display->screen.width;
  vcons->state.screen.height = vcons->display->screen.height;
  vcons->state.screen.lines = vcons->display->screen.lines;
  vcons->state.screen.matrix = (conchar_t *)
    (((uint32_t *) vcons->display) + vcons->display->screen.matrix);
  vcons->state.changes.length = vcons->display->changes.length;
  vcons->state.changes.buffer = (cons_change_t *)
    (((uint32_t *) vcons->display) + vcons->display->changes.buffer);

  /* Request notification messages.  */
  notify = ports_get_right (vcons);
  mach_port_set_qlimit (mach_task_self (), notify, 1);

  /* When this succeeds, we will immediately receive notification
     messages for this virtual console.  */
  pthread_mutex_lock (&vcons->lock);
  err = file_notice_changes (file, notify, MACH_MSG_TYPE_MAKE_SEND);
  if (!err)
    {
      *r_vcons = vcons;
      goto out;
    }

 err:
  if (vcons->input >= 0)
    {
      close (vcons->input);
      vcons->input = -1;
    }
  if (vcons->display != MAP_FAILED)
    {
      munmap (vcons->display, vcons->display_size);
      vcons->display = MAP_FAILED;
    }
  if (notify)
    {
      mach_port_deallocate (mach_task_self (), notify);
      ports_port_deref (vcons);
    }
  ports_destroy_right (vcons);
 out:
  if (fd > 0)
    close (fd);
  if (file != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), file);
  if (vconsp != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), vconsp);
  free (name);
  return err;
}
