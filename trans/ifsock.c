/* Server for S_IFSOCK nodes
   Copyright (C) 1994 Free Software Foundation

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

mach_port_t address_port;

/* Port types */
#define PT_CTL 0
#define PT_NODE 1

int trivfs_fstype = FSTYPE_IFSOCK;
int trivfs_fsid = 0; /* ??? */

int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0

int trivfs_allow_open = 0;

int trivfs_protid_porttypes = {PT_NODE};
int trivfs_cntl_porttypes = {PT_CTL};
int trivfs_protid_nporttypes = 1;
int trivfs_cntl_nporttypes = 1;

main (int argc, char **argv)
{
  mach_port_t bootstrap;
  mach_port_t pflocal;
  mach_port_t ourcntl;
  mach_port_t realnode;
  char buf[512];
  struct trivfs_control *cntl;

  _libports_initialize ();

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    {
      fprintf (stderr, "%s must be started as a translator\n", argv[0]);
      exit (1);
    }
  
  /* Reply to our parent */
  ourcntl = trivfs_handle_port (MACH_PORT_NULL, PT_CTL, PT_NODE);
  error = fsys_startup (bootstrap, ourcntl, MACH_MSG_TYPE_MAKE_SEND,
			&realnode);
  
  /* Install the returned realnode for trivfs's use */
  cntl = ports_check_port_type (ourcntl, PT_CTL);
  assert (cntl);
  cntl->underlying = realnode;
  ports_done_with_port (ourcntl);

  /* Try and connect to the pflocal server */
  sprintf (buf, "%s/%d", _SERVERS_SOCKET, PF_LOCAL);
  pflocal = path_lookup (buf, 0);

  if (pflocal == MACH_PORT_NULL)
    address_port = MACH_PORT_NULL;
  else
    {
      errno = socket_fabricate_address (pflocal, &address_port);
      if (errno)
	address_port = MACH_PORT_NULL;
      mach_port_deallocate (mach_task_self (), pflocal);
    }

  /* Launch. */
  ports_manage_port_operations_onethread ();
  return 0;
}
  
int
ports_demuxer (mach_msg_header_t *, mach_msg_header_t *)
{
  return trivfs_demuxer || ifsock_server;
}

void
trivfs_modify_stat (struct stat *)
{
}

S_ifsock_getsockaddr (file_t sockfile,
		      mach_port_t *address)
{
  struct trivfs_protid *cred = ports_check_port_type (sockfile, PT_NODE);
  int perms;
  
  if (!cred)
    return EOPNOTSUPP;
  
  err = file_check_access (cred->underlying, &perms);
  if (!err && !(perms & O_READ))
    err = EACCES;
  
  if (!err)
    *address = address_port;
  return err;
}
