/* Server for S_IFSOCK nodes
   Copyright (C) 1994, 1995, 2001 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/paths.h>
#include <sys/socket.h>
#include <hurd/socket.h>
#include <hurd/fsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <fcntl.h>

#include "ifsock_S.h"

mach_port_t address_port;

struct port_class *control_class;
struct port_class *node_class;
struct port_bucket *port_bucket;

int trivfs_fstype = FSTYPE_IFSOCK;
int trivfs_fsid = 0; /* ??? */

int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

int trivfs_allow_open = 0;

struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;

int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int ifsock_server (mach_msg_header_t *, mach_msg_header_t *);
  return trivfs_demuxer (inp, outp) || ifsock_server (inp, outp);
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t pflocal;
  mach_port_t bootstrap;
  char buf[512];

  control_class = ports_create_class (trivfs_clean_cntl, 0);
  node_class = ports_create_class (trivfs_clean_protid, 0);
  port_bucket = ports_create_bucket ();
  trivfs_protid_portclasses[0] = node_class;
  trivfs_cntl_portclasses[0] = control_class;

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error(1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, control_class, port_bucket,
			node_class, port_bucket, NULL);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error(2, err, "Contacting parent");

  /* Try and connect to the pflocal server */
  sprintf (buf, "%s/%d", _SERVERS_SOCKET, PF_LOCAL);
  pflocal = file_name_lookup (buf, 0, 0);

  if (pflocal == MACH_PORT_NULL)
    address_port = MACH_PORT_NULL;
  else
    {
      errno = socket_fabricate_address (pflocal, AF_LOCAL, &address_port);
      if (errno)
	address_port = MACH_PORT_NULL;
      mach_port_deallocate (mach_task_self (), pflocal);
    }

  /* Launch. */
  ports_manage_port_operations_one_thread (port_bucket, demuxer, 0);
  return 0;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_mode = (st->st_mode & ~S_IFMT) | S_IFSOCK;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  exit (0);
}

error_t
S_ifsock_getsockaddr (file_t sockfile,
		      mach_port_t *address)
{
  struct trivfs_protid *cred = ports_lookup_port (port_bucket, sockfile,
						  node_class);
  int perms;
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  err = file_check_access (cred->realnode, &perms);
  if (!err && !(perms & O_READ))
    err = EACCES;

  if (!err)
    *address = address_port;
  ports_port_deref (cred);
  return err;
}
