/* remap -- a translator for changing paths
   Copyright (C) 2013 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <hurd/trivfs.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <error.h>
#include <string.h>

#include <version.h>

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = 0;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  /* Don't care */  
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  exit (0);
}

struct remap
{
  char *from;
  char *to;
  struct remap *next;
};

static struct remap *remaps;

error_t
trivfs_S_dir_lookup (struct trivfs_protid *diruser,
		     mach_port_t reply, mach_msg_type_name_t reply_type,
		     char *filename,
		     int flags,
		     mode_t mode,
		     retry_type *do_retry,
		     char *retry_name,
		     mach_port_t *retry_port,
		     mach_msg_type_name_t *retry_port_type)
{
  struct remap *remap;
  string_t dest = { };
  size_t prefix_size;

  if (!diruser)
    return EOPNOTSUPP;

  for (remap = remaps; remap; remap = remap->next)
    {
      prefix_size = strlen (remap->from);
      if (!strncmp (remap->from, filename, prefix_size)
	  && (filename[prefix_size] == '\0' || filename[prefix_size] == '/'))
	{
	  snprintf (dest, sizeof (dest), "%s%s", remap->to,
		    filename + prefix_size);

#ifdef DEBUG
	  fprintf (stderr, "replacing %s with %s\n", filename, dest);
	  fflush (stderr);
#endif

	  filename = dest;
	  break;
	}
    }

  *do_retry = FS_RETRY_REAUTH;
  *retry_port = getcrdir ();
  *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
  strcpy (retry_name, filename);

  return 0;
}

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  static char *remap_from;

  switch (key)
  {
    case ARGP_KEY_ARG:
      if (arg[0] != '/')
	{
	  argp_error (state, "remap only works with absolute paths: %s",
		      arg);
	  return EINVAL;
	}

      /* Skip heading slashes */
      while (arg[0] == '/')
        arg++;

      if (!remap_from)
	/* First of a pair */
	remap_from = strdup (arg);
      else
	{
	  /* Second of a pair */
	  struct remap *remap = malloc (sizeof (*remap));
	  remap->from = remap_from;
	  remap->to = strdup (arg);
	  remap->next = remaps;
#ifdef DEBUG
	  fprintf (stderr, "adding remap %s->%s\n", remap->from, remap->to);
#endif
	  remaps = remap;
	  remap_from = NULL;
	}

      break;
  }
  return 0;
}

const char *argp_program_version = STANDARD_HURD_VERSION (fakeroot);

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  struct argp argp = { NULL, parse_opt, "[ FROM1 TO1 [ FROM2 TO2 [ ... ] ] ]", "\
A translator for remapping directories.\v\
This translator is to be used as a chroot, within which paths point to the\
same files as the original root, except a given set of paths, which are\
remapped to given paths." };

  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  struct trivfs_control *fsys;

  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  if (err)
    error (1, err, "trivfs_startup failed");
  ports_manage_port_operations_one_thread (fsys->pi.bucket, trivfs_demuxer, 0);

  /*NOTREACHED*/
  return 0;
}
