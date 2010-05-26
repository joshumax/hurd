/* fakeauth -- proxy auth server to lie to users about what their IDs are
   Copyright (C) 2002, 2010 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <hurd.h>
#include <hurd/auth.h>
#include <hurd/interrupt.h>
#include <hurd/ports.h>
#include <idvec.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <assert-backtrace.h>
#include <argp.h>
#include <error.h>
#include "auth_S.h"
#include "auth_request_U.h"
#include "interrupt_S.h"

#include "../auth/auth.h"

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
    memset (&(*new)->euids, 0, (void *)&(*new)[1] - (void *)&(*new)->euids);
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


/* This is our original auth port, where we will send all proxied
   authentication requests.  */
static auth_t real_auth_port;

kern_return_t
S_auth_user_authenticate (struct authhandle *userauth,
			  mach_port_t reply,
			  mach_msg_type_name_t reply_type,
			  mach_port_t rendezvous,
			  mach_port_t *newport,
			  mach_msg_type_name_t *newporttype)
{
  if (! userauth)
    return EOPNOTSUPP;

  if (rendezvous == MACH_PORT_DEAD) /* Port died in transit.  */
    return EINVAL;

  return auth_user_authenticate_request (real_auth_port, reply, reply_type,
					 rendezvous, MACH_MSG_TYPE_MOVE_SEND)
    ? : MIG_NO_REPLY;
}

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
  if (! serverauth)
    return EOPNOTSUPP;

  if (rendezvous == MACH_PORT_DEAD) /* Port died in transit.  */
    return EINVAL;

  return auth_server_authenticate_request (real_auth_port,
					   reply, reply_type,
					   rendezvous, MACH_MSG_TYPE_MOVE_SEND,
					   newport, newport_type)
    ? : MIG_NO_REPLY;
}

kern_return_t
S_interrupt_operation (mach_port_t port, mach_port_seqno_t seqno)
{
  return interrupt_operation (real_auth_port, 0);
}

#include "../libports/notify_S.h"

static int
auth_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = auth_server_routine (inp)) ||
      (routine = interrupt_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}


static void *
handle_auth_requests (void *ignored)
{
  while (1)
    ports_manage_port_operations_multithread (auth_bucket, auth_demuxer,
					      30 * 1000, 0, 0);
  return 0;
}

int
main (int argc, char **argv)
{
  error_t err;
  struct authhandle *firstauth;
  auth_t authport;
  pid_t child;
  pthread_t thread;
  int status;
  int argi;

  /* Parse our options.  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
        {
        case ARGP_KEY_NO_ARGS:
          argp_usage (state);
          return EINVAL;
        default:
          return ARGP_ERR_UNKNOWN;
        }
      return 0;
    }
  struct argp argp = { 0, parse_opt, "COMMAND...", "\
Run COMMAND with a fake authentication handle that claims to be root or \
any arbitrary identity derived from that handle, but in fact is always just \
a proxy for your real authentication handle.  This means that all processes \
created by the COMMAND will have your privileges, even though it may \
believe it has restricted them to different identities or no identity at all.\
" };

  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, &argi, 0);

  auth_bucket = ports_create_bucket ();
  authhandle_portclass = ports_create_class (&destroy_authhandle, 0);

  /* Create the initial root auth handle.  */
  err = create_authhandle (&firstauth);
  assert_perror_backtrace (err);
  idvec_add (&firstauth->euids, 0);
  idvec_add (&firstauth->auids, 0);
  idvec_add (&firstauth->auids, 0);
  idvec_merge (&firstauth->egids, &firstauth->euids);
  idvec_merge (&firstauth->agids, &firstauth->auids);

  /* Get a send right for it.  */
  authport = ports_get_right (firstauth);
  err = mach_port_insert_right (mach_task_self (), authport, authport,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror_backtrace (err);
  ports_port_deref (firstauth);

  /* Stash our original auth port for later use.  */
  real_auth_port = getauth ();

  /* Start handling auth requests on our fake handles.  */
  err = pthread_create (&thread, NULL, &handle_auth_requests, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  /* Now we start faking ourselves out.  This will immediately
     reauthenticate all our descriptors through our proxy auth port.
     The POSIXoid calls we make below will continue to use the fake
     port and pass it on to the child.  */
  if (setauth (authport))
    error (2, errno, "Cannot switch to fake auth handle");
  mach_port_deallocate (mach_task_self (), authport);

  /* We cannot use fork because it doesn't do the right thing with our send
     rights that point to our own receive rights, i.e. the new auth port.
     Since posix_spawn might be implemented with fork (prior to glibc 2.3),
     we cannot use that simple interface either.  We use _hurd_exec_paths
     directly to effect what posix_spawn does in the simple case.  */
  {
    task_t newtask;
    process_t proc;
    char *prefixed_name;
    file_t execfile = file_name_path_lookup (argv[argi], getenv ("PATH"),
					     O_EXEC, 0, &prefixed_name);
    if (execfile == MACH_PORT_NULL)
      error (3, errno, "%s", argv[argi]);
    if (prefixed_name)
      argv[argi] = prefixed_name;

    err = task_create (mach_task_self (),
#ifdef KERN_INVALID_LEDGER
			 NULL, 0,	/* OSF Mach */
#endif
			 0, &newtask);
    if (err)
      error (3, err, "cannot create child task");
    child = task2pid (newtask);
    if (child < 0)
      error (3, errno, "task2pid");
    proc = getproc ();
    err = proc_child (proc, newtask);
    mach_port_deallocate (mach_task_self (), proc);
    if (err)
      error (3, err, "proc_child");

#ifdef HAVE__HURD_EXEC_PATHS
    err = _hurd_exec_paths (newtask, execfile, argv[argi], argv[argi],
			    &argv[argi], environ);
#else
    err = _hurd_exec (newtask, execfile, &argv[argi], environ);
#endif
    mach_port_deallocate (mach_task_self (), newtask);
    mach_port_deallocate (mach_task_self (), execfile);
    if (err)
      error (3, err, "cannot execute %s", argv[argi]);
  }

  if (waitpid (child, &status, 0) != child)
    error (4, errno, "waitpid on %d", child);

  if (WIFSIGNALED (status))
    error (WTERMSIG (status) + 128, 0,
	   "%s for child %d", strsignal (WTERMSIG (status)), child);
  if (WEXITSTATUS (status) != 0)
    error (WEXITSTATUS (status), 0,
	   "Error %d for child %d", WEXITSTATUS (status), child);

  return 0;
}
