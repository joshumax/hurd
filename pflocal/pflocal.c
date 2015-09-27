/* A server for local sockets, of type PF_LOCAL

   Copyright (C) 1995, 1997, 1998 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <getopt.h>
#include <error.h>
#include <sys/stat.h>

#include <hurd/hurd_types.h>
#include <hurd/trivfs.h>

#include "sock.h"

/* Where to put the file-system ports. */
static struct port_bucket *pf_port_bucket;

/* Trivfs hooks */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = 0;

/* ---------------------------------------------------------------- */
#include "socket_S.h"

/* A demuxer to separate out pf-level operations on our node.  */
static int
pf_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = socket_server_routine (inp)) ||
      (routine = NULL, trivfs_demuxer (inp, outp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}


int
main(int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;

  if (argc > 1)
    {
      fprintf(stderr, "Usage: %s\n", program_invocation_name);
      exit (1);
    }

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error(2, 0, "Must be started as a translator");

  /* Prepare to create sockets.  */
  err = sock_global_init ();
  if (err)
    error(3, err, "Initializing");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  if (err)
    error(3, err, "Contacting parent");

  pf_port_bucket = fsys->pi.bucket;

  /* Launch. */
  do
    ports_manage_port_operations_multithread (pf_port_bucket,
					      pf_demuxer,
					      30*1000, 5*60*1000, 0);
  while (sock_global_shutdown () != 0);

  return 0;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_fstype = FSTYPE_MISC;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int force = (flags & FSYS_GOAWAY_FORCE);
  error_t err = ports_inhibit_bucket_rpcs (pf_port_bucket); /* Stop all I/O. */

  if (err == 0 || (err != EINTR && force))
    {
      /* Now see if there are any old sockets lying around.  */
      err = sock_global_shutdown ();

      /* Exit if not, or if we must. */
      if (err == 0 || force)
	exit (0);

      /* We won't go away, so start things going again...  */
      ports_resume_bucket_rpcs (pf_port_bucket);
    }

  return err;
}
