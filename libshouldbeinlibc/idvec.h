/* Routines for vectors of uids/gids

   Copyright (C) 1995,96,97,99,2001 Free Software Foundation, Inc.
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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef __IDVEC_H__
#define __IDVEC_H__

#include <sys/types.h>
#include <hurd/hurd_types.h>
#include <string.h>
#include <features.h>

#ifdef IDVEC_DEFINE_EI
#define IDVEC_EI
#else
#define IDVEC_EI __extern_inline
#endif

struct idvec
{
  uid_t *ids;
  unsigned num, alloced;
};

#define IDVEC_INIT { 0 }

/* Return a new, empty, idvec, or NULL if there wasn't enough memory.  */
struct idvec *make_idvec (void);

/* Free the storage pointed to by IDVEC->ids.  */
void idvec_free_contents (struct idvec *idvec);
#define idvec_fini idvec_free_contents

/* Free IDVEC, but not the storage pointed to by the IDS field.  */
void idvec_free_wrapper (struct idvec *idvec);

/* Free IDVEC and any storage associated with it.  */
void idvec_free (struct idvec *idvec);

extern void idvec_clear (struct idvec *idvec);

extern int idvec_is_empty (const struct idvec *idvec);

extern int idvec_equal (const struct idvec *idvec1, const struct idvec *idvec2);

#if defined(__USE_EXTERN_INLINES) || defined(IDVEC_DEFINE_EI)

/* Mark IDVEC as not containing any ids.  */
IDVEC_EI void
idvec_clear (struct idvec *idvec)
{
  idvec->num = 0;
}

/* Returns true if IDVEC contains no ids.  */
IDVEC_EI int
idvec_is_empty (const struct idvec *idvec)
{
  return idvec->num == 0;
}

/* Return true if IDVEC1 has contents identical to IDVEC2.  */
IDVEC_EI int
idvec_equal (const struct idvec *idvec1, const struct idvec *idvec2)
{
  size_t num = idvec1->num;
  return idvec2->num == num
    && (num == 0
	|| memcmp (idvec1->ids, idvec2->ids, num * sizeof *idvec1->ids) == 0);
}

#endif /* Use extern inlines.  */

/* Ensure that IDVEC has enough spaced allocated to hold NUM ids, thus
   ensuring that any subsequent ids added won't return a memory allocation
   error unless it would result in more ids that NUM.  ENOMEM is returned if
   a memory allocation error occurs.  */
error_t idvec_ensure (struct idvec *idvec, unsigned num);

/* Like idvec_ensure(), but takes INC, the increment of the number of ids
   already in IDVEC as an argument.  */
error_t idvec_grow (struct idvec *idvec, unsigned inc);

/* Returns true if IDVEC contains ID, at or after position POS.  */
int idvec_tail_contains (const struct idvec *idvec, unsigned pos, uid_t id);

extern int idvec_contains (const struct idvec *idvec, uid_t id);

#if defined(__USE_EXTERN_INLINES) || defined(IDVEC_DEFINE_EI)

/* Returns true if IDVEC contains ID.  */
IDVEC_EI int
idvec_contains (const struct idvec *idvec, uid_t id)
{
  return idvec_tail_contains (idvec, 0, id);
}

#endif /* Use extern inlines.  */

/* Insert ID into IDVEC at position POS, returning ENOMEM if there wasn't
   enough memory, or 0.  */
error_t idvec_insert (struct idvec *idvec, unsigned pos, uid_t id);

/* Add ID onto the end of IDVEC, returning ENOMEM if there's not enough memory,
   or 0.  */
error_t idvec_add (struct idvec *idvec, uid_t id);

/* If IDVEC doesn't contain ID, add it onto the end, returning ENOMEM if
   there's not enough memory; otherwise, do nothing.  */
error_t idvec_add_new (struct idvec *idvec, uid_t id);

/* If IDVEC doesn't contain ID at position POS or after, insert it at POS,
   returning ENOMEM if there's not enough memory; otherwise, do nothing.  */
error_t idvec_insert_new (struct idvec *idvec, unsigned pos, uid_t id);

/* Set the ids in IDVEC to IDS (NUM elements long); delete whatever
   the previous ids were. */
error_t idvec_set_ids (struct idvec *idvec, const uid_t *ids, unsigned num);

/* Like idvec_set_ids, but get the new ids from new. */
error_t idvec_set (struct idvec *idvec, const struct idvec *new);

/* Adds each id in the vector IDS (NUM elements long) to IDVEC, as if with
   idvec_add_new().  */
error_t idvec_merge_ids (struct idvec *idvec, const uid_t *ids, unsigned num);

/* Adds each id from  NEW to IDVEC, as if with idvec_add_new().  */
error_t idvec_merge (struct idvec *idvec, const struct idvec *new);

/* Remove all ids in SUB from IDVEC, returning true if anything was done. */
int idvec_subtract (struct idvec *idvec, const struct idvec *sub);

/* Remove all ids from IDVEC that are *not* in KEEP, returning true if
   anything was changed. */
int idvec_keep (struct idvec *idvec, const struct idvec *keep);

/* Remove any occurrences of ID in IDVEC after position POS>  Returns true if
   anything was done.  */
int idvec_remove (struct idvec *idvec, unsigned pos, uid_t id);

/* Deleted the id at position POS in IDVEC.  */
void idvec_delete (struct idvec *idvec, unsigned pos);

/* Insert ID at position POS in IDVEC, remove any instances of ID previously
   present at POS or after.  ENOMEM is returned if there's not enough memory,
   otherwise 0.  */
error_t idvec_insert_only (struct idvec *idvec, unsigned pos, uid_t id);

/* EFF and AVAIL should be idvec's corresponding to a process's
   effective and available ids.  ID replaces the first id in EFF, and,
   if there are any IDs in AVAIL, replaces the second ID in AVAIL;
   what it replaces in any case is preserved by adding it to AVAIL if
   not already present.  In addition, the If SECURE is non-NULL, and
   ID was not previously present in either EFF or AVAIL, then *SECURE
   is set to true.  ENOMEM is returned if a malloc fails, otherwise 0.
   The return parameters are only touched if this call succeeds.  */
error_t idvec_setid (struct idvec *eff, struct idvec *avail, uid_t id,
		     int *secure);

/* Add to all of EFF_UIDS, AVAIL_UIDS, EFF_GIDS, AVAIL_GIDS (as if with
   idvec_merge) the ids associated with the auth port AUTH.  Any of these
   parameters may be NULL if that information isn't desired.  */
error_t idvec_merge_auth (struct idvec *eff_uids, struct idvec *avail_uids,
			  struct idvec *eff_gids, struct idvec *avail_gids,
			  auth_t auth);

/* Add to GIDS those group ids implied by the users in UIDS.  */
error_t idvec_merge_implied_gids (struct idvec *gids, const struct idvec *uids);

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
error_t idvec_verify (const struct idvec *uids, const struct idvec *gids,
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

/* Return a string representation of the ids in IDVEC, each id separated by
   the string SEP (default ",").  SHOW_VALUES and SHOW_NAMES reflect how each
   id is printed (if SHOW_NAMES is true values are used where names aren't
   available); if both are true, the `VALUE(NAME)' format is used.
   ID_NAME_FN is used to map each id to a name; it should return a malloced
   string, which will be freed here.  The empty string is returned for an
   empty list, and 0 for an allocation error.  */
char *idvec_rep (const struct idvec *idvec,
		 int show_values, int show_names,
		 char *(*id_name_fn) (uid_t id),
		 const char *sep);

/* Like idvec_rep, mapping ids to user names.  */
char *idvec_uids_rep (const struct idvec *idvec,
		      int show_values, int show_names,
		      const char *sep);

/* Like idvec_rep, mapping ids to group names.  */
char *idvec_gids_rep (const struct idvec *idvec,
		      int show_values, int show_names,
		      const char *sep);

#endif /* __IDVEC_H__ */
