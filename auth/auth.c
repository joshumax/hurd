/* Authentication server
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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

/* Written by Michael I. Bushnell.  */

#include "auth_S.h"
#include "auth_reply_U.h"
#include "notify_S.h"
#include <hurd/startup.h>
#include <mach/error.h>
#include <errno.h>
#include <mach/mig_errors.h>
#include <mach/notify.h>
#include <stdlib.h>
#include <hurd.h>
#include <mach.h>
#include <string.h>

/* This represents a request from a call to auth_user_authenticate
   which is waiting for the corresponding call.  */
struct saved_user
{
  struct saved_user *next;	/* hash link */
  
  mach_port_t rendezvous;	/* rendezvous port */
  mach_port_t rendezvous2;	/* secondary rendezvous port */

  struct apt *userid;		/* to be passed to server */
  
  mach_port_t reply;		/* where to send the answer */
  mach_msg_type_name_t reply_type; /* type of reply port */
};

/* This represents a request from a call to auth_server_authenticate
   which is waiting for the corresponding call.  */
struct saved_server
{
  struct saved_server *next;	/* hash link */
  
  mach_port_t rendezvous;	/* rendezvous port */
  mach_port_t rendezvous2;	/* secondary rendezvous port */

  struct apt *server;		/* who made the call? */
  mach_port_t passthrough;	/* new port to be given to user */

  mach_port_t reply;		/* where to send the answer */
  mach_msg_type_name_t reply_type; /* type of reply port */
};

struct saved_user *userlist;
struct saved_server *serverlist;

struct apt
{
  mach_port_t port;
  uid_t *gen_uids, *aux_uids;
  uid_t *gen_gids, *aux_gids;
  int ngen_uids, naux_uids, ngen_gids, naux_gids;
  struct apt *next, **prevp;
};

struct apt *allapts;

struct apt *getauthstruct (void);
void auth_nosenders (struct apt *);

mach_port_t auth_pset;

/* Our version number */
char *auth_version = "0.0 pre-alpha";


/* Demultiplex an incoming message. */
int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  extern int auth_server (), notify_server ();
  
  return (auth_server (inp, outp) 
	  || notify_server (inp, outp));
  
}

void
main (int argc, char **argv)
{
  mach_port_t boot;
  struct apt *firstauth;
  process_t proc;
  extern int mach_msg_server ();
  mach_port_t hostpriv, masterdev;
  
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &auth_pset);

  task_get_bootstrap_port (mach_task_self (), &boot);

  firstauth = getauthstruct ();
  firstauth->gen_uids = malloc (sizeof (uid_t) * 1);
  firstauth->aux_uids = malloc (sizeof (uid_t) * 2);
  firstauth->gen_gids = malloc (sizeof (uid_t) * 1);
  firstauth->aux_gids = malloc (sizeof (uid_t) * 2);
  firstauth->gen_uids[0] = 0;
  firstauth->aux_uids[0] = firstauth->aux_uids[1] = 0;
  firstauth->gen_gids[0] = 0;
  firstauth->aux_gids[0] = firstauth->aux_gids[1] = 0;
  firstauth->ngen_uids = firstauth->ngen_gids = 1;
  firstauth->naux_uids = firstauth->naux_gids = 2;

  startup_authinit (boot, firstauth->port, MACH_MSG_TYPE_MAKE_SEND, &proc);
  proc_getprivports (proc, &hostpriv, &masterdev);
  proc_register_version (proc, hostpriv, "auth", HURD_RELEASE, auth_version);
  mach_port_deallocate (mach_task_self (), masterdev);
  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], proc);
  _hurd_proc_init (argv);

  /* Init knows intimately that we will be ready for messages
     as soon as this returns. */
  startup_essential_task (boot, mach_task_self (), MACH_PORT_NULL, "auth",
			  hostpriv);
  mach_port_deallocate (mach_task_self (), boot);
  mach_port_deallocate (mach_task_self (), hostpriv);

  while (1)
    mach_msg_server (request_server, vm_page_size * 2, auth_pset);
}


/* Server routines: */

/* Called when an auth port has no more senders. */
error_t
do_mach_notify_no_senders (mach_port_t notify,
			      mach_port_mscount_t mscount)
{
  auth_nosenders (convert_auth_to_apt (notify));
  return 0;
}

/* A given auth handle has no more senders; deallocate all state referring
   to this handle.  */
void
auth_nosenders (struct apt *auth)
{
  /* Remove all references to this from the saved lists */
  {
    struct saved_user *s, **prevp, *tmp;
    for (s = userlist, prevp = &userlist; s;)
      {
	if (s->userid == auth)
	  {
	    *prevp = s->next;
	    tmp = s;
	    prevp = &s->next;
	    s = s->next;
	    
	    mach_port_deallocate (mach_task_self (), tmp->rendezvous);
	    mach_port_deallocate (mach_task_self (), tmp->reply);
	    free (tmp);
	  }
	else
	  {
	    prevp = &s->next;
	    s = s->next;
	  }
      }
  }
  {
    struct saved_server *s, **prevp, *tmp;
    for (s = serverlist, prevp = &serverlist; s;)
      {
	if (s->server == auth)
	  {
	    *prevp = s->next;
	    tmp = s;
	    prevp = &s->next;
	    s = s->next;
	    
	    mach_port_deallocate (mach_task_self (), tmp->rendezvous);
	    mach_port_deallocate (mach_task_self (), tmp->reply);
	    if (tmp->passthrough)
	      mach_port_deallocate (mach_task_self (), tmp->passthrough);
	    free (tmp);
	  }
	else
	  {
	    prevp = &s->next;
	    s = s->next;
	  }
      }
  }

  /* Take off allapts list */
  *auth->prevp = auth->next;
  if (auth->next)
    auth->next->prevp = auth->prevp;

  /* Delete its storage and port. */
  mach_port_mod_refs (mach_task_self (), auth->port, 
		      MACH_PORT_RIGHT_RECEIVE, -1);
  free (auth->gen_uids);
  free (auth->aux_uids);
  free (auth->gen_gids);
  free (auth->aux_gids);
  free (auth);
}


/* Return true iff TEST is a genuine or auxiliary group id in AUTH. */
inline int
groupmember (gid_t test, 
	     struct apt *auth)
{
  int i;
  
  for (i = 0; i < auth->ngen_gids; i++)
    if (test == auth->gen_gids[i])
      return 1;
  for (i = 0; i < auth->naux_gids; i++)
    if (test == auth->aux_gids[i])
      return 1;
  return 0;
}

/* Return true iff TEST is a genuine or auxiliary uid in AUTH. */
inline int
isuid (uid_t test,
       struct apt *auth)
{
  int i;
  
  for (i = 0; i < auth->ngen_uids; i++)
    if (test == auth->gen_uids[i])
      return 1;
  for (i = 0; i < auth->naux_uids; i++)
    if (test == auth->aux_uids[i])
      return 1;
  return 0;
}

/* Return true if the the given auth handle is root (uid 0).  */
inline int
isroot (struct apt *idblock)
{
  return isuid (0, idblock);
}

/* Allocate a new auth handle, complete with port. */
struct apt *
getauthstruct ()
{
  struct apt *newauth;
  mach_port_t unused;

  newauth = malloc (sizeof (struct apt));
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &newauth->port);
  mach_port_request_notification (mach_task_self (), newauth->port,
				  MACH_NOTIFY_NO_SENDERS, 1, newauth->port,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &unused);
  mach_port_move_member (mach_task_self (), newauth->port, auth_pset);
  newauth->next = allapts;
  if (newauth->next)
    newauth->next->prevp = &newauth->next;
  newauth->prevp = &allapts;
  allapts = newauth;
  return newauth;
}
  


/* Client requests */

/* Implement auth_getids as described in <hurd/auth.defs>. */
kern_return_t
S_auth_getids (struct apt *auth,
	       uid_t **gen_uids,
	       u_int *ngen_uids,
	       uid_t **aux_uids,
	       u_int *naux_uids,
	       uid_t **gen_gids,
	       u_int *ngen_gids,
	       uid_t **aux_gids,
	       u_int *naux_gids)
{
  if (!auth)
    return EOPNOTSUPP;
  
  if (auth->ngen_uids > *ngen_uids)
    *gen_uids = auth->gen_uids;
  else
    bcopy (auth->gen_uids, *gen_uids, sizeof (uid_t) * auth->ngen_uids);
  *ngen_uids = auth->ngen_uids;

  if (auth->naux_uids > *naux_uids)
    *aux_uids = auth->aux_uids;
  else
    bcopy (auth->aux_uids, *aux_uids, sizeof (uid_t) * auth->naux_uids);
  *naux_uids = auth->naux_uids;

  if (auth->ngen_gids > *ngen_gids)
    *gen_gids = auth->gen_gids;
  else
    bcopy (auth->gen_gids, *gen_gids, sizeof (uid_t) * auth->ngen_gids);
  *ngen_gids = auth->ngen_gids;

  if (auth->naux_gids > *naux_gids)
    *aux_gids = auth->aux_gids;
  else
    bcopy (auth->aux_gids, *aux_gids, sizeof (uid_t) * auth->naux_gids);
  *naux_gids = auth->naux_gids;

  return 0;
}

/* Implement auth_makeauth as described in <hurd/auth.defs>. */
kern_return_t
S_auth_makeauth (struct apt *auth,
		 mach_port_t *authpts,
		 u_int nauths,
		 uid_t *gen_uids,
		 u_int ngen_uids,
		 uid_t *aux_uids,
		 u_int naux_uids,
		 uid_t *gen_gids,
		 u_int ngen_gids,
		 uid_t *aux_gids,
		 u_int naux_gids,
		 mach_port_t *newhandle)
{
  int i, j;
  struct apt *newauth;
  struct apt **auths = alloca ((nauths + 1) * sizeof (struct apt *));
  int hasroot = 0;

  if (!auth)
    return EOPNOTSUPP;
  auths[0] = auth;

  for (i = 0; i < nauths; i++)
    if (!(auths[i + 1] = convert_auth_to_apt (authpts[i])))
      return EINVAL;

  nauths++;

  for (i = 0; i < nauths; i++)
    if (isroot (auth))
      {
	hasroot = 1;
	break;
      }
  
  if (!hasroot)
    {
      int has_it;
      
      for (i = 0; i < ngen_uids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (isuid (gen_uids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    return EPERM;
	}
      
      for (i = 0; i < naux_uids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (isuid (aux_uids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    return EPERM;
	}
      
      for (i = 0; i < ngen_gids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (groupmember (gen_gids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    return EPERM;
	}
	    
      for (i = 0; i < naux_gids; i++)
	{
	  has_it = 0;
	  for (j = 0; j < nauths; j++)
	    if (groupmember (aux_gids[i], auths[j]))
	      {
		has_it = 1;
		break;
	      }
	  if (!has_it)
	    return EPERM;
	}
    }  

  for (j = 0; j < nauths - 1; j++)
    mach_port_deallocate (mach_task_self (), authpts[j]);

  newauth = getauthstruct ();
  newauth->gen_uids = malloc (sizeof (uid_t) * ngen_uids);
  newauth->aux_uids = malloc (sizeof (uid_t) * naux_uids);
  newauth->gen_gids = malloc (sizeof (uid_t) * ngen_gids);
  newauth->aux_gids = malloc (sizeof (uid_t) * naux_gids);
  newauth->ngen_uids = ngen_uids;
  newauth->naux_uids = naux_uids;
  newauth->ngen_gids = ngen_gids;
  newauth->naux_gids = naux_gids;
  bcopy (gen_uids, newauth->gen_uids, ngen_uids * sizeof (uid_t));
  bcopy (aux_uids, newauth->aux_uids, naux_uids * sizeof (uid_t));
  bcopy (gen_gids, newauth->gen_gids, ngen_gids * sizeof (uid_t));
  bcopy (aux_gids, newauth->aux_gids, naux_gids * sizeof (uid_t));
  
  *newhandle = newauth->port;
  return 0;
}

/* Implement auth_user_authenticate as described in <hurd/auth.defs>. */
kern_return_t
S_auth_user_authenticate (struct apt *userauth,
			  mach_port_t reply,
			  mach_msg_type_name_t reply_porttype,
			  mach_port_t rendezvous,
			  mach_port_t rendezvous2,
			  mach_port_t *newport,
			  mach_msg_type_name_t *newporttype)
{
  struct saved_server *sv, **spp;
  struct saved_user *u;
  
  if (!userauth)
    return EOPNOTSUPP;
  
  /* Look for this port in the server list.  */
  for (sv = serverlist, spp = &serverlist; sv;)
    {
      if (sv->rendezvous == rendezvous && sv->rendezvous2 == rendezvous2)
	{
	  *spp = sv->next;
	  break;
	}
      else
	{
	  spp = &sv->next;
	  sv = sv->next;
	}
    }
  
  if (sv)
    {
      *newport = sv->passthrough;
      *newporttype = MACH_MSG_TYPE_MOVE_SEND;
      auth_server_authenticate_reply (sv->reply, sv->reply_type, 0,
				      userauth->gen_uids, userauth->ngen_uids,
				      userauth->aux_uids, userauth->naux_uids,
				      userauth->gen_gids, userauth->ngen_uids,
				      userauth->aux_gids, userauth->naux_uids);
      free (sv);
      
      /* Drop both rights from the call.  */
      mach_port_mod_refs (mach_task_self (), rendezvous, MACH_PORT_RIGHT_SEND,
			  -2);
      mach_port_mod_refs (mach_task_self (), rendezvous2, MACH_PORT_RIGHT_SEND,
			  -2);
      return 0;
    }

  /* User got here first.  */
  u = malloc (sizeof (struct saved_user));
  
  u->rendezvous = rendezvous;
  u->rendezvous2 = rendezvous2;
  u->userid = userauth;

  u->reply = reply;
  u->reply_type = reply_porttype;
  
  u->next = userlist;
  userlist = u;
  
  return MIG_NO_REPLY;
}

/* Implement auth_server_authenticate as described in <hurd/auth.defs>. */
kern_return_t
S_auth_server_authenticate (struct apt *serverauth,
			    mach_port_t reply,
			    mach_msg_type_name_t reply_porttype,
			    mach_port_t rendezvous,
			    mach_port_t rendezvous2,
			    mach_port_t newport,
			    uid_t **gen_uids,
			    u_int *ngen_uids,
			    uid_t **aux_uids,
			    u_int *naux_uids,
			    uid_t **gen_gids,
			    u_int *ngen_gids,
			    uid_t **aux_gids,
			    u_int *naux_gids)
{
  struct saved_user *su, **sup;
  struct saved_server *v;
  struct apt *uauth;
  
  if (!serverauth)
    return EOPNOTSUPP;

  /* Look for this port in the user list.  */
  for (su = userlist, sup = &userlist; su;)
    {
      if (su->rendezvous == rendezvous && su->rendezvous2 == rendezvous2)
	{
	  *sup = su->next;
	  break;
	}
      else
	{
	  sup = &su->next;
	  su = su->next;
	}
    }
  if (su)
    {
      auth_user_authenticate_reply (su->reply, su->reply_type, 0, newport,
				    MACH_MSG_TYPE_MOVE_SEND);
      
      uauth = su->userid;
      
      if (uauth->ngen_uids > *ngen_uids)
	*gen_uids = uauth->gen_uids;
      else
	bcopy (uauth->gen_uids, *gen_uids, sizeof (uid_t) * uauth->ngen_uids);
      *ngen_uids = uauth->ngen_uids;

      if (uauth->naux_uids > *naux_uids)
	*aux_uids = uauth->aux_uids;
      else
	bcopy (uauth->aux_uids, *aux_uids, sizeof (uid_t) * uauth->naux_uids);
      *naux_uids = uauth->naux_uids;

      if (uauth->ngen_gids > *ngen_gids)
	*gen_gids = uauth->gen_gids;
      else
	bcopy (uauth->gen_gids, *gen_gids, sizeof (uid_t) * uauth->ngen_gids);
      *ngen_gids = uauth->ngen_gids;

      if (uauth->naux_gids > *naux_gids)
	*aux_gids = uauth->aux_gids;
      else
	bcopy (uauth->aux_gids, *aux_gids, sizeof (uid_t) * uauth->naux_gids);
      *naux_gids = uauth->naux_gids;

      free (su);

      mach_port_mod_refs (mach_task_self (), rendezvous, MACH_PORT_RIGHT_SEND,
			  -2);
      mach_port_mod_refs (mach_task_self (), rendezvous2, MACH_PORT_RIGHT_SEND,
			  -2);
      return 0;
    }
  
  /* Server got here first.  */
  v = malloc (sizeof (struct saved_server));
  
  v->rendezvous = rendezvous;
  v->rendezvous2 = rendezvous2;
  v->passthrough = newport;
  
  v->reply = reply;
  v->reply_type = reply_porttype;
  
  v->next = serverlist;
  serverlist = v;
  
  return MIG_NO_REPLY;
}

/* Convert an auth port into an auth handle structure. */
struct apt *
convert_auth_to_apt (auth_t auth)
{
  struct apt *a;
  for (a = allapts; a; a = a->next)
    if (a->port == auth)
      return a;
  return 0;
}

/* Unneeded notification stubs: */
kern_return_t
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_send_once (mach_port_t notify)
{
  return 0;
}

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
  return 0;
}

