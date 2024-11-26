/* Run a task on slave_pset
   Copyright (C) 2024 Free Software Foundation, Inc.

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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <hurd.h>
#include <version.h>
#include <mach/mach_types.h>
#include <mach/mach_host.h>

static void
smp (char * const argv[])
{
  int i;
  error_t err;
  mach_msg_type_number_t pset_count;
  mach_port_t hostpriv;
  processor_set_name_array_t psets = {0};
  processor_set_t slave_pset = MACH_PORT_NULL;

  err = get_privileged_ports (&hostpriv, NULL);
  if (err)
    error (1, err, "Must be run as root for privileged cpu control");

  err = host_processor_sets (hostpriv, &psets, &pset_count);
  if (err)
    error (1, err, "Cannot get list of host processor sets");

  if (pset_count < 2)
    error (1, ENOSYS, "gnumach does not have the expected processor sets, are you running smp kernel?");

  err = host_processor_set_priv (hostpriv, psets[1], &slave_pset);
  mach_port_deallocate (mach_task_self (), hostpriv);
  for (i = 0; i < pset_count; i++)
    mach_port_deallocate (mach_task_self (), psets[i]);
  if (err)
    error (1, err, "Cannot get access to slave processor set");

  err = task_assign (mach_task_self (), slave_pset, FALSE);
  mach_port_deallocate (mach_task_self (), slave_pset);
  if (err)
    error (1, err, "Cannot assign task self to slave processor set");

  /* Drop privileges from suid binary */
  mach_port_deallocate (mach_task_self (), _hurd_host_priv);
  setuid (getuid ());

  execve (argv[1], &argv[1], environ);

  /* Fall through if not executed */
  error (1, errno, "failed to execute %s", argv[1]);
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    error (1, 0, "Usage: smp /path/to/executable");

  smp (argv);
  return 0;
}
