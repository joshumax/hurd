/* Mach task store backend

   Copyright (C) 1995,96,97,2001, 2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hurd.h>

#include <mach/machine/vm_param.h>

#include "store.h"

static process_t
proc_server ()
{
  static process_t proc = MACH_PORT_NULL;
  if (proc == MACH_PORT_NULL)
    proc = getproc ();
  return proc;
}

static error_t
topen (const char *name, task_t *task)
{
  char *name_end;
  pid_t pid = strtoul (name, &name_end, 0);

  if (*name == '\0' || *name_end != '\0')
    return EINVAL;

  return proc_pid2task (proc_server (), pid, task);
}

static void
tclose (struct store *store)
{
  mach_port_deallocate (mach_task_self (), store->port);
  store->port = MACH_PORT_NULL;
}

static error_t
task_read (struct store *store,
	   store_offset_t addr, size_t index, size_t amount, void **buf, size_t *len)
{
  size_t bsize = store->block_size;
  return vm_read (store->port, addr * bsize, amount, (vm_address_t *)buf, len);
}

static error_t
task_write (struct store *store,
	    store_offset_t addr, size_t index,
	    const void *buf, size_t len, size_t *amount)
{
  size_t bsize = store->block_size;
  error_t err = vm_write (store->port, addr * bsize, (vm_address_t)buf, len);
  if (! err)
    *amount = len;
  return err;
}

static error_t
task_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
}

static error_t
task_decode (struct store_enc *enc, const struct store_class *const *classes,
	     struct store **store)
{
  return store_std_leaf_decode (enc, _store_task_create, store);
}

static error_t
task_open (const char *name, int flags,
	   const struct store_class *const *classes,
	   struct store **store)
{
  return store_task_open (name, flags, store);
}

static error_t
task_set_flags (struct store *store, int flags)
{
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    /* Trying to set flags we don't support.  */
    return EINVAL;

  if ((flags & STORE_ENFORCED)
      && (store->num_runs > 0
	  || store->runs[0].start != 0
	  || store->runs[0].length != (VM_MAX_ADDRESS >> store->log2_block_size)))
    /* Kernel only enforces the whole thing...  */
    return EINVAL;

  if (flags & STORE_INACTIVE)
    tclose (store);

  store->flags |= flags;	/* When inactive, anything goes.  */

  return 0;
}

static error_t
task_clear_flags (struct store *store, int flags)
{
  error_t err = 0;
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    err = EINVAL;
  if (!err && (flags & STORE_INACTIVE))
    err = store->name ? topen (store->name, &store->port) : ESRCH;
  if (! err)
    store->flags &= ~flags;
  return err;
}

const struct store_class
store_task_class =
{
  STORAGE_TASK, "task", task_read, task_write, task_set_size,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, task_decode,
  task_set_flags, task_clear_flags, 0, 0, 0, task_open
};
STORE_STD_CLASS (task);

/* Return a new store in STORE referring to the mach task TASK.  Consumes
   the send right TASK.  */
error_t
store_task_create (task_t task, int flags, struct store **store)
{
  struct store_run run;

  run.start = 0;
  run.length = VM_MAX_ADDRESS / vm_page_size;

  flags |= STORE_ENFORCED;	/* 'cause it's the whole task.  */

  return _store_task_create (task, flags, vm_page_size, &run, 1, store);
}

/* Like store_task_create, but doesn't query the task for information.  */
error_t
_store_task_create (task_t task, int flags, size_t block_size,
		    const struct store_run *runs, size_t num_runs,
		    struct store **store)
{
  error_t err = 0;

  if (block_size >= vm_page_size)
    err = _store_create (&store_task_class,
			 task, flags, block_size, runs, num_runs, 0, store);
  else
    err = EINVAL;		/* block size less than page size.  */

  if (! err)
    {
      pid_t pid;

      err = proc_task2pid (proc_server (), task, &pid);
      if (! err)
	{
	  char buf[20];
	  snprintf (buf, sizeof buf, "%u", pid);
	  err = store_set_name (*store, buf);
	}

      if (err)
	store_free (*store);
    }

  return err;
}

/* Open the task NAME, and return the corresponding store in STORE.  */
error_t
store_task_open (const char *name, int flags, struct store **store)
{
  task_t task;
  error_t err = topen (name, &task);

  if (! err)
    {
      err = store_task_create (task, flags, store);
      if (err)
	mach_port_deallocate (mach_task_self (), task);
    }

  return err;
}
