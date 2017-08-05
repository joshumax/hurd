/* Routines for vectors of uids/gids

   Copyright (C) 1995, 1996, 1997, 1998, 1999 Free Software Foundation, Inc.

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

#include <malloc.h>
#include <string.h>

#include "idvec.h"

/* Return a new, empty, idvec, or NULL if there wasn't enough memory.  */
struct idvec *
make_idvec ()
{
  struct idvec *idvec = malloc (sizeof (struct idvec));
  if (idvec)
    {
      idvec->alloced = idvec->num = 0;
      idvec->ids = 0;
    }
  return idvec;
}

/* Free's IDVEC, but not the storage pointed to by the IDS field.  */
void
idvec_free_wrapper (struct idvec *idvec)
{
  free (idvec);
}

void
idvec_free_contents (struct idvec *idvec)
{
  if (idvec->alloced)
    free (idvec->ids);
}

void
idvec_free (struct idvec *idvec)
{
  idvec_free_contents (idvec);
  idvec_free_wrapper (idvec);
}

/* Ensure that IDVEC has enough spaced allocated to hold NUM ids, thus
   ensuring that any subsequent ids added won't return a memory allocation
   error unless it would result in more ids that NUM.  ENOMEM is returned if
   a memory allocation error occurs.  */
error_t
idvec_ensure (struct idvec *idvec, unsigned num)
{
  if (num > idvec->alloced)
    {
      uid_t *_ids = realloc (idvec->ids, num * sizeof (uid_t));
      if (! _ids)
	return ENOMEM;
      idvec->ids = _ids;
      idvec->alloced = num;
    }
  return 0;
}

/* Like idvec_ensure(), but takes INC, the increment of the number of ids
   already in IDVEC as an argument.  */
error_t
idvec_grow (struct idvec *idvec, unsigned inc)
{
  return idvec_ensure (idvec, idvec->num + inc);
}

/* Returns true if IDVEC contains ID, at or after position POS.  */
int
idvec_tail_contains (const struct idvec *idvec, unsigned pos, uid_t id)
{
  uid_t *ids = idvec->ids, *end = ids + idvec->num, *p = ids + pos;
  while (p < end)
    if (*p++ == id)
      return  1;
  return 0;
}

/* Insert ID into IDVEC at position POS, returning ENOMEM if there wasn't
   enough memory, or 0.  */
error_t
idvec_insert (struct idvec *idvec, unsigned pos, uid_t id)
{
  error_t err = 0;
  unsigned num = idvec->num;
  unsigned new_num = (pos < num ? num + 1 : pos + 1);

  if (idvec->alloced == num)
    /* If we seem to be growing by just one, actually prealloc some more. */
    err = idvec_ensure (idvec, new_num + num);
  else
    err = idvec_ensure (idvec, new_num);

  if (! err)
    {
      uid_t *ids = idvec->ids;
      if (pos < num)
	memmove (ids + pos + 1, ids + pos, (num - pos) * sizeof (uid_t));
      else if (pos > num)
	memset (ids + num, 0, (pos - num) * sizeof(uid_t));
      ids[pos] = id;
      idvec->num = new_num;
    }

  return err;
}

/* Add ID onto the end of IDVEC, returning ENOMEM if there's not enough memory,
   or 0.  */
error_t
idvec_add (struct idvec *idvec, uid_t id)
{
  return idvec_insert (idvec, idvec->num, id);
}

/* If IDVEC doesn't contain ID, add it onto the end, returning ENOMEM if
   there's not enough memory; otherwise, do nothing.  */
error_t
idvec_add_new (struct idvec *idvec, uid_t id)
{
  if (idvec_contains (idvec, id))
    return 0;
  else
    return idvec_add (idvec, id);
}

/* If IDVEC doesn't contain ID at position POS or after, insert it at POS,
   returning ENOMEM if there's not enough memory; otherwise, do nothing.  */
error_t
idvec_insert_new (struct idvec *idvec, unsigned pos, uid_t id)
{
  if (idvec_tail_contains (idvec, pos, id))
    return 0;
  else
    return idvec_insert (idvec, pos, id);
}

/* Set the ids in IDVEC to IDS (NUM elements long); delete whatever
   the previous ids were. */
error_t
idvec_set_ids (struct idvec *idvec, const uid_t *ids, unsigned num)
{
  error_t err;

  err = idvec_ensure (idvec, num);
  if (!err)
    {
      memcpy (idvec->ids, ids, num * sizeof (uid_t));
      idvec->num = num;
    }
  return err;
}

/* Like idvec_set_ids, but get the new ids from new. */
error_t
idvec_set (struct idvec *idvec, const struct idvec *new)
{
  return idvec_set_ids (idvec, new->ids, new->num);
}

/* Adds each id in the vector IDS (NUM elements long) to IDVEC, as long as it
   wasn't previously in IDVEC.  */
error_t
idvec_merge_ids (struct idvec *idvec, const uid_t *ids, unsigned num)
{
  error_t err = 0;
  unsigned num_old = idvec->num;
  while (num-- > 0 && !err)
    {
      unsigned int i;
      for (i = 0; i < num_old; i++)
	if (idvec->ids[i] == *ids)
	  break;
      if (i == num_old)
	err = idvec_add (idvec, *ids);
      ids++;
    }
  return err;
}

/* Adds each id from  NEW to IDVEC, as if with idvec_add_new().  */
error_t
idvec_merge (struct idvec *idvec, const struct idvec *new)
{
  return idvec_merge_ids (idvec, new->ids, new->num);
}

/* Remove any occurrences of ID in IDVEC after position POS.
   Returns true if anything was done.  */
int
idvec_remove (struct idvec *idvec, unsigned pos, uid_t id)
{
  if (pos < idvec->num)
    {
      int left = idvec->num - pos;
      uid_t *ids = idvec->ids + pos, *targ = ids;
      while (left--)
	{
	  if (*ids != id)
	    {
	      if (ids != targ)
		*targ = *ids;
	      targ++;
	    }
	  ids++;
	}
      if (ids == targ)
	return 0;
      idvec->num = targ - idvec->ids;
      return 1;
    }
  else
    return 0;
}

/* Remove all ids in SUB from IDVEC, returning true if anything was done. */
int
idvec_subtract (struct idvec *idvec, const struct idvec *sub)
{
  unsigned int i;
  int done = 0;
  for (i = 0; i < sub->num; i++)
    done |= idvec_remove (idvec, 0, sub->ids[i]);
  return done;
}

/* Remove all ids from IDVEC that are *not* in KEEP, returning true if
   anything was changed. */
int
idvec_keep (struct idvec *idvec, const struct idvec *keep)
{
  uid_t *old = idvec->ids, *new = old, *end = old + idvec->num;

  while (old < end)
    {
      uid_t id = *old++;
      if (idvec_contains (keep, id))
	{
	  if (old != new)
	    *new = id;
	  new++;
	}
    }

  if (old != new)
    {
      idvec->num = new - idvec->ids;
      return 1;
    }
  else
    return 0;
}

/* Deleted the id at position POS in IDVEC.  */
void
idvec_delete (struct idvec *idvec, unsigned pos)
{
  unsigned num = idvec->num;
  if (pos < num)
    {
      uid_t *ids = idvec->ids;
      idvec->num = --num;
      if (num > pos)
	memmove (ids + pos, ids + pos + 1, (num - pos) * sizeof (uid_t));
    }
}

/* Insert ID at position POS in IDVEC, remove any instances of ID previously
   present at POS or after.  ENOMEM is returned if there's not enough memory,
   otherwise 0.  */
error_t
idvec_insert_only (struct idvec *idvec, unsigned pos, uid_t id)
{
  if (idvec->num > pos && idvec->ids[pos] == id)
    return 0;
  else
    {
      idvec_remove (idvec, pos, id);
      return idvec_insert (idvec, pos, id);
    }
}

/* EFF and AVAIL should be idvec's corresponding to a processes
   effective and available ids.  ID replaces the first id in EFF, and,
   if there are any IDs in AVAIL, replaces the second ID in AVAIL;
   what it replaces in any case is preserved by adding it to AVAIL if
   not already present.  In addition, the If SECURE is non-NULL, and
   ID was not previously present in either EFF or AVAIL, then *SECURE
   is set to true.  ENOMEM is returned if a malloc fails, otherwise 0.
   The return parameters are only touched if this call succeeds.  */
error_t
idvec_setid (struct idvec *eff, struct idvec *avail, uid_t id, int *secure)
{
  error_t err;
  /* True if ID was not previously present in either EFF or AVAIL.  */
  int _secure = !idvec_contains (eff, id) &&  !idvec_contains (avail, id);

  if (eff->num > 0)
    {
      /* If there are any old effective ids, we replace eff[0] with
	 ID, and try to preserve the old eff[0] by putting it in AVAIL
	 list if necessary.  */
      err = idvec_add_new (avail, eff->ids[0]);
      if (!err)
	eff->ids[0] = id;
    }
  else
    /* No previous effective ids, just make ID the first one.  */
    err = idvec_add (eff, id);

  if (avail->num > 0 && !err)
    err = idvec_insert_only (avail, 1, id);
  
  if (err)
    return err;

  if (_secure && secure && !*secure)
    *secure = 1;

  return 0;
}
