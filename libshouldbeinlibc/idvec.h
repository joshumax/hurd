/* Routines for vectors of uids/gids

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#ifndef __IDVEC_H__
#define __IDVEC_H__

#include <sys/types.h>
#include <errno.h>
#include <hurd/hurd_types.h>

#ifndef IDVEC_EI
#define IDVEC_EI extern inline
#endif

struct idvec
{
  uid_t *ids;
  unsigned num, alloced;
};

/* Return a new, empty, idvec, or NULL if there wasn't enough memory.  */
struct idvec *make_idvec (void);

/* Free the storage pointed to by IDVEC->ids.  */
void idvec_free_contents (struct idvec *idvec);

/* Free IDVEC, but not the storage pointed to by the IDS field.  */
void idvec_free_wrapper (struct idvec *idvec);

/* Free IDVEC and any storage associated with it.  */
void idvec_free (struct idvec *idvec);

/* Mark IDVEC as not containing any ids.  */
IDVEC_EI void
idvec_clear (struct idvec *idvec)
{
  idvec->num = 0;
}

/* Ensure that IDVEC has enough spaced allocated to hold NUM ids, thus
   ensuring that any subsequent ids added won't return a memory allocation
   error unless it would result in more ids that NUM.  ENOMEM is returned if
   a memory allocation error occurs.  */
error_t idvec_ensure (struct idvec *idvec, unsigned num);

/* Like idvec_ensure(), but takes INC, the increment of the number of ids
   already in IDVEC as an argument.  */
error_t idvec_grow (struct idvec *idvec, unsigned inc);

/* Returns true if IDVEC contains ID, at or after position POS.  */
int idvec_tail_contains (struct idvec *idvec, unsigned pos, uid_t id);

/* Returns true if IDVEC contains ID.  */
int idvec_contains (struct idvec *idvec, uid_t id);

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
error_t idvec_set_ids (struct idvec *idvec, uid_t *ids, unsigned num);

/* Like idvec_set_ids, but get the new ids from new. */
error_t idvec_set (struct idvec *idvec, struct idvec *new);

/* Adds each id in the vector IDS (NUM elements long) to IDVEC, as if with
   idvec_add_new().  */
error_t idvec_merge_ids (struct idvec *idvec, uid_t *ids, unsigned num);

/* Adds each id from  NEW to IDVEC, as if with idvec_add_new().  */
error_t idvec_merge (struct idvec *idvec, struct idvec *new);

/* Remove any occurances of ID in IDVEC after position POS>  Returns true if
   anything was done.  */
int idvec_remove (struct idvec *idvec, unsigned pos, uid_t id);

/* Deleted the id at position POS in IDVEC.  */
void idvec_delete (struct idvec *idvec, unsigned pos);

/* Insert ID at position POS in IDVEC, remoint any instances of ID previously
   present at POS or after.  ENOMEM is returned if there's not enough memory,
   otherwise 0.  */
error_t idvec_insert_only (struct idvec *idvec, unsigned pos, uid_t id);

/* EFF and AVAIL should be idvec's corresponding to a process's effective and
   available ids.  ID replaces the first id in EFF, and what it replaces is
   preserved by adding it to AVAIL (if not already present).  If SECURE is
   non-NULL, and ID was not previously present in either EFF or AVAIL, then
   *SECURE is set to true.  ENOMEM is returned if a malloc fails, otherwise
   0.  The return parameters are only touched if this call succeeds.  */
error_t idvec_setid (struct idvec *eff, struct idvec *avail, uid_t id,
		     int *secure);

/* Add to all of EFF_UIDS, AVAIL_UIDS, EFF_GIDS, AVAIL_GIDS (as if with
   idvec_merge) the ids associated with the auth port AUTH.  Any of these
   parameters may be NULL if that information isn't desired.  */
error_t idvec_merge_auth (struct idvec *eff_uids, struct idvec *avail_uids,
			  struct idvec *eff_gids, struct idvec *avail_gids,
			  auth_t auth);

#endif /* __IDVEC_H__ */
