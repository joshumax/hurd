/* Add/remove paging devices

   Copyright (C) 1997,98,99 Free Software Foundation, Inc.
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

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <error.h>
#include <hurd/store.h>
#include <version.h>
#include <mach/default_pager.h>
#include <mntent.h>

#ifdef SWAPOFF
const char *argp_program_version = STANDARD_HURD_VERSION (swapoff);
#else
const char *argp_program_version = STANDARD_HURD_VERSION (swapon);
#endif

static struct argp_option options[] =
{
  {"standard",	  'a', 0, 0,
    "Use all devices marked as `swap' in " _PATH_MNTTAB},
  {0, 0}
};
static char *args_doc = "DEVICE...";

#ifdef SWAPOFF
static char *doc = "Stop paging on DEVICE...";
#else
static char *doc = "Start paging onto DEVICE...";
#endif

static int
swaponoff (const char *file, int add)
{
  error_t err;
  struct store *store;
  static mach_port_t def_pager = MACH_PORT_NULL;
  static mach_port_t dev_master = MACH_PORT_NULL;

  err = store_open (file, 0, 0, &store);
  if (err)
    {
      error (0, err, "%s", file);
      return err;
    }

  if (store->class != &store_device_class)
    {
      error (0, 0, "%s: Can't get device", file);
      return err;
    }
  if (! (store->flags & STORE_ENFORCED))
    {
      error (0, 0, "%s: Can only page to the entire device", file);
      return err;
    }

  if (def_pager == MACH_PORT_NULL)
    {
      mach_port_t host;

      err = get_privileged_ports (&host, &dev_master);
      if (err)
	error (12, err, "Cannot get host port");

      err = vm_set_default_memory_manager (host, &def_pager);
      mach_port_deallocate (mach_task_self (), host);
      if (err)
	error (13, err, "Cannot get default pager port");
    }

  {
    char pname[sizeof "/dev/" + strlen (store->name) + 1];
    snprintf (pname, sizeof pname, "/dev/%s", store->name);
    err = default_pager_paging_file (def_pager, dev_master, pname, add);
    if (err)
      error (0, err, "%s", file);
  }

  store_free (store);

  return err;
}

static int do_all;

int
main(int argc, char *argv[])
{
  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'a':
	  do_all = 1;
	  break;

	case ARGP_KEY_ARG:
#ifdef SWAPOFF
#define ONOFF 0
#else
#define ONOFF 1
#endif
	  swaponoff (arg, ONOFF);
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (do_all)
    {
      struct mntent *me;
      FILE *f;

      f = setmntent (_PATH_MNTTAB, "r");
      if (f == NULL)
	error (1, errno, "Cannot read %s", _PATH_MNTTAB);
      else
	{
	  int done = 0, err = 0;
	  while ((me = getmntent (f)) != NULL)
	    if (!strcmp (me->mnt_type, MNTTYPE_SWAP))
	      {
		done = 1;

		err |= swaponoff (me->mnt_fsname, ONOFF);
	      }
	  if (done == 0)
	    error (2, 0, "No swap partitions found in %s", _PATH_MNTTAB);
	  else if (err)
	    return 1;
	}
    }

  return 0;
}
