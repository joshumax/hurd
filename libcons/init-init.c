/* init-init.c - Initialize the console library.
   Copyright (C) 1995, 1996, 2002 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG and Marcus Brinkmann.

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
#include <malloc.h>
#include <pthread.h>

#include <hurd.h>
#include <hurd/ports.h>

#include <mach.h>

#include "cons.h"
#include "priv.h"

struct port_bucket *cons_port_bucket;
struct port_class *cons_port_class;


error_t
cons_init (void)
{
  error_t err;
  cons_t cons;
  cons_notify_t dir_notify_port;
  mach_port_t dir_notify;

  cons_port_bucket = ports_create_bucket ();
  if (!cons_port_bucket)
    return errno;

  cons_port_class = ports_create_class (cons_vcons_destroy, NULL);
  if (!cons_port_class)
    return errno;

  /* Create the console structure.  */
  cons = malloc (sizeof (*cons));
  if (!cons)
    return errno;
  pthread_mutex_init (&cons->lock, NULL);
  cons->vcons_list = NULL;
  cons->vcons_last = NULL;
  cons->dir = opendir (cons_file);
  cons->slack = _cons_slack;
  if (!cons->dir)
    {
      free (cons);
      return errno;
    }
  cons->dirport = getdport (dirfd (cons->dir));
  if (cons->dirport == MACH_PORT_NULL)
    {
      closedir (cons->dir);
      free (cons);
      return errno;
    }

  /* Request directory notifications.  */
  err = ports_create_port (cons_port_class, cons_port_bucket,
                           sizeof (*dir_notify_port), &dir_notify_port);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), cons->dirport);
      closedir (cons->dir);
      free (cons);
      return err;
    }
  dir_notify_port->cons = cons;

  dir_notify = ports_get_right (dir_notify_port);
  err = dir_notice_changes (cons->dirport, dir_notify,
			    MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), cons->dirport);
      closedir (cons->dir);
      free (cons);
      return err;
    }
  return 0;
}
