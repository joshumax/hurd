/* Add/remove paging devices

   Copyright (C) 1997, 1998 Free Software Foundation, Inc.
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

#ifdef SWAPOFF
const char *argp_program_version = STANDARD_HURD_VERSION (swapoff);
#else
const char *argp_program_version = STANDARD_HURD_VERSION (swapon);
#endif

static struct argp_option options[] =
{
  {"standard",	  'a', 0, 0, "Use all devices marked as `sw' in /etc/fstab"},
  {0, 0}
};
static char *args_doc = "DEVICE...";

#ifdef SWAPOFF
static char *doc = "Stop paging on DEVICE...";
#else
static char *doc = "Start paging onto DEVICE...";
#endif

static void
swaponoff (char *file, int add)
{
  error_t err;
  struct store *store;
  static mach_port_t def_pager = MACH_PORT_NULL;
  static mach_port_t dev_master = MACH_PORT_NULL;

  err = store_open (file, 0, 0, &store);
  if (err)
    {
      error (0, err, "%s", file);
      return;
    }

  if (store->class != &store_device_class)
    {
      error (0, 0, "%s: Can't get device", file);
      return;
    }
  if (! (store->flags & STORE_ENFORCED))
    {
      error (0, 0, "%s: Can only page to the entire device", file);
      return;
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
}

int
main(int argc, char *argv[])
{
  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'a':
	  argp_failure (state, 5, 0, "--standard: Not supported yet");

	case ARGP_KEY_ARG:
#ifdef SWAPOFF
	  swaponoff (arg, 0);
#else
	  swaponoff (arg, 1);
#endif
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  return 0;
}
