/* Wrapper for diskfs_lookup_hard
   Copyright (C) 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
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
#include <string.h>

/* Lookup in directory DP (which is locked) the name NAME.  TYPE will
   either be LOOKUP, CREATE, RENAME, or REMOVE.  CRED identifies the
   user making the call.

   NAME will have leading and trailing slashes stripped.  It is an
   error if there are internal slashes.  NAME will be modified in
   place if there are slashes in it; it is therefore an error to
   specify a constant NAME which contains slashes.

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

   This function is a wrapper for diskfs_lookup_hard.  */
error_t
diskfs_lookup (struct node *dp, const char *name, enum lookup_type type,
	       struct node **np, struct dirstat *ds, struct protid *cred)
{
  error_t err;
  struct node *cached;

  if (type == REMOVE || type == RENAME)
    assert_backtrace (np);

  if (!S_ISDIR (dp->dn_stat.st_mode))
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return ENOTDIR;
    }

  /* Strip leading and trailing slashes. */
  while (*name == '/')
    name++;

  if (name[0] == '\0')
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return EINVAL;
    }
  else
    {
      char *p = strchr (name, '/');
      if (p != 0)
	{
	  *p = '\0';
	  do
	    ++p;
	  while (*p == '/');
	  if (*p != '\0')
	    {
	      if (ds)
		diskfs_null_dirstat (ds);
	      return EINVAL;
	    }
	}
    }


  err = fshelp_access (&dp->dn_stat, S_IEXEC, cred->user);
  if (err)
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return err;
    }

  if (dp == cred->po->shadow_root
      && name[0] == '.' && name[1] == '.' && name[2] == '\0')
    /* Ran into the root.  */
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return EAGAIN;
    }

  if (type == LOOKUP)
    /* Check the cache first */
    cached = diskfs_check_lookup_cache (dp, name);
  else
    cached = 0;

  if (cached == (struct node *)-1)
    /* Negative lookup cached.  */
    {
      if (np)
	*np = 0;
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
    }
  else
    {
      err = diskfs_lookup_hard (dp, name, type, np, ds, cred);
      if (err && err != ENOENT)
	return err;

      if (type == RENAME
	  || (type == CREATE && err == ENOENT)
	  || (type == REMOVE && err != ENOENT))
	{
	  error_t err2;

	  if (diskfs_name_max > 0 && strlen (name) > diskfs_name_max)
	    err2 = ENAMETOOLONG;
	  else
	    err2 = fshelp_checkdirmod (&dp->dn_stat,
				       (err || !np) ? 0 : &(*np)->dn_stat,
				       cred->user);
	  if (err2)
	    {
	      if (np && !err)
		{
		  if (*np == dp)
		    diskfs_nrele (*np);
		  else
		    diskfs_nput (*np);
		  *np = 0;
		}
	      return err2;
	    }
	}

      if ((type == LOOKUP || type == CREATE) && !err && np)
	diskfs_enter_lookup_cache (dp, *np, name);
      else if (type == LOOKUP && err == ENOENT)
	diskfs_enter_lookup_cache (dp, 0, name);
    }

  return err;
}
