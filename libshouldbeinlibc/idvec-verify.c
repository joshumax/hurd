/* Verify user passwords

   Copyright (C) 1996, 1997 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <idvec.h>
#include <grp.h>
#include <pwd.h>

extern char *crypt (const char *string, const char salt[2]);
#pragma weak crypt

static error_t verify_id (); /* FWD */

/* Make sure the user has the right to the ids in UIDS and GIDS, given that
   we know he already has HAVE_UIDS and HAVE_GIDS, asking for passwords (with
   GETPASS, which defaults to the standard libc function getpass) where
   necessary; any of the arguments may be 0, which is treated the same as if
   they were empty.  0 is returned if access should be allowed, otherwise
   EINVAL if an incorrect password was entered, or an error relating to
   resource failure.  Any uid/gid < 0 will be guaranteed to fail regardless
   of what the user types.  */
error_t
idvec_verify (const struct idvec *uids, const struct idvec *gids,
	      const struct idvec *have_uids, const struct idvec *have_gids,
	      char *(*getpass_fn)(const char *prompt))
{
  if (have_uids && idvec_contains (have_uids, 0))
    /* Root can do anything.  */
    return 0;
  else
    {
      int i;
      int multiple = 0;		/* Asking for multiple ids? */
      error_t  err = 0;		/* Our return status.  */
      struct idvec implied_gids = IDVEC_INIT; /* Gids implied by uids.  */
      /* If we already are in group 0 (`wheel'), this user's password can be
	 used to get root privileges instead of root's.  */
      int wheel_uid =
	((have_uids && have_gids
	  && (idvec_contains (have_gids, 0) && have_uids->num > 0))
	 ? have_uids->ids[0]
	 : 0);

      /* See if there are multiple ids in contention, in which case we should
	 name each user/group as we ask for its password.  */
      if (uids && gids)
	{
	  int num_non_implied_gids = 0;

	  /* Calculate which groups we need not ask about because they are
	     implied by the uids which we (will) have verified.  Note that we
	     ignore any errors; at most, it means we will ask for too many
	     passwords.  */
	  idvec_merge_implied_gids (&implied_gids, uids);

	  for (i = 0; i < gids->num; i++)
	    if (! idvec_contains (&implied_gids, gids->ids[i]))
	      num_non_implied_gids++;

	  multiple = (uids->num + num_non_implied_gids) > 1;
	}
      else if (uids)
	multiple = uids->num > 1;
      else if (gids)
	multiple = gids->num > 1;

      if (uids && idvec_contains (uids, 0))
	/* root is being asked for, which, once granted will provide access for
	   all the others.  */
	err = verify_id (0, 0, multiple, wheel_uid, getpass_fn);
      else
	{
	  if (uids)
	    /* Check uids */
	    for (i = 0; i < uids->num && !err; i++)
	      {
		uid_t uid = uids->ids[i];
		if (!have_uids || !idvec_contains (have_uids, uid))
		  err = verify_id (uid, 0, multiple, wheel_uid, getpass_fn);
	      }

	  if (gids)
	    /* Check gids */
	    for (i = 0; i < gids->num && !err; i++)
	      {
		gid_t gid = gids->ids[i];
		if ((!have_gids || !idvec_contains (have_gids, gid))
		    && !idvec_contains (&implied_gids, gid))
		  err = verify_id (gid, 1, multiple, wheel_uid, getpass_fn);
	      }
	}

      idvec_fini (&implied_gids);

      return err;
    }
}

/* Verify that the user should be allowed to assume the indentity of the
   user/group ID (depending on whether IS_GROUP is false/true).  If MULTIPLE
   is true, then this is one of multiple ids being verified, so  */
static error_t
verify_id (uid_t id, int is_group, int multiple, int wheel_uid,
	   char *(*getpass_fn)(const char *prompt))
{
  int err;
  char *name = 0, *password = 0;
  char *prompt = 0, *unencrypted, *encrypted;
  char id_lookup_buf[1024];

  if (id >= 0)
    do
      {
	if (is_group)
	  {
	    struct group _gr, *gr;
	    if (getgrgid_r (id, &_gr, id_lookup_buf, sizeof id_lookup_buf, &gr)
		== 0)
	      {
		password = gr->gr_passwd;
		name = gr->gr_name;
	      }
	  }
	else
	  {
	    struct passwd _pw, *pw;
	    if (getpwuid_r (id, &_pw, id_lookup_buf, sizeof id_lookup_buf, &pw)
		== 0)
	      {
		password = pw->pw_passwd;
		name = pw->pw_name;
	      }
	  }
	if (! name)
	  /* [ug]id lookup failed!  */
	  if (id != 0 || is_group)
	    /* If ID != 0, then it's probably just an unknown id, so ask for
	       the root password instead -- root should be able to do
	       anything.  */
	    {
	      id = 0;		/* Root */
	      is_group = 0;	/* uid */
	      multiple = 1;	/* Explicitly ask for root's password.  */
	    }
	  else
	    /* If ID == 0 && !IS_GROUP, then this means that the system is
	       really fucked up (there's no root password entry), so instead
	       just don't ask for a password at all (if an intruder has
	       succeeded in preventing the lookup somehow, he probably could
	       have just provided his own result anyway).  */
	    name = "uh-oh";
      }
    while (! name);

  if (!password || !*password)
    /* No password!  */
    return 0;

  if (! getpass_fn)
    getpass_fn = getpass;

  if (multiple)
    if (name)
      asprintf (&prompt, "Password for %s%s:",
		is_group ? "group " : "", name);
    else
      asprintf (&prompt, "Password for %s %d:",
		is_group ? "group" : "user", id);
  if (prompt)
    {
      unencrypted = (*getpass_fn) (prompt);
      free (prompt);
    }
  else 
    unencrypted = (*getpass_fn) ("Password:");

  if (crypt)
    {
      encrypted = crypt (unencrypted, password);
      if (! encrypted)
	/* Something went wrong.  */
	return errno;
    }
  else
    encrypted = unencrypted;

  err = EINVAL;			/* Assume an invalid password.  */

  if (encrypted && strcmp (encrypted, password) == 0)
    err = 0;			/* Password correct!  */
  else if (id == 0 && !is_group && wheel_uid)
    /* Special hack: a user attempting to gain root access can use
       their own password (instead of root's) if they're in group 0. */
    {
      struct passwd *pw = getpwuid (wheel_uid);
      encrypted = crypt (unencrypted, pw->pw_passwd);
      if (pw && encrypted && strcmp (encrypted, pw->pw_passwd) == 0)
	err = 0;		/* *this* password is correct!  */
    }

  /* Paranoia may destroya.  */
  memset (unencrypted, 0, strlen (unencrypted));

  return err;
}
