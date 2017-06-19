/* Hurd standard password server.
   Copyright (C) 1999, 2013 Free Software Foundation
   Written by Mark Kettenis.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <argp.h>
#include <assert-backtrace.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>
#include <string.h>

#include <hurd.h>
#include <hurd/auth.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>

#include <ugids.h>
#include <version.h>

#include "password_S.h"


const char *argp_program_version = STANDARD_HURD_VERSION (password);

/* Port bucket we service requests on.  */
struct port_bucket *port_bucket;

/* Our port classes.  */
struct port_class *trivfs_control_class;
struct port_class *trivfs_protid_class;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = 0;

static int
password_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int password_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  return password_server (inp, outp) || trivfs_demuxer (inp, outp);
}


int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  const struct argp argp = { 0, 0, 0, "Hurd standard password server." };

  argp_parse (&argp, argc, argv, 0, 0, 0);
  
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
    ports_manage_port_operations_multithread (port_bucket, password_demuxer,
					      2 * 60 * 1000,
					      10 * 60 * 1000,
					      0);
  /* That returns when 10 minutes pass without an RPC.  Try shutting down
     as if sent fsys_goaway; if we have any users who need us to stay
     around, this returns EBUSY and we loop to service more RPCs.  */
  while (trivfs_goaway (fsys, 0));

  return 0;
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


/* Implement password_check_user as described in <hurd/password.defs>.  */
kern_return_t
S_password_check_user (struct trivfs_protid *cred, uid_t user, char *pw,
		       mach_port_t *port, mach_msg_type_name_t *port_type)
{
  struct ugids ugids = UGIDS_INIT;
  auth_t auth;
  error_t err;
  
  char *getpass (const char *prompt, uid_t id, int is_group,
		 void *pwd_or_group, void *hook)
    {
      assert_backtrace (! is_group && id == user);
      return strdup (pw);
    }

  if (! cred)
    return EOPNOTSUPP;

  if (cred->pi.bucket != port_bucket ||
      cred->pi.class != trivfs_protid_class)
    {
      ports_port_deref (cred);
      return EOPNOTSUPP;
    }

  /* Verify password.  */
  err = ugids_add_user (&ugids, user, 1);
  if (!err)
    err = ugids_verify (&ugids, 0, 0, getpass, 0, 0, 0);

  if (!err)
    {
      auth = getauth ();
      err = auth_makeauth (auth, 0, MACH_MSG_TYPE_COPY_SEND, 0,
			   ugids.eff_uids.ids, ugids.eff_uids.num,
			   ugids.avail_uids.ids, ugids.avail_uids.num,
			   ugids.eff_gids.ids, ugids.eff_gids.num,
			   ugids.avail_gids.ids, ugids.avail_gids.num,
			   port);
      mach_port_deallocate (mach_task_self (), auth);
      *port_type = MACH_MSG_TYPE_MOVE_SEND;
    }

  ugids_fini (&ugids);
  return err;
}

/* Implement password_check_group as described in <hurd/password.defs>.  */
kern_return_t
S_password_check_group (struct trivfs_protid *cred, uid_t group, char *pw,
			mach_port_t *port, mach_msg_type_name_t *port_type)
{
  struct ugids ugids = UGIDS_INIT;
  auth_t auth;
  error_t err;
  
  char *getpass (const char *prompt, uid_t id, int is_group,
		 void *pwd_or_group, void *hook)
    {
      assert_backtrace (is_group && id == group);
      return strdup (pw);
    }

  if (! cred)
    return EOPNOTSUPP;

  if (cred->pi.bucket != port_bucket ||
      cred->pi.class != trivfs_protid_class)
    {
      ports_port_deref (cred);
      return EOPNOTSUPP;
    }

  /* Verify password.  */
  err = ugids_add_gid (&ugids, group, 1);
  if (!err)
    err = ugids_verify (&ugids, 0, 0, getpass, 0, 0, 0);

  if (!err)
    {
      auth = getauth ();
      err = auth_makeauth (auth, 0, MACH_MSG_TYPE_COPY_SEND, 0,
			   ugids.eff_uids.ids, ugids.eff_uids.num,
			   ugids.avail_uids.ids, ugids.avail_uids.num,
			   ugids.eff_gids.ids, ugids.eff_gids.num,
			   ugids.avail_gids.ids, ugids.avail_gids.num,
			   port);
      mach_port_deallocate (mach_task_self (), auth);
      *port_type = MACH_MSG_TYPE_MOVE_SEND;
    }

  ugids_fini (&ugids);
  return err;
}
