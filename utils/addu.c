/* Add a user to some process(es)

   Copyright (C) 1997 Free Software Foundation, Inc.

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

/* XXX NOTE: This program is a real hack job, with large chunks of code cut
   out of ps and login; the code should be shared in some fashion instead. */

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <argp.h>
#include <pwd.h>
#include <grp.h>
#include <idvec.h>
#include <ps.h>
#include <error.h>
#include <version.h>

#include "psout.h"

const char *argp_program_version = STANDARD_HURD_VERSION (ps);

#define OA OPTION_ARG_OPTIONAL

static const struct argp_option options[] =
{
  {0,0,0,0, "Which ids to add:"},
  {"user",	'u', "USER",  0, "Add USER to the effective uids"},
  {"avail-user",'U', "USER",  0, "Add USER to the available uids"},
  {"group",     'g', "GROUP", 0, "Add GROUP to the effective groups"},
  {"avail-group",'G',"GROUP", 0, "Add GROUP to the available groups"},

  {0,0,0,0, "Whic processes to add to:"},
  {"login",      'L',     "LID", OA, "Processes from the login"
                                      " collection LID (which defaults that of"
                                      " the current process)"},
  {"lid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {"pid",        'p',     "PID",  0,  "The process PID"},
  {"pgrp",       'P',     "PGRP", 0,  "Processes in process group PGRP"},
  {"session",    'S',     "SID",  OA, "Processes from the session SID"
                                      " (which defaults to the sid of the"
                                      " current process)"},
  {"sid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {0, 0}
};

char *args_doc = "USER...";
char *doc =
"Add USER to the userids of the selected processes"
"\vBy default, all processes in the current login collection are selected";

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_ADD_FN isn't NULL, then call DEFAULT_ADD_FN instead. */
static void
_parse_strlist (char *arg,
		void (*add_fn)(const char *str), void (*default_add_fn)(),
		const char *type_name)
{
  if (arg)
    while (isspace(*arg))
      arg++;

  if (arg == NULL || *arg == '\0')
    if (default_add_fn)
      (*default_add_fn)();
    else
      error(7, 0, "Empty %s list", type_name);
  else
    {
      char *end = arg;

      void mark_end()
	{
	  *end++ = '\0';
	  while (isspace(*end))
	    end++;
	}
      void parse_element()
	{
	  if (*arg == '\0')
	    error(7, 0, "Empty element in %s list", type_name);
	  (*add_fn)(arg);
	  arg = end;
	}

      while (*end != '\0')
	switch (*end)
	  {
	  case ' ': case '\t':
	    mark_end();
	    if (*end == ',')
	      mark_end();
	    parse_element();
	    break;
	  case ',':
	    mark_end();
	    parse_element();
	    break;
	  default:
	    end++;
	  }

      parse_element();
    }
}

/* For each numeric string in the comma-separated list in ARG, call ADD_FN;
   if ARG is empty and DEFAULT_FN isn't NULL, then call DEF_FN to get a number,
   and call ADD_FN on that, otherwise signal an error.  If any member of the
   list isn't a number, and LOOKUP_FN isn't NULL, then it is called to return
   an integer for the string.  LOOKUP_FN should signal an error itself it
   there's some problem parsing the string.  */
static void
parse_numlist (char *arg,
	       void (*add_fn)(unsigned num),
	       int (*default_fn)(),
	       int (*lookup_fn)(const char *str),
	       const char *type_name)
{
  void default_num_add() { (*add_fn)((*default_fn)()); }
  void add_num_str(const char *str)
    {
      const char *p;
      for (p = str; *p != '\0'; p++)
	if (!isdigit(*p))
	  {
	    if (lookup_fn)
	      (*add_fn)((*lookup_fn)(str));
	    else
	      error (7, 0, "%s: Invalid %s", p, type_name);
	    return;
	  }
      (*add_fn)(atoi(str));
    }
  _parse_strlist(arg, add_num_str, default_fn ? default_num_add : 0,
		 type_name);
}

static process_t proc_server;

/* Returns our session id.  */
static pid_t
current_sid()
{
  pid_t sid;
  error_t err = proc_getsid(proc_server, getpid(), &sid);
  if (err)
    error(2, err, "Couldn't get current session id");
  return sid;
}

/* Returns our login collection id.  */
static pid_t
current_lid()
{
  pid_t lid;
  error_t err = proc_getloginid(proc_server, getpid(), &lid);
  if (err)
    error(2, err, "Couldn't get current login collection") ;
  return lid;
}

/* Returns the UID for the user called NAME.  */
static int
lookup_user(const char *name)
{
  struct passwd *pw = getpwnam(name);
  if (pw == NULL)
    error(2, 0, "%s: Unknown user", name);
  return pw->pw_uid;
}

void 
main (int argc, char *argv[])
{
  error_t err;
  struct proc_stat_list *procset;
  struct ps_context *context;
  struct idvec *eff_uids = make_idvec (); /* The UIDs of the new shell.  */
  struct idvec *eff_gids = make_idvec (); /* The EFF_GIDs.  */
  struct idvec *avail_uids = make_idvec (); /* The aux UIDs of the new shell.  */
  struct idvec *avail_gids = make_idvec (); /* The aux EFF_GIDs.  */
  struct idvec *parent_uids = make_idvec (); /* Parent uids, -SETUID. */
  struct idvec *parent_gids = make_idvec (); /* Parent gids, -SETGID. */
  auth_t auth, parent_auth = getauth ();

  /* Add a specific process to be printed out.  */
  void add_pid (unsigned pid)
    {
      struct proc_stat *ps;

      err = proc_stat_list_add_pid (procset, pid, &ps);
      if (err)
	error (2, err, "%d: Cannot add process", pid);

      /* See if this process actually exists.  */
      proc_stat_set_flags (ps, PSTAT_PROC_INFO);
      if (! proc_stat_has (ps, PSTAT_PROC_INFO))
	error (99, 0, "%d: Unknown process", pid);
    }
  /* Print out all process from the given session.  */
  void add_sid(unsigned sid)
    {
      err = proc_stat_list_add_session (procset, sid, 0, 0);
      if (err)
	error(2, err, "%u: Cannot add session", sid);
    }
  /* Print out all process from the given login collection.  */
  void add_lid(unsigned lid)
    {
      error_t err = proc_stat_list_add_login_coll (procset, lid, 0, 0);
      if (err)
	error(2, err, "%u: Cannot add login collection", lid);
    }
  /* Print out all process from the given process group.  */
  void add_pgrp(unsigned pgrp)
    {
      error_t err = proc_stat_list_add_pgrp (procset, pgrp, 0, 0);
      if (err)
	error(2, err, "%u: Cannot add process group", pgrp);
    }

  /* Make sure that the parent_[ug]ids are filled in.  To make them useful
     for su'ing, each is the avail ids with all effective ids but the first
     appended; this gets rid of the effect of login being suid, and is useful
     as the new process's avail id list (e.g., the real id is right).   */
  void need_parent_ids ()
    {
      if (parent_uids->num == 0 && parent_gids->num == 0)
	{
	  struct idvec *p_eff_uids = make_idvec ();
	  struct idvec *p_eff_gids = make_idvec ();
	  if (!p_eff_uids || !p_eff_gids)
	    err = ENOMEM;
	  if (! err)
	    err = idvec_merge_auth (p_eff_uids, parent_uids,
				    p_eff_gids, parent_gids,
				    parent_auth);
	  if (! err)
	    {
	      idvec_delete (p_eff_uids, 0); /* Counteract setuid. */
	      idvec_delete (p_eff_gids, 0);
	      err = idvec_merge (parent_uids, p_eff_uids);
	      if (! err)
		err = idvec_merge (parent_gids, p_eff_gids);
	    }
	  if (err)
	    error (39, err, "Cannot get uids");
	}
    }

  /* Returns true if the *caller* of this login program has UID.  */
  int parent_has_uid (uid_t uid)
    {
      need_parent_ids ();
      return idvec_contains (parent_uids, uid);
    }
  /* Returns true if the *caller* of this login program has GID.  */
  int parent_has_gid (gid_t gid)
    {
      need_parent_ids ();
      return idvec_contains (parent_gids, gid);
    }
  /* Returns the number of parent uids.  */
  int count_parent_uids ()
    {
      need_parent_ids ();
      return parent_uids->num;
    }
  /* Returns the number of parent gids.  */
  int count_parent_gids ()
    {
      need_parent_ids ();
      return parent_gids->num;
    }

  /* Make sure the user should be allowed to do this.  */
  void verify_passwd (const char *name, const char *password,
		      uid_t id, int is_group)
    {
      extern char *crypt (const char *string, const char salt[2]);
#ifndef HAVE_CRYPT
#pragma weak crypt
#endif
      char *prompt, *unencrypted, *encrypted;

      if (!password || !*password
	  || idvec_contains (is_group ? eff_gids : eff_uids, id)
	  || idvec_contains (is_group ? avail_gids : avail_uids, id)
	  || parent_has_uid (0)
	  || (is_group ? parent_has_uid (id) : parent_has_gid (id)))
	return;			/* Already got this one.  */

      if (name)
	asprintf (&prompt, "Password for %s%s:",
		  is_group ? "group " : "", name);
      else
	prompt = "Password:";

      unencrypted = getpass (prompt);
      if (crypt)
	{
	  encrypted = crypt (unencrypted, password);
	  /* Paranoia may destroya.  */
	  memset (unencrypted, 0, strlen (unencrypted));

	  if (! encrypted)
	    /* Something went wrong.  */
	    error (51, errno, "Password encryption failed");
	}
      else
	encrypted = unencrypted;

      if (name)
	free (prompt);

      if (strcmp (encrypted, password) != 0)
	error (50, 0, "Incorrect password");
    }

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'p':
	  parse_numlist(arg, add_pid, NULL, NULL, "process id");
	  break;
	case 'S':
	  parse_numlist(arg, add_sid, current_sid, NULL, "session id");
	  break;
	case 'L':
	  parse_numlist(arg, add_lid, current_lid, NULL, "login collection");
	  break;
	case 'P':
	  parse_numlist(arg, add_pgrp, NULL, NULL, "process group");
	  break;

	case 'u':
	case 'U':
	case ARGP_KEY_ARG:
	  {
	    struct passwd *pw =
	      isdigit (*arg) ? getpwuid (atoi (arg)) : getpwnam (arg);
	    /* True if this is the user arg and there were no user options. */
	    int only_user =
	      (key == ARGP_KEY_ARG
	       && eff_uids->num == 0 && avail_uids->num <= count_parent_uids ()
	       && eff_gids->num == 0 && avail_gids->num <= count_parent_gids ());

	    if (! pw)
	      argp_failure (state, 10, 0, "%s: Unknown user", arg);

	    /* If it's not nobody, make sure we're authorized.  */
	    verify_passwd (only_user ? 0 : pw->pw_name, pw->pw_passwd,
			   pw->pw_uid, 0);

	    if (key == 'U')
	      /* Add available ids instead of effective ones.  */
	      idvec_add_new (avail_uids, pw->pw_uid);
	    else
	      idvec_add_new (eff_uids, pw->pw_uid);
	  }
	  break;

	case 'g':
	case 'G':
	  {
	    struct group *gr =
	      isdigit (*arg) ? getgrgid (atoi (arg)) : getgrnam (arg);
	    if (! gr)
	      argp_failure (state, 11, 0, "%s: Unknown group", arg);
	    verify_passwd (gr->gr_name, gr->gr_passwd, gr->gr_gid, 1);
	    idvec_add_new (key == 'g' ? eff_gids : avail_gids, gr->gr_gid);
	  }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  error_t frob_proc (struct proc_stat *ps)
    {
      if (! (ps->flags & PSTAT_MSGPORT))
	error (0, 0, "%d: Cannot get message port", ps->pid);
      else
	{
	  error_t err = msg_add_auth (ps->msgport, auth);
	  if (err)
	    error (0, err, "%d: Cannot add authentication", ps->pid);
	}
      return 0;
    }

  struct argp argp = { options, parse_opt, args_doc, doc};

  proc_server = getproc();

  err = ps_context_create (proc_server, &context);
  if (err)
    error(1, err, "ps_context_create");

  err = proc_stat_list_create(context, &procset);
  if (err)
    error(1, err, "proc_stat_list_create");

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  err =
    auth_makeauth (getauth (), 0, MACH_MSG_TYPE_COPY_SEND, 0,
		   eff_uids->ids, eff_uids->num,
		   avail_uids->ids, avail_uids->num,
		   eff_gids->ids, eff_gids->num,
		   avail_gids->ids, avail_gids->num,
		   &auth);
  if (err)
    error (3, err, "Authentication failure");

  proc_stat_list_set_flags (procset, PSTAT_TASK | PSTAT_MSGPORT);
  proc_stat_list_for_each (procset, frob_proc);

  exit (0);
}
