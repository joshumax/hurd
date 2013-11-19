/* Uid/gid parsing/frobbing

   Copyright (C) 1997,2001 Free Software Foundation, Inc.

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

#ifndef __UGIDS_H__
#define __UGIDS_H__

#include <stdlib.h>		/* For inline function stuff.  */
#include <idvec.h>
#include <features.h>
#include <errno.h>
#include <sys/types.h>

#ifdef UGIDS_DEFINE_EI
#define UGIDS_EI
#else
#define UGIDS_EI __extern_inline
#endif

/* A structure holding a set of the common various types of ids.  */
struct ugids
{
  struct idvec eff_uids;	/* Effective UIDs */
  struct idvec eff_gids;	/* Effective GIDs */
  struct idvec avail_uids;	/* Available UIDs */
  struct idvec avail_gids;	/* Available GIDs */

  /* These should be a subset of EFF/AVAIL_GIDS, containing those gids which
     are present only by implication from uids in EFF/AVAIL_UIDS.  */
  struct idvec imp_eff_gids;
  struct idvec imp_avail_gids;
};

#define UGIDS_INIT { IDVEC_INIT, IDVEC_INIT, IDVEC_INIT, IDVEC_INIT, IDVEC_INIT, IDVEC_INIT }

/* Return a new ugids structure, or 0 if an allocation error occurs.  */
struct ugids *make_ugids ();

extern void ugids_fini (struct ugids *ugids);

extern void ugids_free (struct ugids *ugids);

extern int ugids_is_empty (const struct ugids *ugids);

extern int ugids_equal (const struct ugids *ugids1, const struct ugids *ugids2);

#if defined(__USE_EXTERN_INLINES) || defined(UGIDS_DEFINE_EI)

/* Free all resources used by UGIDS except UGIDS itself.  */
UGIDS_EI void
ugids_fini (struct ugids *ugids)
{
  idvec_fini (&ugids->eff_uids);
  idvec_fini (&ugids->eff_gids);
  idvec_fini (&ugids->avail_uids);
  idvec_fini (&ugids->avail_gids);
  idvec_fini (&ugids->imp_eff_gids);
  idvec_fini (&ugids->imp_avail_gids);
}

/* Free all resources used by UGIDS.  */
UGIDS_EI void
ugids_free (struct ugids *ugids)
{
  ugids_fini (ugids);
  free (ugids);
}

/* Return true if UGIDS contains no ids.  */
UGIDS_EI int
ugids_is_empty (const struct ugids *ugids)
{
  /* We needn't test the imp_*_gids vectors because they are subsets of the
     corresponding *_gids vectors.  */
  return
    idvec_is_empty (&ugids->eff_uids)
    && idvec_is_empty (&ugids->eff_gids)
    && idvec_is_empty (&ugids->avail_uids)
    && idvec_is_empty (&ugids->avail_gids);
}

/* Free all resources used by UGIDS except UGIDS itself.  */
UGIDS_EI int
ugids_equal (const struct ugids *ugids1, const struct ugids *ugids2)
{
  return
    idvec_equal (&ugids1->eff_uids, &ugids2->eff_uids)
    && idvec_equal (&ugids1->eff_gids, &ugids2->eff_gids)
    && idvec_equal (&ugids1->avail_uids, &ugids2->avail_uids)
    && idvec_equal (&ugids1->avail_gids, &ugids2->avail_gids)
    && idvec_equal (&ugids1->imp_eff_gids, &ugids2->imp_eff_gids)
    && idvec_equal (&ugids1->imp_avail_gids, &ugids2->imp_avail_gids);
}

#endif /* Use extern inlines.  */

/* Add all ids in NEW to UGIDS.  */
error_t ugids_merge (struct ugids *ugids, const struct ugids *new);

/* Set the ids in UGIDS to those in NEW.  */
error_t ugids_set (struct ugids *ugids, const struct ugids *new);

/* Remove the ids in SUB from those in UGIDS.  */
error_t ugids_subtract (struct ugids *ugids, const struct ugids *sub);

/* Mark as implied all gids in UGIDS that can be implied from its uids.  */
error_t ugids_imply_all (struct ugids *ugids);

/* Save any effective ids in UGIDS by merging them into the available ids,
   and removing them from the effective ones.  */
error_t ugids_save (struct ugids *ugids);

/* Verify that we have the right to the ids in UGIDS, given that we already
   possess those in HAVE_UIDS and HAVE_GIDS, asking for passwords where
   necessary.  0 is returned if access should be allowed, otherwise
   EINVAL if an incorrect password was entered, or an error relating to
   resource failure.  The GETPASS_FN, GETPASS_HOOK, VERIFY_FN, and
   VERIFY_HOOK arguments are as for the idvec_verify function (in <idvec.h>).  */
error_t ugids_verify (const struct ugids *ugids,
		      const struct idvec *have_uids,
		      const struct idvec *have_gids,
		      char *(*getpass_fn) (const char *prompt,
					   uid_t id, int is_group,
					   void *pwd_or_grp, void *hook),
		      void *getpass_hook,
		      error_t (*verify_fn) (const char *password,
					    uid_t id, int is_group,
					    void *pwd_or_grp, void *hook),
		      void *verify_hook);

/* Make an auth port from UGIDS and return it in AUTH, using authority in
   both the auth port FROM and the current auth port.  */
error_t ugids_make_auth (const struct ugids *ugids,
			 const auth_t *from, size_t num_from,
			 auth_t *auth);

/* Verify that we have the right to the ids in UGIDS, given that we already
   possess those in HAVE_UIDS and HAVE_GIDS (asking for passwords where
   necessary), and return corresponding authentication in AUTH; the auth
   ports in FROM, of length NUM_FROM, are used to supplement the auth port of
   the current process if necessary.  0 is returned if access should be
   allowed, otherwise EINVAL if an incorrect password was entered, or an
   error relating to resource failure.  GETPASS_FN and GETPASS_HOOK are as
   for the idvec_verify function in <idvec.h>.  */
error_t ugids_verify_make_auth (const struct ugids *ugids,
				const struct idvec *have_uids,
				const struct idvec *have_gids,
				char *(*getpass_fn) (const char *prompt,
						     uid_t id, int is_group,
						     void *pwd_or_grp,
						     void *hook),
				void *getpass_hook,
				const auth_t *from, size_t num_from,
				auth_t *auth);

/* Merge the ids from the auth port  AUTH into UGIDS.  */
error_t ugids_merge_auth (struct ugids *ugids, auth_t auth);

/* Return a string representation of the ids in UGIDS.  SHOW_VALUES and
   SHOW_NAMES reflect how each id is printed (if SHOW_NAMES is true values
   are used where names aren't available); if both are true, the
   `VALUE(NAME)' format is used.  ID_SEP, TYPE_SEP, and HDR_SEP contain the
   strings that separate, respectively, multiple ids of a particular type
   (default ","), the various types of ids (default ", "), and the name of
   each type from its ids (default ": ").  The empty string is returned for
   an empty list, and 0 for an allocation error.  */
char *ugids_rep (const struct ugids *ugids, int show_values, int show_names,
		 const char *id_sep, const char *type_sep,
		 const char *hdr_sep);

/* Add a new uid to UGIDS.  If AVAIL is true, it's added to the avail uids
   instead of the effective ones.  */
error_t ugids_add_uid (struct ugids *ugids, uid_t uid, int avail);

/* Add a new gid to UGIDS.  If AVAIL is true, it's added to the avail gids
   instead of the effective ones.  */
error_t ugids_add_gid (struct ugids *ugids, gid_t gid, int avail);

/* Add UID to UGIDS, plus any gids to which that user belongs.  If AVAIL is
   true, the are added to the avail gids instead of the effective ones.  */
error_t ugids_add_user (struct ugids *ugids, uid_t uid, int avail);

/* Install UID into UGIDS as the main user, making sure that the posix
   `real' and `saved' uid slots are filled in, and similarly add all
   groups to which UID belongs.  */
error_t ugids_set_posix_user (struct ugids *ugids, uid_t uid);

/* Params to be passed as the input when parsing UGIDS_ARGP.  */
struct ugids_argp_params
{
  /* Parsed ids should be added here.  */
  struct ugids *ugids;

  /* If true, parse multiple args as user otherwise, parse none.  */
  int parse_user_args;

  /* If true, and PARSE_USER_ARGS is true, add user args to the available
     ids, not the effective ones.  If both are true, add them to both.
     If both are false, use the special ugids_set_posix_user instead (which
     sets both, in a particular way).  */
  int user_args_are_effective;
  int user_args_are_available;

  /* If >= 0, a user that should be added if none are specified on the
     command line (following the same rules).  */
  int default_user;

  /* If true, at least one id has to be specified.  */
  int require_ids;
};

/* A parser for selecting a set of ugids.  */
extern struct argp ugids_argp;

#endif /* __UGIDS_H__ */
