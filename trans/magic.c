/* A translator for returning FS_RETRY_MAGIC strings.

   Copyright (C) 1995, 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
#include <stdio.h>
#include <error.h>
#include <argp.h>
#include <hurd/fsys.h>
#include "fsys_S.h"
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (magic);
static char args_doc[] = "MAGIC";
static char doc[] = "A translator that returns the magic retry result MAGIC";

extern int fsys_server (mach_msg_header_t *, mach_msg_header_t *);

/* The magic string we return for lookups.  */
static char *magic = NULL;

void
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap, control, realnode;
  struct argp argp = { 0, 0, args_doc, doc };

  argp_parse (&argp, argc, argv, 0, 0, 0);

  magic = argv[1];
  
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (3, 0, "Must be started as a translator");

  /* Reply to our parent */
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &control);
  err =
    fsys_startup (bootstrap, 0, control, MACH_MSG_TYPE_MAKE_SEND, &realnode);
  if (err)
    error (1, err, "starting translator");

  /* Launch */
  while (1)
    {
      /* The timeout here is 10 minutes */
      err = mach_msg_server_timeout (fsys_server, 0, control,
				       MACH_RCV_TIMEOUT, 1000 * 60 * 10);
      if (err == MACH_RCV_TIMED_OUT)
	exit (0);
    }
}

error_t
S_fsys_getroot (mach_port_t fsys_t,
		mach_port_t dotdotnode,
		uid_t *uids, u_int nuids, uid_t *gids, u_int ngids,
		int flags,
		retry_type *do_retry, char *retry_name,
		mach_port_t *ret, mach_msg_type_name_t *rettype)
{
  strcpy(retry_name, magic);
  *do_retry = FS_RETRY_MAGICAL;
  *ret = MACH_PORT_NULL;
  *rettype = MACH_MSG_TYPE_COPY_SEND;
  return 0;
}

error_t
S_fsys_startup (mach_port_t bootstrap,
		int flags, mach_port_t control,
		mach_port_t *real, mach_msg_type_name_t *real_type)
{
  return EOPNOTSUPP;
}

error_t
S_fsys_goaway (mach_port_t control,
	     int flags)
{
  exit (0);
}

error_t
S_fsys_syncfs (mach_port_t control,
	       int wait,
	       int recurse)
{
  return 0;
}

error_t
S_fsys_set_options (mach_port_t control,
		    char *data, mach_msg_type_number_t len,
		    int do_children)
{
  return EOPNOTSUPP;
}

error_t
S_fsys_get_options (mach_port_t control,
		    char **data, mach_msg_type_number_t *len)
{
  return EOPNOTSUPP;
}

error_t
S_fsys_getfile (mach_port_t control,
		uid_t *uids, u_int nuids, uid_t *gids, u_int ngids,
		char *handle, u_int handllen,
		mach_port_t *pt, mach_msg_type_name_t *pttype)
{
  return EOPNOTSUPP;
}

error_t
S_fsys_getpriv (mach_port_t control,
		mach_port_t *host_priv, mach_msg_type_name_t *host_priv_type,
		mach_port_t *dev_master, mach_msg_type_name_t *dev_master_type,
		task_t *fs_task, mach_msg_type_name_t *fs_task_type)
{
  return EOPNOTSUPP;
}

error_t
S_fsys_init (mach_port_t control,
	     mach_port_t reply, mach_msg_type_name_t replytype,
	     mach_port_t proc, auth_t auth)
{
  return EOPNOTSUPP;
}

error_t
S_fsys_forward (mach_port_t server, mach_port_t requestor,
		char *argz, size_t argz_len)
{
  return EOPNOTSUPP;
}
