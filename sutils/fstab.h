/* Fstab filesystem frobbing

   Copyright (C) 1996, 1999 Free Software Foundation, Inc.

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

#ifndef __FSTAB_H__
#define __FSTAB_H__

#include <mntent.h>
#include <hurd.h>

struct fs
{
  struct fstab *fstab;		/* Containing fstab. */
  struct mntent mntent;		/* Mount entry from fstab file. */
  char *storage;		/* Storage for strings in MNTENT.  */
  struct fstype *type;		/* Only set if fs_type called. */
  int readonly, mounted;	/* Only set if fs_{readonly,mounted} called.
				   0 or 1 if known; -1 if unknown.  */
  fsys_t fsys;			/* Only set if fs_fsys called.  */
  struct fs *next, **self;
};

struct fstab
{
  struct fs *entries;
  struct fstypes *types;
};

struct fstype
{
  char *name;			/* Malloced.  */
  char *program;		/* Malloced.  */
  struct fstype *next;
};

struct fstypes
{
  struct fstype *entries;

  /* How to search for programs.  A '\0' separated list of strings.  Each
     should be a printf-style format string, which will be passed the
     filesystem type.  The first such expansion that results in an executable
     file will be chosen as the fsck program for that type.  */
  char *program_search_fmts;
  size_t program_search_fmts_len; /* Length of PROGRAM_SEARCH_FMTS. */
};

/* Return a new fstab in FSTAB.  */
error_t fstab_create (struct fstypes *types, struct fstab **fstab);

/* Free FSTAB and all of its entries.  */
void fstab_free (struct fstab *fstab);

/* Return a new fstypes structure in TYPES.  SEARCH_FMTS is copied.  */
error_t fstypes_create (const char *search_fmts, size_t search_fmts_len,
			struct fstypes **types);

/* Return an fstype entry in TYPES called NAME, in FSTYPE.  If there is no
   existing entry, an attempt to find a program with the given type,
   using the alternatives in the PROGRAM_SEARCH_FMTS field in TYPES.  If
   one is found, it is added to TYPES, otherwise a new entry is created
   with a null PROGRAM field.  */
error_t fstypes_get (struct fstypes *types,
		     const char *name, struct fstype **fstype);

/* Copy MNTENT into FS, copying component strings as well.  */
error_t fs_set_mntent (struct fs *fs, const struct mntent *mntent);

/* Returns an fstype for FS in TYPE, trying to fill in FS's type field if
   necessary.  */
error_t fs_type (struct fs *fs, struct fstype **type);

/* Looks to see if FS is currently mounted, being very careful to avoid
   mounting anything that's not already, and returns the control port for the
   mounted filesystem (MACH_PORT_NULL if not mounted).  */
error_t fs_fsys (struct fs *fs, fsys_t *fsys);

/* Looks to see if FS is currently mounted, being very careful to avoid
   mounting anything that's not already, and returns the boolean MOUNTED.  */
error_t fs_mounted (struct fs *fs, int *mounted);

/* Looks to see if FS is currently mounted readonly, being very careful to
   avoid mounting anything that's not already, and returns the boolean
   READONLY.  If FS isn't mounted at all, READONLY is set to 1 (it's not
   going to write anything!).  */
error_t fs_readonly (struct fs *fs, int *readonly);

/* If FS is currently mounted writable, try to make it readonly.  XXX If FS
   is not mounted at all, then nothing is done.   */
error_t fs_set_readonly (struct fs *fs, int readonly);

/* If FS is currently mounted tell it to remount the device.  XXX If FS is
   not mounted at all, then nothing is done.  */
error_t fs_remount (struct fs *fs);

/* Destroy FS, removing it from its containing FSTAB.  */
void fs_free (struct fs *fs);

/* Returns the FS entry in FSTAB with the device field NAME (there can only
   be one such entry).  */
struct fs *fstab_find_device (const struct fstab *fstab, const char *name);

/* Returns the FS entry in FSTAB with the mount point NAME (there can only
   be one such entry).  */
struct fs *fstab_find_mount (const struct fstab *fstab, const char *name);

/* Returns the FS entry in FSTAB with the device or mount point NAME (there
   can only be one such entry).  */
struct fs *fstab_find (const struct fstab *fstab, const char *name);

/* Add an entry for MNTENT to FSTAB, removing any existing entries that
   conflict (in either the device or mount point).  If RESULT is non-zero, the
   new entry is returned in it.  */
error_t fstab_add_mntent (struct fstab *fstab, const struct mntent *mntent,
			  struct fs **result);

/* Copy the entry FS (which should belong to another fstab than DST) into
   DST.  If DST & SRC have different TYPES fields, EINVAL is returned.  If
   COPY is non-zero, the copy is returned in it.  */
error_t fstab_add_fs (struct fstab *dst, const struct fs *fs,
		      struct fs **copy);

/* Merge SRC into DST, as if by calling fstab_add_fs on DST with every
   entry in SRC, and then deallocating SRC.  If DST & SRC have different
   TYPES fields, EINVAL is returned.  */
error_t fstab_merge (struct fstab *dst, struct fstab *src);

/* Reads fstab-format entries into FSTAB from the file NAME.  Any entries
   duplicating one already in FS_LIST supersede the existing entry.  */
error_t fstab_read (struct fstab *fstab, const char *name);

/* Return the next pass number that applies to any filesystem in FSTAB that
   is greater than PASS, or -1 if there isn't any.  */
int fstab_next_pass (const struct fstab *fstab, int pass);


struct argp;
extern const struct argp fstab_argp;
struct fstab_argp_params
{
  char *fstab_path;
  char *program_search_fmts;
  size_t program_search_fmts_len;

  int do_all;
  char *types;
  size_t types_len;
  char *exclude;
  size_t exclude_len;

  char *names;
  size_t names_len;
};

struct fstab *fstab_argp_create (struct fstab_argp_params *params,
				 const char *default_search_fmts,
				 size_t default_search_fmts_len);


#endif /* __FSTAB_H__ */
