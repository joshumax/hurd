/* Verify user passwords

   Copyright (C) 1996, 1997, 1998, 1999, 2002, 2008
     Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include "assert-backtrace.h"
#include <idvec.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#ifdef HAVE_LIBCRYPT
#include <crypt.h>
#else
#warning "No crypt on this system!  Using plain-text passwords."
#define crypt(password, encrypted) password
#endif

#define SHADOW_PASSWORD_STRING	"x" /* pw_passwd contents for shadow passwd */

static error_t verify_id (); /* FWD */

/* Get a password from the user, returning it in malloced storage.  */
static char *
get_passwd (const char *prompt,
	    uid_t id, int is_group,
	    void *pwd_or_grp, void *hook)
{
  char *st = getpass (prompt);
  if (st)
    st = strdup (st);
  return st;
}

/* Verify PASSWORD using /etc/passwd (and maybe /etc/shadow).  */
static error_t
verify_passwd (const char *password,
	       uid_t id, int is_group,
	       void *pwd_or_grp, void *hook)
{
  const char *encrypted;
  int wheel_uid = (intptr_t)hook;
  const char *sys_encrypted;

  if (! pwd_or_grp)
    /* No password db entry for ID; if ID is root, the system is probably
       really fucked up, so grant it (heh).  */
    return (id == 0 ? 0 : EACCES);

  /* The encrypted password in the passwd db.  */
  sys_encrypted =
    (is_group
     ? ((struct passwd *)pwd_or_grp)->pw_passwd
     : ((struct group *)pwd_or_grp)->gr_passwd);

  if (sys_encrypted[0] == '\0')
    return 0;			/* No password.  */

  /* Encrypt the password entered by the user (SYS_ENCRYPTED is the salt). */
  encrypted = crypt (password, sys_encrypted);

  if (! encrypted)
    /* Crypt failed.  */
    return errno;

  /* See whether the user's password matches the system one.  */
  if (strcmp (encrypted, sys_encrypted) == 0)
    /* Password check succeeded.  */
    return 0;
  else if (id == 0 && !is_group && wheel_uid)
    /* Special hack: a user attempting to gain root access can use
       their own password (instead of root's) if they're in group 0. */
    {
      struct passwd _pw, *pw;
      char lookup_buf[1024];
      char sp_lookup_buf[1024];

      const char *check_shadow (struct passwd *pw)
	{
	  if (strcmp (pw->pw_passwd, SHADOW_PASSWORD_STRING) == 0)
	    {
	      /* When encrypted password is "x", try shadow passwords. */
	      struct spwd _sp, *sp;
	      if (getspnam_r (pw->pw_name, &_sp, sp_lookup_buf,
			      sizeof sp_lookup_buf, &sp) == 0)
		return sp->sp_pwdp;
	    }
	  return pw->pw_passwd;
	}

      if (getpwuid_r (wheel_uid, &_pw, lookup_buf, sizeof lookup_buf, &pw)
	  || ! pw)
	return errno ?: EINVAL;

      sys_encrypted = check_shadow (pw);

      encrypted = crypt (password, sys_encrypted);
      if (! encrypted)
	/* Crypt failed.  */
	return errno;

      if (strcmp (encrypted, sys_encrypted) == 0)
	/* *this* password is correct!  */
	return 0;
    }

  return EACCES;
}

/* Make sure the user has the right to the ids in UIDS and GIDS, given that
   we know he already has HAVE_UIDS and HAVE_GIDS, asking for passwords (with
   GETPASS_FN) where necessary; any of the arguments may be 0, which is
   treated the same as if they were empty.  0 is returned if access should be
   allowed, otherwise EINVAL if an incorrect password was entered, or an
   error relating to resource failure.  Any uid/gid < 0 will be guaranteed to
   fail regardless of what the user types.  GETPASS_FN should ask for a
   password from the user, and return it in malloced storage; it defaults to
   using the standard libc function getpass.  If VERIFY_FN is 0, then the
   users password will be encrypted with crypt and compared with the
   password/group entry's encrypted password, otherwise, VERIFY_FN will be
   called to check the entered password's validity; it should return 0 if the
   given password is correct, or an error code.  The common arguments to
   GETPASS_FN and VERIFY_FN are: ID, the user/group id; IS_GROUP, true if its
   a group, or false if a user; PWD_OR_GRP, a pointer to either the passwd or
   group entry for ID, and HOOK, containing the appropriate hook passed into
   idvec_verify.  */
error_t
idvec_verify (const struct idvec *uids, const struct idvec *gids,
	      const struct idvec *have_uids, const struct idvec *have_gids,
	      char *(*getpass_fn) (const char *prompt,
				   uid_t id, int is_group,
				   void *pwd_or_grp, void *hook),
	      void *getpass_hook,
	      error_t (*verify_fn) (const char *password,
				    uid_t id, int is_group,
				    void *pwd_or_grp, void *hook),
	      void *verify_hook)
{
  if (have_uids && idvec_contains (have_uids, 0))
    /* Root can do anything.  */
    return 0;
  else
    {
      unsigned int i;
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

      if (! verify_fn)
	{
	  verify_fn = verify_passwd;
	  verify_hook = (void *)(intptr_t)wheel_uid;
	}

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
	err = verify_id (0, 0, multiple,
			 getpass_fn, getpass_hook, verify_fn, verify_hook);
      else
	{
	  if (uids)
	    /* Check uids */
	    for (i = 0; i < uids->num && !err; i++)
	      {
		uid_t uid = uids->ids[i];
		if (!have_uids || !idvec_contains (have_uids, uid))
		  err = verify_id (uid, 0, multiple,
				   getpass_fn, getpass_hook, verify_fn, verify_hook);
	      }

	  if (gids)
	    /* Check gids */
	    for (i = 0; i < gids->num && !err; i++)
	      {
		gid_t gid = gids->ids[i];
		if ((!have_gids || !idvec_contains (have_gids, gid))
		    && !idvec_contains (&implied_gids, gid))
		  err = verify_id (gid, 1, multiple,
				   getpass_fn, getpass_hook, verify_fn, verify_hook);
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
verify_id (uid_t id, int is_group, int multiple,
	   char *(*getpass_fn) (const char *prompt,
				uid_t id, int is_group,
				void *pwd_or_grp, void *hook),
	   void *getpass_hook,
	   error_t (*verify_fn) (const char *password,
				 uid_t id, int is_group,
				 void *pwd_or_grp, void *hook),
	   void *verify_hook)
{
  int err;
  void *pwd_or_grp = 0;
  char *name = 0;
  char *prompt = 0, *password;
  char id_lookup_buf[1024];
  char sp_lookup_buf[1024];

  /* VERIFY_FN should have been defaulted in idvec_verify if necessary.  */
  assert_backtrace (verify_fn);

  if (id != (uid_t) -1)
    do
      {
	if (is_group)
	  {
	    struct group _gr, *gr;
	    if (getgrgid_r (id, &_gr, id_lookup_buf, sizeof id_lookup_buf, &gr)
		== 0 && gr)
	      {
		if (!gr->gr_passwd || !*gr->gr_passwd)
		  return (*verify_fn) ("", id, 1, gr, verify_hook);
		name = gr->gr_name;
		pwd_or_grp = gr;
	      }
	  }
	else
	  {
	    struct passwd _pw, *pw;
	    if (getpwuid_r (id, &_pw, id_lookup_buf, sizeof id_lookup_buf, &pw)
		== 0 && pw)
	      {
		if (strcmp (pw->pw_passwd, SHADOW_PASSWORD_STRING) == 0)
		  {
		    /* When encrypted password is "x", check shadow
		       passwords to see if there is an empty password. */
		    struct spwd _sp, *sp;
		    if (getspnam_r (pw->pw_name, &_sp, sp_lookup_buf,
				    sizeof sp_lookup_buf, &sp) == 0)
		      /* The storage for the password string is in
			 SP_LOOKUP_BUF, a local variable in this function.
			 We Know that the only use of PW->pw_passwd will be
			 in the VERIFY_FN call in this function, and that
			 the pointer will not be stored past the call.  */
		      pw->pw_passwd = sp->sp_pwdp;
		  }

		if (pw->pw_passwd[0] == '\0')
		  return (*verify_fn) ("", id, 0, pw, verify_hook);
		name = pw->pw_name;
		pwd_or_grp = pw;
	      }
	  }
	if (! name)
	  {
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
	      /* No password entry for root.  */
	      name = "root";
	  }
      }
    while (! name);

  if (! getpass_fn)
    /* Default GETPASS_FN to using getpass.  */
    getpass_fn = get_passwd;

  if (multiple)
    {
      if (name)
	asprintf (&prompt, "Password for %s%s:",
		  is_group ? "group " : "", name);
      else
	asprintf (&prompt, "Password for %s %d:",
		  is_group ? "group" : "user", id);
    }

  /* Prompt the user for the password.  */
  if (prompt)
    {
      password =
	(*getpass_fn) (prompt, id, is_group, pwd_or_grp, getpass_hook);
      free (prompt);
    }
  else
    password =
      (*getpass_fn) ("Password:", id, is_group, pwd_or_grp, getpass_hook);

  /* Check the user's answer.  */
  if (password)
    {
      err = (*verify_fn) (password, id, is_group, pwd_or_grp, verify_hook);

      /* Paranoia may destroya.  */
      memset (password, 0, strlen (password));

      free (password);
    }
  else
    err = EACCES;

  return err;
}
