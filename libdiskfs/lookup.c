/* Wrapper for diskfs_lookup_hard
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "priv.h"

/* Lookup in directory DP (which is locked) the name NAME.  TYPE will
   either be LOOKUP, CREATE, RENAME, or REMOVE.  CRED identifies the
   user making the call.

   If the name is found, return zero, and (if NP is nonzero) set *NP
   to point to the node for it, locked.  If the name is not found,
   return ENOENT, and (if NP is nonzero) set *NP to zero.  If NP is
   zero, then the node found must not be locked, even transitorily.
   Lookups for REMOVE and RENAME (which must often check permissions
   on the node being found) will always set NP.

   If DS is nonzero then:
     For LOOKUP: set *DS to be ignored by diskfs_drop_dirstat.
     For CREATE: on success, set *DS to be ignored by diskfs_drop_dirstat.
                 on failure, set *DS for a future call to diskfs_direnter.
     For RENAME: on success, set *DS for a future call to diskfs_dirrewrite.
                 on failure, set *DS for a future call to diskfs_direnter.
     For REMOVE: on success, set *DS for a future call to diskfs_dirremove.
                 on failure, set *DS to be ignored by diskfs_drop_dirstat.
   The caller of this function guarantees that if DS is nonzero, then
   either the appropriate call listed above or diskfs_drop_dirstat will
   be called with DS before the directory DP is unlocked, and guarantees
   that no lookup calls will be made on this directory between this
   lookup and the use (or descruction) of *DS.

   If you use the library's versions of diskfs_rename_dir,
   diskfs_clear_directory, and diskfs_init_dir, then lookups for `..'
   might have the flag SPEC_DOTDOT or'd in.  This has the following special
   meaning:
   For LOOKUP: DP should be unlocked and its reference dropped before
               returning.
   For RENAME and REMOVE: The node being found (*NP) is already held
               locked, so don't lock it or add a reference to it.
   (SPEC_DOTDOT will not be given with CREATE.)

   Return ENOTDIR if DP is not a directory.
   Return EACCES if CRED isn't allowed to search DP.
   Return EACCES if completing the operation will require writing
   the directory and diskfs_checkdirmod won't allow the modification.
   Return ENOENT if NAME isn't in the directory.
   Return EAGAIN if NAME refers to the `..' of this filesystem's root.
   Return EIO if appropriate.
   
   This function is a wrapper for diskfs_lookup_hard. 
*/
error_t 
diskfs_lookup (struct node *dp, char *name, enum lookup_type type,
	       struct node **np, struct dirstat *ds,
	       struct protid *cred)
{
  error_t err;
  
  if (type == REMOVE || type == RENAME)
    assert (np);

  if (!S_ISDIR (dp->dn_stat.st_mode))
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return ENOTDIR;
    }
  err = diskfs_access (dp, S_IEXEC, cred);
  if (err)
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return err;
    }

  if (type == LOOKUP)
    {
      /* Check the cache first */
      struct node *cached = diskfs_check_lookup_cache (dp, name);

      if (cached == (struct node *)-1)
	/* Negative lookup cached.  */
	{
	  if (np)
	    *np = 0;
	  dp->dn_set_atime = 1;
	  return ENOENT;
	}
      else if (cached)
	{
	  if (np)
	    *np = cached;	/* Return what we found.  */
	  else
	    /* Ick, the user doesn't want the result, we have to drop our
	       reference.  */
	    if (cached == dp)
	      diskfs_nrele (cached);
	    else
	      diskfs_nput (cached);
	  if (ds)
	    diskfs_null_dirstat (ds);
	  dp->dn_set_atime = 1;
	  return 0;
	}
    }
  
  err = diskfs_lookup_hard (dp, name, type, np, ds, cred);
  dp->dn_set_atime = 1;
  if (err && err != ENOENT)
    return err;
  
  if (type == RENAME
      || (type == CREATE && err == ENOENT)
      || (type == REMOVE && err != ENOENT))
    {
      error_t err2 = diskfs_checkdirmod (dp, (err || !np) ? 0 : *np, cred);
      if (err2)
	return err2;
    }

  if ((type == LOOKUP || type == CREATE) && !err && np)
    diskfs_enter_lookup_cache (dp, *np, name);
    
  return err;
}

