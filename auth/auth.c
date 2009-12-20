/* Authentication server.
   Copyright (C) 1996,97,98,99,2002 Free Software Foundation, Inc.
   Written by Roland McGrath.

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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <mach.h>
#include <cthreads.h>
#include <hurd.h>
#include <hurd/startup.h>
#include <hurd/ports.h>
#include <hurd/ihash.h>
#include <idvec.h>
#include <assert.h>
#include <argp.h>
#include <error.h>
#include <version.h>
#include "auth_S.h"
#include "auth_reply_U.h"

const char *argp_program_version = STANDARD_HURD_VERSION(auth);


/* Auth handles are server ports with sets of ids.  */
struct authhandle
  {
    struct port_info pi;
    struct idvec euids, egids, auids, agids;
  };

struct port_bucket *auth_bucket;
struct port_class *authhandle_portclass;


/* Create a new auth port.  */

static error_t
create_authhandle (struct authhandle **new)
{
  error_t err = ports_create_port (authhandle_portclass, auth_bucket,
				   sizeof **new, new);
  if (! err)
    bzero (&(*new)->euids, (void *) &(*new)[1] - (void *) &(*new)->euids);
  return err;
}

/* Clean up a dead auth port.  */

static void
destroy_authhandle (void *p)
{
  struct authhandle *h = p;
  idvec_free_contents (&h->euids);
  idvec_free_contents (&h->egids);
  idvec_free_contents (&h->auids);
  idvec_free_contents (&h->agids);
}

/* Called by server stub functions.  */

authhandle_t
auth_port_to_handle (auth_t auth)
{
  return ports_lookup_port (auth_bucket, auth, authhandle_portclass);
}

/* id management.  */

static inline void
idvec_copyout (struct idvec *idvec, uid_t **ids, size_t *nids)
{
  if (idvec->num > *nids)
    *ids = idvec->ids;
  else
    memcpy (*ids, idvec->ids, idvec->num * sizeof *ids);
  *nids = idvec->num;
}

#define C(auth, ids)	idvec_copyout (&auth->ids, ids, n##ids)
#define OUTIDS(auth)	(C (auth, euids), C (auth, egids), \
			 C (auth, auids), C (auth, agids))

/* Implement auth_getids as described in <hurd/auth.defs>. */
kern_return_t
S_auth_getids (struct authhandle *auth,
	       uid_t **euids,
	       size_t *neuids,
	       uid_t **auids,
	       size_t *nauids,
	       uid_t **egids,
	       size_t *negids,
	       uid_t **agids,
	       size_t *nagids)
{
  if (! auth)
    return EOPNOTSUPP;

  OUTIDS (auth);

  return 0;
}

/* Implement auth_makeauth as described in <hurd/auth.defs>. */
kern_return_t
S_auth_makeauth (struct authhandle *auth,
		 mach_port_t *authpts, size_t nauths,
		 uid_t *euids, size_t neuids,
		 uid_t *auids, size_t nauids,
		 uid_t *egids, size_t negids,
		 uid_t *agids, size_t nagids,
		 mach_port_t *newhandle)
{
  struct authhandle *newauth, *auths[1 + nauths];
  int hasroot = 0;
  error_t err;
  size_t i, j;

  if (!auth)
    return EOPNOTSUPP;

  auths[0] = auth;

  /* Fetch the auth structures for all the ports passed in. */
  for (i = 0; i < nauths; i++)
    auths[i + 1] = auth_port_to_handle (authpts[i]);

  ++nauths;

  /* Verify that the union of the handles passed in either contains euid 0
     (root), or contains all the requested ids.  */

#define isuid(uid, auth) \
  (idvec_contains (&(auth)->euids, uid) \
   || idvec_contains (&(auth)->auids, uid))
#define groupmember(gid, auth) \
  (idvec_contains (&(auth)->egids, gid) \
   || idvec_contains (&(auth)->agids, gid))
#define isroot(auth)		isuid (0, auth)

  for (i = 0; i < nauths; i++)
    if (auths[i] && isroot (auths[i]))
      {
	hasroot = 1;
	break;
      }

  if (!hasroot)
    {
      int has_it;

      for (i = 0; i < neuids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (auths[j] && isuid (euids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    goto eperm;
	}

      for (i = 0; i < nauids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (auths[j] && isuid (auids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    goto eperm;
	}

      for (i = 0; i < negids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (auths[j] && groupmember (egids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    goto eperm;
	}

      for (i = 0; i < nagids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (auths[j] && groupmember (agids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    goto eperm;
	}
    }

  err = create_authhandle (&newauth);

  /* Create a new handle with the specified ids.  */

#define MERGE S (euids); S (egids); S (auids); S (agids);
#define S(uids) if (!err) err = idvec_merge_ids (&newauth->uids, uids, n##uids)

  MERGE;

#undef S

  if (! err)
    {
      for (j = 1; j < nauths; ++j)
	mach_port_deallocate (mach_task_self (), authpts[j - 1]);
      *newhandle = ports_get_right (newauth);
      ports_port_deref (newauth);
    }

  for (j = 1; j < nauths; j++)
    if (auths[j])
      ports_port_deref (auths[j]);
  return err;

 eperm:
  for (j = 1; j < nauths; j++)
    if (auths[j])
      ports_port_deref (auths[j]);
  return EPERM;
}

/* Transaction handling.  */

/* A pending transaction.  */
struct pending
  {
    hurd_ihash_locp_t locp;	/* Position in one of the ihash tables.  */
    struct condition wakeup;    /* The waiter is blocked on this condition.  */

    /* The user's auth handle.  */
    struct authhandle *user;

    /* The port to pass back to the user.  */
    mach_port_t passthrough;
  };

/* Table of pending transactions keyed on RENDEZVOUS.  */
struct hurd_ihash pending_users
  = HURD_IHASH_INITIALIZER (offsetof (struct pending, locp));
struct hurd_ihash pending_servers
  = HURD_IHASH_INITIALIZER (offsetof (struct pending, locp));
struct mutex pending_lock = MUTEX_INITIALIZER;

/* Implement auth_user_authenticate as described in <hurd/auth.defs>. */
kern_return_t
S_auth_user_authenticate (struct authhandle *userauth,
			  mach_port_t reply,
			  mach_msg_type_name_t reply_type,
			  mach_port_t rendezvous,
			  mach_port_t *newport,
			  mach_msg_type_name_t *newporttype)
{
  struct pending *s;

  if (! userauth)
    return EOPNOTSUPP;

  if (rendezvous == MACH_PORT_DEAD) /* Port died in transit.  */
    return EINVAL;

  mutex_lock (&pending_lock);

  /* Look for this port in the server list.  */
  s = hurd_ihash_find (&pending_servers, rendezvous);
  if (s)
    {
      /* Found it!  Extract the port.  */
      *newport = s->passthrough;
      *newporttype = MACH_MSG_TYPE_MOVE_SEND;

      /* Remove it from the pending list.  */
      hurd_ihash_locp_remove (&pending_servers, s->locp);

      /* Give the server the auth port and wake the RPC up.
	 We need to add a ref in case the port dies.  */
      s->user = userauth;
      ports_port_ref (userauth);

      condition_signal (&s->wakeup);
      mutex_unlock (&pending_lock);

      mach_port_deallocate (mach_task_self (), rendezvous);
      return 0;
    }
  else
    {
      /* No pending server RPC for this port.
	 Create a pending user RPC record.  */
      struct pending u;
      error_t err;

      err = hurd_ihash_add (&pending_users, rendezvous, &u);
      if (! err)
	{
	  /* Store the user auth port and wait for the server RPC to wake
             us up.  */
	  u.user = userauth;
	  condition_init (&u.wakeup);
	  ports_interrupt_self_on_port_death (userauth, rendezvous);
	  if (hurd_condition_wait (&u.wakeup, &pending_lock))
	    /* We were interrupted; remove our record.  */
	    {
	      hurd_ihash_locp_remove (&pending_users, u.locp);
	      err = EINTR;
	    }
	}
      /* The server side has already removed U from the ihash table.  */
      mutex_unlock (&pending_lock);

      if (! err)
	{
	  /* The server RPC has set the port and signalled U.wakeup.  */
	  *newport = u.passthrough;
	  *newporttype = MACH_MSG_TYPE_MOVE_SEND;
	  mach_port_deallocate (mach_task_self (), rendezvous);
	}
      return err;
    }
}

/* Implement auth_server_authenticate as described in <hurd/auth.defs>. */
kern_return_t
S_auth_server_authenticate (struct authhandle *serverauth,
			    mach_port_t reply,
			    mach_msg_type_name_t reply_type,
			    mach_port_t rendezvous,
			    mach_port_t newport,
			    mach_msg_type_name_t newport_type,
			    uid_t **euids,
			    size_t *neuids,
			    uid_t **auids,
			    size_t *nauids,
			    uid_t **egids,
			    size_t *negids,
			    uid_t **agids,
			    size_t *nagids)
{
  struct pending *u;
  struct authhandle *user;
  error_t err;

  if (! serverauth)
    return EOPNOTSUPP;

  if (rendezvous == MACH_PORT_DEAD) /* Port died in transit.  */
    return EINVAL;

  mutex_lock (&pending_lock);

  /* Look for this port in the user list.  */
  u = hurd_ihash_find (&pending_users, rendezvous);
  if (u)
    {
      /* Remove it from the pending list.  */
      hurd_ihash_locp_remove (&pending_users, u->locp);

      /* Found it!  We must add a ref because the one held by the
	 user RPC might die as soon as we unlock pending_lock.  */
      user = u->user;
      ports_port_ref (user);

      /* Give the user the new port and wake the RPC up.  */
      u->passthrough = newport;

      condition_signal (&u->wakeup);
      mutex_unlock (&pending_lock);
    }
  else
    {
      /* No pending user RPC for this port.
	 Create a pending server RPC record.  */
      struct pending s;

      err = hurd_ihash_add (&pending_servers, rendezvous, &s);
      if (! err)
	{
	  /* Store the new port and wait for the user RPC to wake us up.  */
	  s.passthrough = newport;
	  condition_init (&s.wakeup);
	  ports_interrupt_self_on_port_death (serverauth, rendezvous);
	  if (hurd_condition_wait (&s.wakeup, &pending_lock))
	    /* We were interrupted; remove our record.  */
	    {
	      hurd_ihash_locp_remove (&pending_servers, s.locp);
	      err = EINTR;
	    }
	}
      /* The user side has already removed S from the ihash table.  */
      mutex_unlock (&pending_lock);

      if (err)
	return err;

      /* The user RPC has set the port (with a ref) and signalled S.wakeup.  */
      user = s.user;
    }

  /* Extract the ids.  We must use a separate reply stub so
     we can deref the user auth handle after the reply uses its
     contents.  */
  err = auth_server_authenticate_reply (reply, reply_type, 0,
					user->euids.ids, user->euids.num,
					user->auids.ids, user->auids.num,
					user->egids.ids, user->egids.num,
					user->agids.ids, user->agids.num);

  ports_port_deref (user);
  if (err)
    return err;

  mach_port_deallocate (mach_task_self (), rendezvous);
  return MIG_NO_REPLY;
}



static int
auth_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int auth_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  return (auth_server (inp, outp) ||
	  ports_interrupt_server (inp, outp) ||
	  ports_notify_server (inp, outp));
}


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t boot;
  process_t proc;
  mach_port_t hostpriv, masterdev;
  struct authhandle *firstauth;
  struct argp argp = { 0, 0, 0, "Hurd standard authentication server." };

  argp_parse (&argp, argc, argv, 0, 0, 0);

  auth_bucket = ports_create_bucket ();
  authhandle_portclass = ports_create_class (&destroy_authhandle, 0);

  /* Create the initial root auth handle.  */
  err = create_authhandle (&firstauth);
  assert_perror (err);
  idvec_add (&firstauth->euids, 0);
  idvec_add (&firstauth->auids, 0);
  idvec_add (&firstauth->auids, 0);
  idvec_merge (&firstauth->egids, &firstauth->euids);
  idvec_merge (&firstauth->agids, &firstauth->auids);

  /* Fetch our bootstrap port and contact the bootstrap filesystem.  */
  err = task_get_bootstrap_port (mach_task_self (), &boot);
  assert_perror (err);
  if (boot == MACH_PORT_NULL)
    error (2, 0, "auth server can only be run by init during boot");
  err = startup_authinit (boot, ports_get_right (firstauth),
			  MACH_MSG_TYPE_MAKE_SEND, &proc);
  if (err)
    error (2, err, "cannot contact init for bootstrap");

  /* Register ourselves with the proc server and then start signals.  */
  proc_getprivports (proc, &hostpriv, &masterdev);
  proc_register_version (proc, hostpriv, "auth", "", HURD_VERSION);
  mach_port_deallocate (mach_task_self (), masterdev);
  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], proc);
  _hurd_proc_init (argv, NULL, 0);

  /* Init knows intimately that we will be ready for messages
     as soon as this returns. */
  startup_essential_task (boot, mach_task_self (), MACH_PORT_NULL, "auth",
			  hostpriv);
  mach_port_deallocate (mach_task_self (), boot);
  mach_port_deallocate (mach_task_self (), hostpriv);

  /* Be a server.  */
  while (1)
    ports_manage_port_operations_multithread (auth_bucket,
					      auth_demuxer,
					      30 * 1000, 0, 0);
}
