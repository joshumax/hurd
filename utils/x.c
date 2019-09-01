/* Hurdish su

   Copyright (C) 1996 Free Software Foundation, Inc.

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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <ctype.h>
#include <utmp.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <time.h>
#ifdef HAVE_LIBCRYPT
#include <crypt.h>
#else
#warning "No crypt on this system!  Using plain-text passwords."
#define crypt(password, encrypted) password
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/fcntl.h>

#include <argp.h>
#include <argz.h>
#include <envz.h>
#include <idvec.h>
#include <error.h>
#include <timefmt.h>
#include <hurd/lookup.h>

static struct argp_option options[] =
{
  {"add",	'a', "USER",   0, "Add following ids"},
  {"remove",	'r', "USER",   0, "Remove following ids"},
  {"user",	'u', "USER",  0, "Add USER to the effective uids"},
  {"avail-user",'U', "USER",  0, "Add USER to the available uids"},
  {"group",     'g', "GROUP", 0, "Add GROUP to the effective groups"},
  {"avail-group",'G',"GROUP", 0, "Add GROUP to the available groups"},
  {0, 0}
};
static char *args_doc = "[USER...]";
static char *doc = "Modify authentication of existing processes"
 "\vA USER specified as an argument adds (or removes) that user's groups as"
 " well.  When removing groups implied by such an argument, the groups to"
 " which uids remaining in the process after any we remove are ignored."
"\nUids and groups specified with options are used as-is.";

/* Full set of desired authorization.  XXX msg_del_auth doesn't allow such
   fine control.  */
struct auth
{
  struct idvec euids, egids;	/* Effective ids.  */
  struct idvec auids, agids;	/* Available ids.  */
};

/* Ids of our parent process, with the effect of this program being su'd
   removed.  */
static struct idvec parent_uids = {0}, parent_gids = {0};

/* Make sure that the parent_[ug]ids are filled in.  To make them useful for
   su'ing, each is the avail ids with all effective ids but the first
   appended; this gets rid of the effect of login being suid, and is useful
   as the new process's avail id list (e.g., the real id is right).  */
static void
need_parent_ids ()
{
  if (parent_uids.num == 0 && parent_gids.num == 0)
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
	error (39, err, "Can't get uids");
    }
}

/* Returns true if the *caller* of this login program has UID.  */
static int
parent_has_uid (uid_t uid)
{
  need_parent_ids ();
  return idvec_contains (parent_uids, uid);
}
/* Returns true if the *caller* of this login program has GID.  */
static int
parent_has_gid (gid_t gid)
{
  need_parent_ids ();
  return idvec_contains (parent_gids, gid);
}
/* Returns the number of parent uids.  */
static int
count_parent_uids ()
{
  need_parent_ids ();
  return parent_uids.num;
}
/* Returns the number of parent gids.  */
static int
count_parent_gids ()
{
  need_parent_ids ();
  return parent_gids.num;
}

/* Make sure the user should be allowed to do this.  */
void verify_passwd (const char *name, const char *password,
		    uid_t id, int is_group, structh auth *auth)
{
  char *prompt, *unencrypted, *encrypted;

  if (!password || !*password
      || idvec_contains (is_group ? auth->egids : auth->euids, id)
      || idvec_contains (is_group ? auth->agids : auth->auids, id)
      || (no_passwd
	  && (parent_has_uid (0)
	      || (is_group ? parent_has_uid (id) : parent_has_gid (id)))))
    return;			/* Already got this one.  */

  if (name)
    asprintf (&prompt, "Password for %s%s:",
	      is_group ? "group " : "", name);
  else
    prompt = "Password:";

  unencrypted = getpass (prompt);
  encrypted = crypt (unencrypted, password);
  /* Paranoia may destroya.  */
  memset (unencrypted, 0, strlen (unencrypted));

  if (name)
    free (prompt);

  if (strcmp (encrypted, password) != 0)
    error (50, 0, "Incorrect password", 0);
}

void
main(int argc, char *argv[])
{
  error_t err = 0;
  struct auth add, remove;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_NO_ARGS:
	  arg = "0";		/* root */
	  /* fall through.  */

	case 'u':
	case 'U':
	  {
	    struct passwd *pw =
	      isdigit (*user) ? getpwuid (atoi (user)) : getpwnam (user);
	    /* True if this is the user arg and there were no user options. */

	    if (! pw)
	      error (10, 0, "%s: Unknown user", user);

	    verify_passwd (state->argv[state->next] ? pw->pw_name : 0,
			   pw->pw_passwd, pw->pw_uid, 0, &auth);

	    if (key == 'u')
	      idvec_add_new (&auth.euids, pw->pw_uid);
	    else if (key == 'U')
	      /* Add available ids instead of effective ones.  */
	      idvec_add_new (&auth.auids, pw->pw_uid);
	    else
	      /* A plain argument.  Add both the specified user and any
		 associated groups.  */
	      {
		/* Effective */
		idvec_add_new (&auth.euids, 0, pw->pw_uid);
		idvec_add_new (&auth.egids, 0, pw->pw_gid);
	      }
	  }
	  break;

	case 'g':
	case 'G':
	  {
	    struct group *gr =
	      isdigit (*arg) ? getgrgid (atoi (arg)) : getgrnam (arg);
	    if (! gr)
	      error (11, 0, "%s: Unknown group", arg);
	    verify_passwd (gr->gr_name, gr->gr_passwd, gr->gr_gid, 1, &auth);
	    idvec_add_new (key == 'g' ? &auth.egids : &auth.agids, gr->gr_gid);
	  }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  memset (add, 0, sizeof add);
  memset (remove, 0, sizeof remove);


  err =
    auth_makeauth (getauth (), 0, MACH_MSG_TYPE_COPY_SEND, 0,
		   &auth.euids->ids, &auth.euids->num,
		   &auth.auids->ids, &auth.auids->num,
		   &auth.egids->ids, &auth.egids->num,
		   &auth.agids->ids, &auth.agids->num,
		   &auth);
  if (err)
    error (3, err, "Authentication failure", 0);


  exit(0);
}
