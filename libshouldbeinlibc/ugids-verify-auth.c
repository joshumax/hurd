/* Verify user/group passwords and authenticate accordingly

   Copyright (C) 1997, 1998 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdlib.h>
#include <hurd.h>

#include <hurd/paths.h>
#include <hurd/password.h>

#include "idvec.h"
#include "ugids.h"

/* Accumulated information from authentication various passwords.  */
struct svma_state
{
  /* The password server.  */
  file_t server;

  /* An auth port for each password that was verify by the server.  */
  auth_t *auths;
  size_t num_auths;
};

/* Append the auth ports in AUTHS, of length NUM_AUTHS, to the auth port
   vector in SS, returning 0 if successful, or an error.  */
static error_t
svma_state_add_auths (struct svma_state *ss,
		      const auth_t *auths, size_t num_auths)
{
  auth_t *new = realloc (ss->auths,
			 (ss->num_auths + num_auths) * sizeof (auth_t));
  if (new)
    {
      ss->auths = new;
      while (num_auths--)
	ss->auths[ss->num_auths++] = *auths++;
      return 0;
    }
  else
    return ENOMEM;
}

/* Get authentication from PASSWORD using the hurd password server.  */
static error_t
server_verify_make_auth (const char *password,
			 uid_t id, int is_group,
			 void *pwd_or_grp, void *hook)
{
  auth_t auth;
  struct svma_state *svma_state = hook;
  /* Mig routines don't use 'const' for passwd.  */
  error_t (*check) (io_t server, uid_t id, const char *passwd, auth_t *auth) =
    is_group ? password_check_group : password_check_user;
  error_t err = (*check) (svma_state->server, id, password, &auth);

  if (! err)
    /* PASSWORD checked out ok; the corresponding authentication is in AUTH. */
    {
      err = svma_state_add_auths (svma_state, &auth, 1);
      if (err)
	mach_port_deallocate (mach_task_self (), auth);
    }

  return err;
}

/* Verify that we have the right to the ids in UGIDS, given that we already
   possess those in HAVE_UIDS and HAVE_GIDS (asking for passwords where
   necessary), and return corresponding authentication in AUTH; the auth
   ports in FROM, of length NUM_FROM, are used to supplement the auth port of
   the current process if necessary.  0 is returned if access should be
   allowed, otherwise EINVAL if an incorrect password was entered, or an
   error relating to resource failure.  GETPASS_FN and GETPASS_HOOK are as
   for the idvec_verify function in <idvec.h>.  */
error_t
ugids_verify_make_auth (const struct ugids *ugids,
			const struct idvec *have_uids,
			const struct idvec *have_gids,
			char *(*getpass_fn) (const char *prompt,
					     uid_t id, int is_group,
					     void *pwd_or_grp, void *hook),
			void *getpass_hook,
			const auth_t *from, size_t num_from,
			auth_t *auth)
{
  error_t err;
  /* By default, get authentication from the password server.  */
  struct svma_state svma_state;
  error_t (*verify_fn) (const char *password,
			uid_t id, int is_group,
			void *pwd_or_grp, void *hook)
    = server_verify_make_auth;
  void *verify_hook = &svma_state;

  /* Try to open the hurd password server.  */
  svma_state.server = file_name_lookup (_SERVERS_PASSWORD, 0, 0);

  if (svma_state.server == MACH_PORT_NULL)
    /* Can't open the password server, try to use our own authority in
       the traditional unix manner.  */
    {
      verify_fn = 0;
      verify_hook = 0;
    }
  else
    {
      /* Must initialize list to empty so svma_state_add_auths works.  */
      svma_state.auths = NULL;
      svma_state.num_auths = 0;
    }

  /* Check passwords.  */
  err = ugids_verify (ugids, have_uids, have_gids,
		      getpass_fn, getpass_hook, verify_fn, verify_hook);

  if (! err)
    {
      /* The user apparently has access to all the ids, try to grant the
	 corresponding authentication.  */
      if (verify_fn)
	/* Merge the authentication we got from the password server into our
	   result.  */
	{
	  if (num_from > 0)
	    /* Use FROM as well as the passwords to get authentication.  */
	    err = svma_state_add_auths (&svma_state, from, num_from);

	  if (! err)
	    {
	      auth_t cur_auth = getauth ();

	      err =
		auth_makeauth (cur_auth,
			       svma_state.auths, MACH_MSG_TYPE_COPY_SEND,
			       svma_state.num_auths,
			       ugids->eff_uids.ids, ugids->eff_uids.num,
			       ugids->avail_uids.ids, ugids->avail_uids.num,
			       ugids->eff_gids.ids, ugids->eff_gids.num,
			       ugids->avail_gids.ids, ugids->avail_gids.num,
			       auth);
	      mach_port_deallocate (mach_task_self (), cur_auth);

	      /* Avoid deallocating FROM when we clean up SVMA_STATE.  */
	      svma_state.num_auths -= num_from;
	    }
	}
      else
	/* Try to authenticate the old fashioned way...  */
	err = ugids_make_auth (ugids, from, num_from, auth);
    }

  if (verify_fn)
    /* Clean up any left over state.  */
    {
      unsigned int i;

      /* Get rid of auth ports.  */
      for (i = 0; i < svma_state.num_auths; i++)
	mach_port_deallocate (mach_task_self (), svma_state.auths[i]);

      /* Close password server.  */
      mach_port_deallocate (mach_task_self (), svma_state.server);

      if (svma_state.num_auths > 0)
	free (svma_state.auths);
    }

  return err;
}
