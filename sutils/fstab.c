/* Fstab filesystem frobbing

   Copyright (C) 1996 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <error.h>
#include <argz.h>

#include <hurd/fsys.h>

#include "fstab.h"

extern error_t fsys_set_readonly (fsys_t fsys, int readonly);
extern error_t fsys_get_readonly (fsys_t fsys, int *readonly);
extern error_t fsys_remount (fsys_t fsys);

extern file_t file_name_lookup_carefully (const char *file,
					  int flags, mode_t mode);

/* Return a new fstab in FSTAB.  */
error_t
fstab_create (struct fstypes *types, struct fstab **fstab)
{
  struct fstab *new = malloc (sizeof (struct fstab));
  if (new)
    {
      new->entries = 0;
      new->types = types;
      *fstab = new;
      return 0;
    }
  else
    return ENOMEM;
}

/* Free FSTAB and all of its entries.  */
void
fstab_free (struct fstab *fstab)
{
  while (fstab->entries)
    fs_free (fstab->entries);
  free (fstab);
}

/* Return a new fstypes structure in TYPES.  SEARCH_FMTS is copied.  */
error_t
fstypes_create (char *search_fmts, size_t search_fmts_len,
		struct fstypes **types)
{
  struct fstypes *new = malloc (sizeof (struct fstypes));
  if (new)
    {
      new->entries = 0;
      new->program_search_fmts = malloc (search_fmts_len);
      new->program_search_fmts_len = search_fmts_len;
      if (! new->program_search_fmts)
	{
	  free (types);
	  return ENOMEM;
	}
      *types = new;
      return 0;
    }
  else
    return ENOMEM;
}

/* Return an fstype entry in TYPES called NAME, in FSTYPE.  If there is no
   existing entry, an attempt to find a fsck program with the given type,
   using the alternatives in the FSCK_SEARCH_FMTS field in TYPES.  If
   one is found, it is added to TYPES, otherwise an new entry is created
   with a NULL PROGRAM field.  */
error_t
fstypes_get (struct fstypes *types, char *name, struct fstype **fstype)
{
  char *fmts, *fmt;
  size_t fmts_len;
  struct fstype *type;

  for (type = types->entries; type; type = type->next)
    if (strcasecmp (type->name, name) == 0)
      {
	*fstype = type;
	return 0;
      }

  /* No existing entry, make a new one.  */
  type = malloc (sizeof (struct fstype));
  if (! type)
    return ENOMEM;

  type->name = strdup (name);
  type->program = 0;
  type->next = types->entries;
  types->entries = type;

  fmts = types->program_search_fmts;
  fmts_len = types->program_search_fmts_len;

  for (fmt = fmts; fmt; fmt = argz_next (fmts, fmts_len, fmt))
    {
      int fd;
      char *program;

      asprintf (&program, fmt, name);
      fd = open (program, O_EXEC);
      if (fd < 0)
	{
	  free (program);	/* Failed.  */
	  if (errno != ENOENT && errno != EACCES)
	    /* The program's there but something went wrong; fail.  */
	    return errno;
	}
      else
	/* We can open for exec, but check the stat info too (e.g. root can
	   open everything).  */
	{
	  struct stat stat;
	  int rv = fstat (fd, &stat);

	  close (fd);

	  if (rv < 0)
	    return errno;

	  if (stat.st_mode & S_IXUSR)
	    /* Yup execute bit is set.  This must a program...  */
	    {
	      type->program = program;
	      break;
	    }
	}
    }

  return 0;
}

/* Copy MNTENT into FS, copying component strings as well.  */
error_t
fs_set_mntent (struct fs *fs, struct mntent *mntent)
{
  char *end;
  size_t needed = 0;

  if (fs->storage)
    free (fs->storage);

  /* Allocate space for all string mntent fields in FS.  */
#define COUNT(field) if (mntent->field) needed += strlen (mntent->field) + 1;
  COUNT (mnt_fsname);
  COUNT (mnt_dir);
  COUNT (mnt_type);
  COUNT (mnt_opts);
#undef COUNT

  fs->storage = malloc (needed);
  if (! fs->storage)
    return ENOMEM;

  /* Copy MNTENT into FS; string-valued fields will be fixed up next.  */
  fs->mntent = *mntent;

  /* Copy each mntent field from MNTENT into FS's version.  */
  end = fs->storage;
#define STORE(field) \
    fs->mntent.field = end; end = stpcpy (end, mntent->field) + 1
  STORE (mnt_fsname);
  STORE (mnt_dir);
  STORE (mnt_type);
  STORE (mnt_opts);
#undef STORE

  if (fs->type && strcasecmp (fs->type->name, mntent->mnt_type) != 0)
    fs->type = 0;		/* Type is different.  */

  return 0;
}

/* Returns an fstype for FS in TYPE, trying to fillin FS's type field if
   necessary.  */
error_t
fs_type (struct fs *fs, struct fstype **type)
{
  error_t err = 0;
  if (! fs->type)
    err = fstypes_get (fs->fstab->types, fs->mntent.mnt_type, &fs->type);
  if (! err)
    *type = fs->type;
  return err;
}

/* Looks to see if FS is currently mounted, being very careful to avoid
   mounting anything that's not already, and fills in the fsys & mounted
   fields in FS.  */
static error_t
_fs_check_mounted (struct fs *fs)
{
  error_t err = 0;

  if (fs->mounted < 0)
    /* The mounted field in FS is -1 if we're not sure.  */
    {
      file_t mount_point =
	file_name_lookup_carefully (fs->mntent.mnt_dir, O_NOTRANS, 0);

      if (mount_point != MACH_PORT_NULL)
	/* The node exists.  Is it the root of an active translator?
	   [Note that it could be a different translator than the one in
	   the mntent, but oh well, nothing we can do about that.]  */
	{
	  if (fs->fsys != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), fs->fsys);
	  err = file_get_translator_cntl (mount_point, &fs->fsys);
	  if (err ==  EINVAL || err == EOPNOTSUPP)
	    /* Either the mount point doesn't exist, or wasn't mounted.  */
	    {
	      fs->fsys = MACH_PORT_NULL;
	      err = 0;
	    }
	}
      else if (errno == ENXIO)
	/* Ran into an inactive passive translator.  FS can't be mounted.  */
	{
	  fs->fsys = MACH_PORT_NULL;
	  err = 0;
	}

      if (! err)
	fs->mounted = (fs->fsys != MACH_PORT_NULL);
    }

  return err;
}

/* Looks to see if FS is currently mounted, being very careful to avoid
   mounting anything that's not already, and returns the control port for the
   mounted filesystem (MACH_PORT_NULL if not mounted).  */
error_t
fs_fsys (struct fs *fs, fsys_t *fsys)
{
  error_t err = _fs_check_mounted (fs);
  if (!err && fsys)
    *fsys = fs->fsys;
  return err;
}

/* Looks to see if FS is currently mounted, being very careful to avoid
   mounting anything that's not already, and returns the boolean MOUNTED.  */
error_t
fs_mounted (struct fs *fs, int *mounted)
{
  error_t err = _fs_check_mounted (fs);
  if (!err && mounted)
    *mounted = fs->mounted;
  return err;
}

/* Looks to see if FS is currently mounted readonly, being very careful to
   avoid mounting anything that's not already, and returns the boolean
   READONLY.  If FS isn't mounted at all, READONLY is set to 1 (it's not
   going to write anything!).  */
error_t
fs_readonly (struct fs *fs, int *readonly)
{
  error_t err = 0;

  if (fs->readonly < 0)
    /* Unknown. */
    {
      fsys_t fsys;

      err = fs_fsys (fs, &fsys);
      if (! err)
	if (fsys == MACH_PORT_NULL)
	  fs->readonly = 1;
	else
	  err = fsys_get_readonly (fsys, &fs->readonly);
    }

  if (!err && readonly)
    *readonly = fs->readonly;

  return err;
}

/* If FS is currently mounted writable, try to make it readonly.  XXX If FS
   is not mounted at all, then nothing is done.   */
error_t
fs_set_readonly (struct fs *fs, int readonly)
{
  int currently_readonly;
  error_t err = fs_readonly (fs, &currently_readonly);

  readonly = !!readonly;

  if (!err && readonly != currently_readonly)
    /* We have to try and change the readonly state.  */
    {
      fsys_t fsys;
      err = fs_fsys (fs, &fsys);
      if (!err && fsys != MACH_PORT_NULL) /* XXX What to do if not mounted? */
	err = fsys_set_readonly (fsys, readonly);
      if (! err)
	fs->readonly = readonly;
    }

  return err;
}

/* If FS is currently mounted tell lit to remount the device.  XXX If FS is
   not mounted at all, then nothing is done.  */
error_t
fs_remount (struct fs *fs)
{
  fsys_t fsys;
  error_t err = fs_fsys (fs, &fsys);
  if (!err && fsys != MACH_PORT_NULL) /* XXX What to do if not mounted? */
    err = fsys_remount (fsys);
  return err;
}

/* Returns the FS entry in FSTAB with the device field NAME (there can only
   be one such entry).  */
inline struct fs *
fstab_find_device (struct fstab *fstab, char *name)
{
  struct fs *fs;
  for (fs = fstab->entries; fs; fs = fs->next)
    if (strcmp (fs->mntent.mnt_fsname, name) == 0)
      return fs;
  return 0;
}

/* Returns the FS entry in FSTAB with the mount point NAME (there can only
   be one such entry).  */
inline struct fs *
fstab_find_mount (struct fstab *fstab, char *name)
{
  struct fs *fs;
  for (fs = fstab->entries; fs; fs = fs->next)
    if (strcmp (fs->mntent.mnt_dir, name) == 0)
      return fs;
  return 0;
}

/* Returns the FS entry in FSTAB with the device or mount point NAME (there
   can only be one such entry).  */
inline struct fs *
fstab_find (struct fstab *fstab, char *name)
{
  return fstab_find_device (fstab, name) ?: fstab_find_mount (fstab, name);
}

/* Cons FS onto the beginning of FSTAB's entry list.  */
static void
_fstab_add (struct fstab *fstab, struct fs *fs)
{
  fs->fstab = fstab;
  fs->next = fstab->entries;
  fs->self = &fstab->entries;
  if (fstab->entries)
    fstab->entries->self = &fs->next;
  fstab->entries = fs;
}

/* Destroy FS, removing it from its containing FSTAB.  */
void
fs_free (struct fs *fs)
{
  *fs->self = fs->next;		/* unlink from chain */
  if (fs->storage)
    free (fs->storage);
  if (fs->fsys != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), fs->fsys);
  free (fs);
}

/* Add an entry for MNTENT to FSTAB, removing any existing entries that
   conflict (in either the device or mount point).  If RESULT is non-zero, the
   new entry is returne in it.  */
error_t
fstab_add_mntent (struct fstab *fstab, struct mntent *mntent,
		  struct fs **result)
{
  int new = 0;			/* True if we didn't overwrite an old entry.  */
  error_t err = 0;
  struct fs *fs = fstab_find_device (fstab, mntent->mnt_fsname);
  struct fs *mounted_fs = fstab_find_mount (fstab, mntent->mnt_dir);

  if (! fs)
    /* No old entry with the same device; see if there's one with the same
       mount point.  */
    {
      fs = mounted_fs;
      mounted_fs = 0;
    }
    
  if (! fs)
    /* No old entry, make a new one.  */
    {
      fs = malloc (sizeof (struct fs));
      if (fs)
	{
	  bzero (fs, sizeof (struct fs));
	  new = 1;
	}
      else
	err = ENOMEM;
    }

  if (! err)
    /* Try and fill in FS's mntent.  */
    err = fs_set_mntent (fs, mntent);

  if (new)
    if (! err)
      _fstab_add (fstab, fs);
    else if (fs)
      free (fs);

  if (!err && mounted_fs)
    /* Get rid of the conflicting entry MOUNTED_FS.  */
    fs_free (mounted_fs);
    
  if (!err && result)
    *result = fs;

  return err;
}

/* Copy the entry FS (which should belong to another fstab than DST) into
   DST.  If DST & SRC have different TYPES fields, EINVAL is returned.  If
   COPY is non-zero, the copy is returned in it.  */
error_t
fstab_add_fs (struct fstab *dst, struct fs *fs, struct fs **copy)
{
  error_t err;
  struct fstab *src = fs->fstab;

  if (dst->types != src->types)
    return EINVAL;

  err = fstab_add_mntent (dst, &fs->mntent, copy);
  if (err)
    return err;

  (*copy)->type = fs->type;

  return 0;
}

/* Merge SRC into DST, as if by calling fstab_add_fs on DST with every
   entry in SRC, and then deallocating SRC.  If DST & SRC have different
   TYPES fields, EINVAL is returned.  */
error_t
fstab_merge (struct fstab *dst, struct fstab *src)
{
  struct fs *fs;

  if (dst->types != src->types)
    return EINVAL;

  /* Remove entries in DST which conflict with those in SRC.  */
  for (fs = src->entries; fs; fs = fs->next)
    {
      struct fs *old_fs;

      old_fs = fstab_find_device (dst, fs->mntent.mnt_fsname);
      if (old_fs)
	fs_free (old_fs);
      old_fs = fstab_find_mount (dst, fs->mntent.mnt_dir);
      if (old_fs)
	fs_free (old_fs);
    }
    
  /* Now that we know there are no conflicts, steal all SRC's entries and
     cons them onto DST.  */
  for (fs = src->entries; fs; fs = fs->next)
    _fstab_add (dst, fs);

  /* Now all entries from SRC should be in DST, so just deallocate SRC.  */
  free (src);

  return 0;
}

/* Reads fstab-format entries into FSTAB from the file NAME.  Any entries
   duplicating one already in FS_LIST supersede the existing entry.  */
error_t
fstab_read (struct fstab *fstab, char *name)
{
  error_t err;
  /* Used to hold entries from the file, before merging with FSTAB at the
     end.  */
  struct fstab *contents;
  FILE *stream = setmntent (name, "r");

  if (! stream)
    return errno;

  err = fstab_create (fstab->types, &contents);
  if (! err)
    {
      while (!err && !feof (stream))
	{
	  struct mntent *mntent = getmntent (stream);

	  if (! mntent)
	    err = errno;
	  else if (fstab_find_device (fstab, mntent->mnt_fsname))
	    error (0, 0, "%s: Warning: duplicate entry for device %s (%s)",
		   name, mntent->mnt_fsname, mntent->mnt_dir);
	  else if (fstab_find_mount (fstab, mntent->mnt_dir))
	    error (0, 0, "%s: Warning: duplicate entry for mount point %s (%s)",
		   name, mntent->mnt_dir, mntent->mnt_fsname);
	  else
	    err = fstab_add_mntent (fstab, mntent, 0);
	}

      if (! err)
	fstab_merge (fstab, contents);
      else
	fstab_free (contents);
    }

  endmntent (stream);

  return err;
}

/* Return the next pass number that applies to any filesystem in FSTAB that
   is greater than PASS, or -1 if there isn't any.  */
int fstab_next_pass (struct fstab *fstab, int pass)
{
  int next_pass = -1;
  struct fs *fs;
  for (fs = fstab->entries; fs; fs = fs->next)
    if (fs->mntent.mnt_passno > pass)
      if (next_pass < 0 || fs->mntent.mnt_passno < next_pass)
	{
	  next_pass = fs->mntent.mnt_passno;
	  if (next_pass == pass + 1)
	    break;		/* Only possible answer.  */
	}
  return next_pass;
}
