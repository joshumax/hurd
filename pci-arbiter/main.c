/*
   Copyright (C) 2017 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Translator initialization and demuxing */

#include <stdio.h>
#include <error.h>
#include <fcntl.h>
#include <version.h>
#include <argp.h>
#include <hurd/netfs.h>

#include <pci_S.h>
#include <startup_notify_S.h>
#include "libnetfs/io_S.h"
#include "libnetfs/fs_S.h"
#include "libports/notify_S.h"
#include "libnetfs/fsys_S.h"
#include "libports/interrupt_S.h"
#include "libnetfs/ifsock_S.h"
#include <pciaccess.h>
#include "pcifs.h"
#include "startup.h"

/* Libnetfs stuff */
int netfs_maxsymlinks = 0;
char *netfs_server_name = "pci-arbiter";
char *netfs_server_version = HURD_VERSION;

int
netfs_demuxer (mach_msg_header_t * inp, mach_msg_header_t * outp)
{
  mig_routine_t routine;

  if ((routine = netfs_io_server_routine (inp)) ||
      (routine = netfs_fs_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)) ||
      (routine = netfs_fsys_server_routine (inp)) ||
      (routine = ports_interrupt_server_routine (inp)) ||
      (routine = netfs_ifsock_server_routine (inp)) ||
      (routine = pci_server_routine (inp)) ||
      (routine = startup_notify_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  /* Parse options */
  alloc_file_system (&fs);
  argp_parse (netfs_runtime_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "must be started as a translator");

  /* Initialize netfs and start the translator. */
  netfs_init ();

  err = maptime_map (0, 0, &pcifs_maptime);
  if (err)
    error (1, err, "mapping time");

  /* Start the PCI system: NB: pciaccess will choose x86 first and take lock */
  err = pci_system_init ();
  if (err)
    error (1, err, "Starting the PCI system");

  /* Create the PCI filesystem */
  err = init_file_system (netfs_startup (bootstrap, O_READ), fs);
  if (err)
    error (1, err, "Creating the PCI filesystem");

  /* Create the filesystem tree */
  err = create_fs_tree (fs);
  if (err)
    error (1, err, "Creating the PCI filesystem tree");

  /* Set permissions */
  err = fs_set_permissions (fs);
  if (err)
    error (1, err, "Setting permissions");

  /*
   * Ask init to tell us when the system is going down,
   * so we can try to be friendly to our correspondents on the network.
   */
  arrange_shutdown_notification ();

  netfs_server_loop ();		/* Never returns.  */

  return 0;
}
