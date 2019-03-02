/* 
   Copyright (C) 2018 Free Software Foundation, Inc.

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
 * This program is a translator that implements an RPC to halt the pc.
 */

#include <argp.h>
#include <assert-backtrace.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>
#include <string.h>
#include <hurd.h>
#include <hurd/fs.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/paths.h>
#include <sys/file.h>
#include <version.h>

#include "acpi_shutdown.h"
#include "shutdown_S.h"

/* Port bucket we service requests on.  */
struct port_bucket *port_bucket;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ | O_WRITE; 

/* Our port classes.  */
struct port_class *trivfs_protid_class;
struct port_class *trivfs_control_class;

kern_return_t
S_shutdown_shutdown(trivfs_protid_t server)
{
  disappear_via_acpi();
  return 0;
}

static int
shutdown_demuxer (mach_msg_header_t *inp,
		  mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = shutdown_server_routine (inp)) ||
      (routine = NULL, trivfs_demuxer (inp, outp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;
  
  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_control_class);
  ports_inhibit_class_rpcs (trivfs_protid_class);

  /* Are there any extant user ports for the /servers/password file?  */
  count = ports_count_class (trivfs_protid_class);
  if (count > 0 && !(flags & FSYS_GOAWAY_FORCE))
    {
      /* We won't go away, so start things going again...  */
      ports_enable_class (trivfs_protid_class);
      ports_resume_class_rpcs (trivfs_control_class);
      ports_resume_class_rpcs (trivfs_protid_class);

      return EBUSY;
    }

  exit (0);
}


int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "must be started as a translator");

  err = trivfs_add_port_bucket (&port_bucket);
  if (err)
    error (1, 0, "error creating port bucket");

  err = trivfs_add_control_port_class (&trivfs_control_class);
  if (err)
    error (1, 0, "error creating control port class");

  err = trivfs_add_protid_port_class (&trivfs_protid_class);
  if (err)
    error (1, 0, "error creating protid port class");

  /* Reply to our parent.  */
  err = trivfs_startup (bootstrap, 0,
                        trivfs_control_class, port_bucket,
                        trivfs_protid_class, port_bucket,
                        &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "Contacting parent");

  /* Launch.  */
  do
    ports_manage_port_operations_multithread (port_bucket, shutdown_demuxer,
					      2 * 60 * 1000,
					      10 * 60 * 1000,
					      0);
  while (1);

  return 0;
}
