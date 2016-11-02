/*
   Copyright (C) 2008 Free Software Foundation, Inc.
   Written by Zheng Da.

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
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*
 * The multiplexer server provides the virtual network interface.
 * When it gets a packet, it forwards it to every other network interface,
 * the ones that are created by itself or that it connects to.
 * BPF is ported to the multiplexer to help deliver packets
 * to the right pfinet.
 */

#include <argz.h>
#include <argp.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>
#include <fcntl.h>

#include <hurd.h>
#include <mach.h>
#include <version.h>
#include <device/device.h>
#include <hurd/ports.h>
#include <hurd/netfs.h>
#include <version.h>

#include "ethernet.h"
#include "vdev.h"
#include "device_S.h"
#include "notify_S.h"
#include "bpf_impl.h"
#include "netfs_impl.h"
#include "util.h"

/* The device which the multiplexer connects to */
static char *device_file;

const char *argp_program_version = STANDARD_HURD_VERSION (eth-multiplexer);

static const char doc[] = "Hurd multiplexer server.";
static const struct argp_option options[] =
{
    {"interface", 'i', "DEVICE", 0,
      "Network interface to use", 2},
    {0}
};

/* Port bucket we service requests on.  */
struct port_bucket *port_bucket;
struct port_class *other_portclass;
struct port_class *vdev_portclass;
struct port_info *notify_pi;

int netfs_maxsymlinks = 12;
char *netfs_server_name = "multiplexer";
char *netfs_server_version = HURD_VERSION;
file_t root_file;
struct lnode root;
struct stat underlying_node_stat;

static int
multiplexer_demuxer (mach_msg_header_t *inp,
		  mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = NULL, ethernet_demuxer (inp, outp)) ||
      (routine = device_server_routine (inp)) ||
      (routine = notify_server_routine (inp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

static void *
multiplexer_thread (void *arg)
{
  ports_manage_port_operations_one_thread (port_bucket,
					   multiplexer_demuxer,
					   0);
  return 0;
}

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case 'i':
      device_file = arg;
      break;
    case ARGP_KEY_ERROR:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_INIT:
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  mach_port_t master_device;
  const struct argp argp = { options, parse_opt, 0, doc };
  pthread_t t;

  port_bucket = ports_create_bucket ();
  other_portclass = ports_create_class (0, 0);
  vdev_portclass = ports_create_class (destroy_vdev, 0);

  argp_parse (&argp, argc, argv, 0, 0, 0);

  /* Open the network interface. */
  if (device_file)
    {
      master_device = file_name_lookup (device_file, 0, 0);
      if (master_device == MACH_PORT_NULL)
	error (1, errno, "file_name_lookup");

      ethernet_open (device_file, master_device, port_bucket,
		     other_portclass);
    }

  /* Prepare for the notification. */
  err = ports_create_port (other_portclass, port_bucket,
			   sizeof (struct port_info), &notify_pi);
  if (err)
    error (1, err, "ports_create_port for notification");

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "must be started as a translator");

  /* Run the multiplexer server in another thread. */
  pthread_create (&t, NULL, multiplexer_thread, NULL);
  pthread_detach (t);

  err = maptime_map (0, 0, &multiplexer_maptime);
  if (err)
    error (4, err, "Cannot map time");

  /* Initialize netfs and start the translator. */
  netfs_init ();

  root_file = netfs_startup (bootstrap, O_READ);
  err = new_node (&root, &netfs_root_node);
  if (err)
    error (5, err, "Cannot create root node");

  err = io_stat (root_file, &underlying_node_stat);
  if (err)
    error (6, err, "Cannot stat underlying node");

  struct stat stat = underlying_node_stat;
  /* If the underlying node is not a directory, increase its permissions */
  if(!S_ISDIR(stat.st_mode))
    {
      if(stat.st_mode & S_IRUSR)
	stat.st_mode |= S_IXUSR;
      if(stat.st_mode & S_IRGRP)
	stat.st_mode |= S_IXGRP;
      if(stat.st_mode & S_IROTH)
	stat.st_mode |= S_IXOTH;
    }

  stat.st_mode &= ~(S_ITRANS | S_IFMT);
  stat.st_mode |= S_IFDIR;
  netfs_root_node->nn->ln->st = stat;
  fshelp_touch (&netfs_root_node->nn_stat, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
		multiplexer_maptime);

  netfs_server_loop ();         /* Never returns.  */
  return 0;
}

error_t
netfs_append_args (char **argz, size_t *argz_len)
{
  error_t err = 0;

#define ADD_OPT(fmt, args...)						\
  do { char buf[100];							\
       if (! err) {							\
         snprintf (buf, sizeof buf, fmt , ##args);			\
         err = argz_add (argz, argz_len, buf); } } while (0)
  if (device_file)
    ADD_OPT ("--interface=%s", device_file);
#undef ADD_OPT
  return err;
}
