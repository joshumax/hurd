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
#include <unistd.h>
#include <hurd/netfs.h>
#include <hurd/ports.h>
#include <hurd/fsys.h>
#include <device/device.h>
#include <sys/mman.h>

#include <pci_S.h>
#include <startup_notify_S.h>
#include "libnetfs/io_S.h"
#include "libnetfs/fs_S.h"
#include "libports/notify_S.h"
#include "libnetfs/fsys_S.h"
#include "libports/interrupt_S.h"
#include "libnetfs/ifsock_S.h"
#include "libmachdev/machdev.h"
#include <pciaccess.h>
#include <pthread.h>
#include "pcifs.h"
#include "startup.h"

struct pcifs *fs;
volatile struct mapped_time_value *pcifs_maptime;

/* Libnetfs stuff */
int netfs_maxsymlinks = 0;
char *netfs_server_name = "pci-arbiter";
char *netfs_server_version = HURD_VERSION;

static mach_port_t pci_control_port;


static io_return_t
pci_device_open (mach_port_t reply_port, mach_msg_type_name_t reply_port_type,
                 dev_mode_t mode, char *name, device_t * devp,
                 mach_msg_type_name_t * devicePoly)
{
  io_return_t err = D_SUCCESS;
  mach_port_t dev_master, root;
  string_t retry_name;
  retry_type retry;
  uid_t idlist[] = {0, 0, 0};

  if (strncmp(name, "pci", 3))
    err = D_NO_SUCH_DEVICE;

  /* Fall back to opening kernel device master */
  if (err)
    {
      err = get_privileged_ports(NULL, &dev_master);
      if (err)
        return err;
      if (dev_master == MACH_PORT_NULL)
        return D_NO_SUCH_DEVICE;
      err = device_open (dev_master, mode, name, devp);
      if (err)
        return err;
      *devicePoly = MACH_MSG_TYPE_MOVE_SEND;
      return D_SUCCESS;
    }

  err = fsys_getroot(pci_control_port, MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND,
                     idlist, 3, idlist, 3, 0,
                     &retry, retry_name, &root);
  if (err)
    return err;

  *devp = root;
  *devicePoly = MACH_MSG_TYPE_COPY_SEND;
  return D_SUCCESS;
}

static io_return_t
pci_device_close (void *d)
{
  ports_port_deref (&pci_control_port);
  return 0;
}

static void
pci_device_shutdown (mach_port_t dosync_handle)
{
  struct port_info *inpi = ports_lookup_port (netfs_port_bucket, dosync_handle,
					      pci_shutdown_notify_class);

  if (!inpi)
    return;

  // Free all libpciaccess resources
  pci_system_cleanup ();

  ports_port_deref (inpi);

  ports_destroy_right (&pci_control_port);

  netfs_shutdown (FSYS_GOAWAY_FORCE);
}

static struct machdev_device_emulation_ops pci_arbiter_emulation_ops = {
  NULL,
  NULL,
  NULL,
  NULL,
  pci_device_open,
  pci_device_close,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  pci_device_shutdown,
};

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

static void *
netfs_server_func (void *arg)
{
  error_t err;

  do 
    {
      ports_manage_port_operations_multithread (netfs_port_bucket,
						netfs_demuxer,
						1000 * 60 * 2, /* two minutes thread */
						1000 * 60 * 10,/* ten minutes server */
						0);
      err = netfs_shutdown (0);
    }
  while (err);
  return NULL;
}


static mach_port_t
pcifs_startup(mach_port_t bootstrap, int flags)
{
  error_t err;
  mach_port_t realnode;
  struct port_info *newpi;

  err = ports_create_port (netfs_control_class, netfs_port_bucket,
			     sizeof (struct port_info), &newpi);
  if (!err)
    {
      pci_control_port = ports_get_send_right (newpi);
      err = fsys_startup (bootstrap, flags, pci_control_port, MACH_MSG_TYPE_COPY_SEND,
			    &realnode);
      assert_perror_backtrace (err);
    }
  if (err)
    error (11, err, "Translator startup failure: fsys_startup");

  return realnode;
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  mach_port_t disk_server_task;
  pthread_t t, nt;

  /* Parse options */
  alloc_file_system (&fs);
  argp_parse (netfs_runtime_argp, argc, argv, 0, 0, 0);
  disk_server_task = fs->params.disk_server_task;

  if (disk_server_task != MACH_PORT_NULL)
    {
      machdev_register (&pci_arbiter_emulation_ops);
      machdev_device_init ();
      machdev_trivfs_init (disk_server_task, "pci", "/servers/bus/pci", &bootstrap);
      err = pthread_create (&t, NULL, machdev_server, NULL);
      if (err)
        error (1, err, "Creating machdev thread");
      pthread_detach (t);
    }
  else
    {
      task_get_bootstrap_port (mach_task_self (), &bootstrap);
      if (bootstrap == MACH_PORT_NULL)
        error (1, 0, "must be started as a translator");
    }
  /* Initialize netfs and start the translator. */
  netfs_init ();

  err = maptime_map (0, 0, &pcifs_maptime);
  if (err)
    err = maptime_map (1, 0, &pcifs_maptime);
  if (err)
    error (1, err, "mapping time");

  /* Start the PCI system: NB: pciaccess will choose x86 first and take lock */
  err = pci_system_init ();
  if (err)
    error (1, err, "Starting the PCI system");

  if (disk_server_task != MACH_PORT_NULL)
    machdev_trivfs_server(bootstrap);
    /* Timer started, quickly do all these next, before we call rump_init */

  /* Create the root node first */
  err = init_root_node ();
  if (err)
    error (1, err, "Creating the root node");
  
  pcifs_startup (bootstrap, O_READ);

  err = init_file_system (fs);
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

  err = pthread_create (&nt, NULL, netfs_server_func, NULL);
  if (err)
    error (1, err, "Creating netfs loop thread");
  pthread_detach (nt);

  /*
   * Ask init to tell us when the system is going down,
   * so we can try to be friendly to our correspondents on the network.
   */
  arrange_shutdown_notification ();

  /* Let the other threads do their job */
  pthread_exit(NULL);
  /* Never reached */
  return 0;
}
