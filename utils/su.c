/* `su' for GNU Hurd.
   Copyright (C) 1994, 1995, 1996 Free Software Foundation
   Written by Roland McGrath.

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
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

   Unlike Unix su, this program does not spawn a subshell.  Instead, it
   adds or removes (with the -r flag) auth rights to all the processes in
   the current login.  Note that the addition and removal of rights to a
   particular process is voluntary; the process doesn't get them unless
   it's listening for the right rpc (which the C library normally does),
   and it doesn't have to release any of the rights it has when requested
   (though the C library does this automatically in most programs).

   The -r flag allows you to easily be authorized or not authorized for any
   number of users (uids and gid sets).  This program is not intelligent,
   however.  The -r flag always removes whatever gids the specified user
   has.  If some other user you have obtained authorization for has some of
   the same gids, it will remove them.  The C library could be intelligent
   enough to look up the groups of all the uids it has, and make sure it
   has at least those.  */

#include <stdlib.h>
#include <hurd.h>
#include <pwd.h>
#include <grp.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <hurd/msg.h>
#include <assert.h>

process_t proc;			/* Our proc server port. */

/* Command line flags.  */
int remove_ids = 0;		/* -r: Remove ids instead of adding them.  */

const struct option longopts[] =
  {
    { "remove", 0, 0, 'r' },
    { "uid", 0, 0, 'u' },
    { "gid", 0, 0, 'g' },
    { "loginuser", 0, 0, 'l' },
    { "pid", 0, 0, 'p' },
    { "pgrp", 0, 0, 'G' },
    { "loginid", 0, 0, 'i' },
    { 0, }
  };

struct auth
  {
    unsigned int *ids;
    auth_t authport;
  };

/* These functions return a malloc'd array of ids that can be passed to
   make_auth_handle or used in del_auth messages.  They check authorization
   and return NULL on failure.  */
unsigned int *get_uid (const char *user); /* From a named user or UID.  */
unsigned int *get_gid (const char *user); /* From a named group or GID.  */
unsigned int *get_user (const char *user); /* All ids for a named user.  */

int apply_auth (struct auth *auth, pid_t, int);
int apply_auth_to_pids (struct auth *auth, unsigned int, pid_t *, int);
int apply_auth_to_pgrp (struct auth *auth, pid_t);
int apply_auth_to_loginid (struct auth *auth, int);

int check_password (const char *name, const char *password);


int
main (int argc, char **argv)
{
  int c;
  int status;
  enum { user, group, loginuser, loginid, pid, pgrp } mode = loginuser;
  struct auth auth[argc];
  unsigned int authidx = 0, loginidx = 0, pididx = 0, pgrpidx = 0;
  int loginids[argc];
  pid_t pids[argc], pgrps[argc];
  unsigned int i, j;

  proc = getproc ();		/* Used often.  */

  while ((c = getopt_long (argc, argv, "--rgulpiG", longopts, NULL)) != EOF)
    switch (c)
      {
      case 'r':
	remove_ids = 1;
	break;

      case 'u':
	mode = user;
	break;
      case 'g':
	mode = group;
	break;
      case 'l':
	mode = loginuser;
	break;
      case 'p':
	mode = pid;
	break;
      case 'i':
	mode = loginid;
	break;
      case 'G':
	mode = pgrp;
	break;

      case 1:		/* Non-option argument.  */
	switch (mode)
	  {
	  case user:
	    auth[authidx++].ids = get_uid (optarg);
	    break;
	  case group:
	    auth[authidx++].ids = get_gid (optarg);
	    break;
	  case loginuser:
	    auth[authidx++].ids = get_user (optarg);
	    break;

	  case pid:
	    pids[pididx++] = atoi (optarg);
	    break;
	  case loginid:
	    loginids[loginidx++] = atoi (optarg);
	    break;
	  case pgrp:
	    pgrps[pgrpidx++] = atoi (optarg);
	    break;
	  }
	break;

      case '?':
      default:
	fprintf (stderr, "Usage: %s [-r|--remove]\n\
	[-u|--uid UID...] [-g|--gid GID...] [-l|--loginuser USER...]\n\
	[-p|--pid PID...] [-i|--loginid ID...] [-G|--pgrp PGRP...]\n",
		 program_invocation_short_name);
	exit (1);
      }

  if (authidx == 0)
    /* No ids specified; default is "su root".  */
    auth[authidx++].ids = get_user ("root");

  if (pididx == 0 && loginidx == 0 && pgrpidx == 0)
    {
      /* No processes specified; default is current login collection.  */
      errno = proc_getloginid (proc, getpid (), &loginids[loginidx++]);
      if (errno)
	{
	  perror ("proc_getloginid");
	  return 1;
	}
    }

  status = 0;

  if (! remove_ids)
    for (i = 0; i < authidx; ++i)
      {
	struct auth *a = &auth[i];
	errno = auth_makeauth (getauth (), NULL, MACH_MSG_TYPE_COPY_SEND, 0,
			       &a->ids[1], a->ids[0], NULL, 0,
			       &a->ids[1 + a->ids[0] + 1],
			       a->ids[1 + a->ids[0]], NULL, 0, &a->authport);
	if (errno)
	  {
	    perror ("auth_makeauth");
	    status = 1;
	  }
      }

  for (j = 0; j < authidx; ++j)
    status |= apply_auth_to_pids (&auth[j], pididx, pids, 0);

  for (i = 0; i < loginidx; ++i)
    {
      for (j = 0; j < authidx; ++j)
	status |= apply_auth_to_loginid (&auth[j], loginids[i]);
    }

  for (i = 0; i < pgrpidx; ++i)
    {
      for (j = 0; j < authidx; ++j)
	status |= apply_auth_to_loginid (&auth[j], pgrps[i]);
    }
		

  return status;
}

/* Turn USER into a list of ids, giving USER's primary uid and gid from the
   passwd file and all the groups that USER appears in in the group file.  */

unsigned int *
get_user (const char *user)
{
  struct passwd *p;
  unsigned int ngids, *ids;

  p = getpwnam (user);
  if (p == NULL)
    {
      fprintf (stderr, "%s: User `%s' not found\n",
	       program_invocation_short_name, user);
      return NULL;
    }

  if (! check_password (user, p->pw_passwd))
    return NULL;

  /* We don't need to change our own gids, but it doesn't really hurt,
     and initgroups does all the work for us.  */
  if (initgroups (user, p->pw_gid) < 0)
    {
      perror ("initgroups");
      return NULL;
    }
  ngids = getgroups (0, NULL);
  if ((int) ngids < 0)
    {
    getgroups_lost:
      perror ("getgroups");
      return NULL;
    }
  assert (sizeof (*ids) == sizeof (gid_t));
  ids = malloc ((3 + ngids) * sizeof (*ids));
  ids[0] = 1;
  ids[1] = p->pw_uid;
  ids[2] = getgroups (ngids, &ids[3]);
  if ((int) ids[2] < 0)
    goto getgroups_lost;

  return ids;
}

/* Return an id list containing just the uid of USER.  */

unsigned int *
get_uid (const char *user)
{
  struct passwd *p;
  unsigned int *ids;
  uid_t uid;
  char *uend;

  uid = strtoul (user, &uend, 10);
  if (uend && *uend == '\0')
    {
      if (remove_ids || getuid () == 0)
	/* No need to verify.  */
	p = NULL;
      else
	{
	  p = getpwuid (uid);
	  if (p == NULL)
	    {
	      fprintf (stderr, "%s: UID %u not found\n",
		       program_invocation_short_name, uid);
	      return NULL;
	    }
	}
    }
  else
    {
      p = getpwnam (user);
      if (p == NULL)
	{
	  fprintf (stderr, "%s: User `%s' not found\n",
		   program_invocation_short_name, user);
	  return NULL;
	}
    }

  if (p != NULL && ! check_password (user, p->pw_passwd))
    return NULL;

  ids = malloc (3 * sizeof (*ids));
  ids[0] = 1;
  ids[1] = p->pw_uid;
  ids[2] = 0;

  return ids;
}

/* Return an id list containing just the gid of GROUP.  */

unsigned int *
get_gid (const char *group)
{
  struct group *g;
  unsigned int *ids;
  gid_t gid;
  char *gend;

  gid = strtoul (group, &gend, 10);
  if (gend && *gend == '\0')
    {
      if (remove_ids || getuid () == 0)
	/* No need to verify.  */
	g = NULL;
      else
	{
	  g = getgrgid (gid);
	  if (g == NULL)
	    {
	      fprintf (stderr, "%s: GID %u not found\n",
		       program_invocation_short_name, gid);
	      return NULL;
	    }
	}
    }
  else
    {
      g = getgrnam (group);
      if (g == NULL)
	{
	  fprintf (stderr, "%s: Group `%s' not found\n",
		   program_invocation_short_name, group);
	  return NULL;
	}
    }

  if (g != NULL && ! check_password (group, g->gr_passwd))
    return NULL;

  ids = malloc (3 * sizeof (*ids));
  ids[0] = 0;
  ids[1] = 1;
  ids[2] = g->gr_gid;

  return ids;
}

/* Add or delete (under -r) the ids indicated by AUTH to/from PID.  If
   IGNORE_BAD_PID is nonzero, return success if PID does not exist.
   Returns zero if successful, nonzero on error (after printing message).  */

int
apply_auth (struct auth *auth, pid_t pid, int ignore_bad_pid)
{
  error_t err;

  if (! auth->ids)
    return 0;

  err = HURD_MSGPORT_RPC (proc_getmsgport (proc, pid, &msgport),
			  proc_pid2task (proc, pid, &refport), 1,
			  remove_ids ?
			  msg_del_auth (msgport, refport,
					&auth->ids[1], auth->ids[0],
					&auth->ids[1 + auth->ids[0] + 1],
					auth->ids[1 + auth->ids[0]]) :
			  msg_add_auth (msgport, auth->authport));
  if (err &&
      (!ignore_bad_pid || (err != ESRCH && err != MIG_SERVER_DIED)))
    {
      fprintf (stderr, "%s: error in %s_auth from PID %d: %s\n",
	       program_invocation_short_name,
	       remove_ids ? "del" : "add", pid, strerror (err));
      return 1;
    }
  else
    return 0;
}

int
apply_auth_to_pids (struct auth *auth, unsigned int npids, pid_t pids[],
		   int ignore_bad_pid)
{
  int status = 0;
  unsigned int i;

  for (i = 0; i < npids; ++i)
    status |= apply_auth (auth, pids[i], ignore_bad_pid);

  return status;
}

int
apply_auth_to_loginid (struct auth *auth, int loginid)
{
  unsigned int npids = 20;
  pid_t pidbuf[20], *pids = pidbuf;
  int status;
  error_t err;

  err = proc_getloginpids (proc, loginid, &pids, &npids);
  if (err)
    {
      fprintf (stderr, "%s: proc_getloginpids failed for loginid %d: %s\n",
	       program_invocation_short_name, loginid, strerror (err));
      return 1;
    }

  status = apply_auth_to_pids (auth, npids, pids, 1);

  if (pids != pidbuf)
    vm_deallocate (mach_task_self (),
		   (vm_address_t) pids, npids * sizeof (pid_t));

  return status;
}

int
apply_auth_to_pgrp (struct auth *auth, pid_t pgrp)
{
  unsigned int npids = 20;
  pid_t pidbuf[20], *pids = pidbuf;
  int status;
  error_t err;

  err = proc_getpgrppids (proc, pgrp, &pids, &npids);
  if (err)
    {
      fprintf (stderr, "%s: proc_getpgrppids failed for pgrp %d: %s\n",
	       program_invocation_short_name, pgrp, strerror (err));
      return 1;
    }

  status = apply_auth_to_pids (auth, npids, pids, 1);

  if (pids != pidbuf)
    vm_deallocate (mach_task_self (),
		   (vm_address_t) pids, npids * sizeof (pid_t));

  return status;
}

/* Return 1 if the user gives the correct password matching the encrypted
   string PASSWORD, 0 if not.  Return 1 without asking for a password if
   run by uid 0 or if PASSWORD is an empty password, and always under -r.
   Always prints a message before returning 0.  */
int
check_password (const char *name, const char *password)
{
  extern char *crypt (const char *string, const char salt[2]);
#pragma weak crypt
  char *unencrypted, *encrypted;
  static char *prompt = NULL;

  if (remove_ids || getuid () == 0 || password == NULL || password[0] == '\0')
    return 1;

  asprintf (&prompt, "%s's Password:", name);
  unencrypted = getpass (prompt);
  if (crypt)
    {
      encrypted = crypt (unencrypted, password);
      memset (unencrypted, 0, strlen (unencrypted)); /* Paranoia may destroya.  */
    }
  else  
    encrypted = unencrypted;

  if (!strcmp (encrypted, password))
    return 1;

  fprintf (stderr, "%s: Access denied for `%s'\n",
	   program_invocation_short_name, name);
  return 0;
}
