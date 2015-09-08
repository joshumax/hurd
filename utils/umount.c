/* Roughly Unix/Linux-compatible `umount' frontend for Hurd translators.

   Copyright (C) 2013 Free Software Foundation, Inc.
   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <argz.h>
#include <error.h>
#include <fcntl.h>
#include <hurd/fshelp.h>
#include <hurd/fsys.h>
#include <hurd/paths.h>
#include <hurd/process.h>
#include <stdlib.h>
#include <unistd.h>

#include "match-options.h"
#include "../sutils/fstab.h"

/* XXX fix libc */
#undef _PATH_MOUNTED
#define _PATH_MOUNTED "/etc/mtab"

static char *targets;
static size_t targets_len;
static int readonly;
static int verbose;
static int active_flags = FS_TRANS_SET;
static int goaway_flags;
static int source_goaway;
static int fake;

static struct fstab_argp_params fstab_params;

#define FAKE_KEY 0x80 /* !isascii (FAKE_KEY), so no short option.  */

static const struct argp_option argp_opts[] =
{
  {NULL, 'd', 0, 0, "Also ask the source translator to go away"},
  {"fake", FAKE_KEY, 0, 0, "Do not actually umount, just pretend"},
  {"force", 'f', 0, 0, "Force umount by killing the translator"},
  {"no-mtab", 'n', 0, 0, "Do not update /etc/mtab"},
  {"read-only", 'r', 0, 0, "If unmounting fails, try to remount read-only"},
  {"nosync", 'S', 0, 0, "Don't sync a translator before killing it"},
  {"test-opts", 'O', "OPTIONS", 0,
   "Only mount fstab entries matching the given set of options"},
  {"verbose", 'v', 0, 0, "Give more detailed information"},
  {},
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct fstab_argp_params *params = state->input;
  error_t err;
  switch (key)
    {
    case ARGP_KEY_INIT:
      state->child_inputs[0] = params; /* pass down to fstab_argp parser */
      break;

    case 'd':
      source_goaway = 1;
      break;

    case FAKE_KEY:
      fake = 1;
      break;

    case 'f':
      goaway_flags |= FSYS_GOAWAY_FORCE;
      break;

    case 'n':
      /* do nothing */
      break;

    case 'r':
      readonly = 1;
      break;

    case 'S':
      goaway_flags |= FSYS_GOAWAY_NOSYNC;
      break;

    case 'O':
      err = argz_create_sep (arg, ',', &test_opts, &test_opts_len);
      if (err)
	argp_failure (state, 100, ENOMEM, "%s", arg);
      break;

    case 'v':
      verbose += 1;
      break;

    case ARGP_KEY_ARG:
      err = argz_add (&targets, &targets_len, arg);
      if (err)
	argp_failure (state, 100, ENOMEM, "%s", arg);
      break;

    case ARGP_KEY_NO_ARGS:
      if (! params->do_all)
	{
	  argp_error (state,
		      "filesystem argument required if --all is not given");
	  return EINVAL;
	}
      break;

    case ARGP_KEY_END:
      if (params->do_all && targets)
	{
	  argp_error (state, "filesystem argument not allowed with --all");
	  return EINVAL;
	}
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static const char doc[] = "Stop active filesystem translators";
static const char args_doc[] = "DEVICE|DIRECTORY [DEVICE|DIRECTORY ...]";

static struct argp fstab_argp_mtab; /* Slightly modified version.  */

static const struct argp_child argp_kids[] =
{
  {&fstab_argp_mtab, 0,
   "Filesystem selection (if no explicit filesystem arguments given):", 2},
  {},
};
static struct argp argp =
{
 options: argp_opts,
 parser: parse_opt,
 args_doc: args_doc,
 doc: doc,
 children: argp_kids,
};

/* This is a trimmed and slightly modified version of
   fstab_argp.options which uses _PATH_MOUNTED instead of _PATH_MNTTAB
   in the doc strings.	*/
static const struct argp_option fstab_argp_mtab_opts[] =
{
  {"all",	 'a', 0,      0, "Do all filesystems in " _PATH_MOUNTED},
  {0,		 'A', 0,      OPTION_ALIAS },
  {"fstab",	 'F', "FILE", 0, "File to use instead of " _PATH_MOUNTED},
  {"fstype",	 't', "TYPE", 0, "Do only filesystems of given type(s)"},
  {"exclude-root",'R',0,      0,
     "Exclude root (/) filesystem from " _PATH_MOUNTED " list"},
  {"exclude",	 'X', "PATTERN", 0, "Exclude directories matching PATTERN"},
  {}
};

static error_t
fstab_argp_mtab_parse_opt (int key, char *arg, struct argp_state *state)
{
  return fstab_argp.parser (key, arg, state);
}

static struct argp fstab_argp_mtab =
{
  options: fstab_argp_mtab_opts,
  parser: fstab_argp_mtab_parse_opt,
};

/* Unmount one filesystem.  */
static error_t
do_umount (struct fs *fs)
{
  error_t err = 0;

  file_t node = file_name_lookup (fs->mntent.mnt_dir, O_NOTRANS, 0666);
  if (node == MACH_PORT_NULL)
    {
      error (0, errno, "%s", fs->mntent.mnt_dir);
      return errno;
    }

  if (verbose)
    printf ("settrans -ag%s%s %s\n",
	    goaway_flags & FSYS_GOAWAY_NOSYNC? "S": "",
	    goaway_flags & FSYS_GOAWAY_FORCE? "f": "",
	    fs->mntent.mnt_dir);

  if (! fake)
    {
      err = file_set_translator (node,
				 0, active_flags, goaway_flags,
				 NULL, 0,
				 MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND);
      if (! err)
	{
	  if (strcmp (fs->mntent.mnt_fsname, "") != 0 &&
	      strcmp (fs->mntent.mnt_fsname, "none") != 0)
	    {
	      if (verbose)
		printf ("settrans -ag%s%s %s\n",
			goaway_flags & FSYS_GOAWAY_NOSYNC? "S": "",
			goaway_flags & FSYS_GOAWAY_FORCE? "f": "",
			fs->mntent.mnt_fsname);

	      file_t source = file_name_lookup (fs->mntent.mnt_fsname,
						O_NOTRANS,
						0666);
	      if (source == MACH_PORT_NULL)
		{
		  error (0, errno, "%s", fs->mntent.mnt_fsname);
		  return errno;
		}

	      err = file_set_translator (source,
					 0, active_flags, goaway_flags,
					 NULL, 0,
					 MACH_PORT_NULL,
					 MACH_MSG_TYPE_COPY_SEND);
	      if (!(goaway_flags & FSYS_GOAWAY_FORCE))
		err = 0;
	      if (err)
		error (0, err, "%s", fs->mntent.mnt_fsname);

	      mach_port_deallocate (mach_task_self (), source);

	    }
	}
      else
	{
	  error (0, err, "%s", fs->mntent.mnt_dir);

	  /* Try remounting readonly instead if requested.  */
	  if (readonly)
	    {
	      if (verbose)
		printf ("fsysopts %s --readonly\n", fs->mntent.mnt_dir);

	      error_t e = fs_set_readonly (fs, TRUE);
	      if (e)
		error (0, e, "%s", fs->mntent.mnt_dir);
	    }
	}
    }

  /* Deallocate the reference so that unmounting nested translators
     works properly.  */
  mach_port_deallocate (mach_task_self (), node);
  return err;
}

int
main (int argc, char **argv)
{
  error_t err;

  err = argp_parse (&argp, argc, argv, 0, 0, &fstab_params);
  if (err)
    error (3, err, "parsing arguments");

  /* Read the mtab file by default.  */
  if (! fstab_params.fstab_path)
    fstab_params.fstab_path = _PATH_MOUNTED;

  struct fstab *fstab = fstab_argp_create (&fstab_params, NULL, 0);
  if (! fstab)
    error (3, ENOMEM, "fstab creation");

  if (targets)
    for (char *t = targets; t; t = argz_next (targets, targets_len, t))
      {
	/* Figure out if t is the device or the mountpoint.  */
	struct fs *fs = fstab_find_mount (fstab, t);
	if (! fs)
	  {
	    fs = fstab_find_device (fstab, t);
	    if (! fs)
	      {
		/* As last resort, just assume it is the mountpoint.  */
		struct mntent m =
		  {
		    mnt_fsname: "",
		    mnt_dir: t,
		    mnt_type: "",
		    mnt_opts: 0,
		    mnt_freq: 0,
		    mnt_passno: 0,
		  };

		err = fstab_add_mntent (fstab, &m, &fs);
		if (err)
		  error (2, err, "could not find entry for: %s", t);
	      }
	  }

	if (fs)
	  err |= do_umount (fs);
      }
  else
    {
      /* Sort entries in reverse lexicographical order so that the
	 longest mount points are unmounted first.  This makes sure
	 that nested mounts are handled properly.  */
      size_t count = 0;
      for (struct fs *fs = fstab->entries; fs; fs = fs->next)
	count += 1;

      char **entries = malloc (count * sizeof (char *));
      if (! entries)
	error (3, ENOMEM, "allocating entries array");

      char **p = entries;
      for (struct fs *fs = fstab->entries; fs; fs = fs->next)
	*p++ = fs->mntent.mnt_dir;

      /* Reverse lexicographical order.	 */
      int compare_entries (const void *a, const void *b)
	{
	  return -strcmp ((char *) a, (char *) b);
	}

      qsort (entries, count, sizeof (char *), compare_entries);

      for (int i = 0; i < count; i++)
	{
	  struct fs *fs = fstab_find_mount (fstab, entries[i]);
	  if (! fs)
	    error (4, 0, "could not find entry for: %s", entries[i]);

	  if (! match_options (&fs->mntent))
	    continue;

	  err |= do_umount (fs);
	}
    }

  return err? EXIT_FAILURE: EXIT_SUCCESS;
}
