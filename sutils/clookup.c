/* Careful filename lookup

   Copyright (C) 1996, 1998, 1999, 2000 Free Software Foundation, Inc.

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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <hurd.h>
#include <hurd/lookup.h>
#include <hurd/id.h>
#include <hurd/fsys.h>


/* This function is like file_name_lookup, but tries hard to avoid starting
   any passive translators.  If a node with an unstarted passive translator
   is encountered, ENXIO is returned in ERRNO; other errors are as for
   file_name_lookup.  Note that checking for an active translator currently
   requires fetching the control port, which is a privileged operation.  */
file_t
file_name_lookup_carefully (const char *name, int flags, mode_t mode)
{
  error_t err;
  file_t node;
  uid_t *uids;			/* Authentication of the current process.  */
  gid_t *gids;
  size_t num_uids, num_gids;

  /* Do the actual directory lookup.  We only do the first pathname element
     of NAME, appending the rest to any RETRY_NAME returned.  We then make
     sure the result node doesn't have a passive translator with no active
     translator started (but we make an exception for symlinks) -- if it
     does, we just return ENXIO.  */
  error_t lookup (file_t dir, const char *name, int flags, mode_t mode,
		  retry_type *retry, string_t retry_name,
		  mach_port_t *node)
    {
      error_t err;
      const char *head, *tail;
      char *slash = index (name, '/');

      if (slash)
	{
	  *stpncpy (head = alloca (slash - name + 1), name, slash - name) = 0;
	  tail = slash + 1;
	}
      else
	{
	  head = name;
	  tail = 0;
	}

      err = dir_lookup (dir, head, flags | O_NOTRANS, mode,
			retry, retry_name, node);
      if (err)
	return err;

      if (*node != MACH_PORT_NULL
	  && (!(flags & O_NOTRANS) || tail || *retry_name))
	/* The dir_lookup has returned a node to use for the next stage of
	   the lookup.  Unless it's the last element of the path and FLAGS
	   has O_NOTRANS set (in which case we just return what we got as
	   is), we have to simulate the above lookup being done without
	   O_NOTRANS.  Do this being careful not to start any translators.  */
	{
	  /* See if there's an active translator.  */
	  fsys_t fsys;	/* Active translator control port.  */

	  err = file_get_translator_cntl (*node, &fsys);
	  if (! err)
	    /* There is!  Get its root node to use as the actual file.  */
	    {
	      file_t unauth_dir; /* DIR unauthenticated.  */
	      err = io_restrict_auth (dir, &unauth_dir, 0, 0, 0, 0);
	      if (! err)
	        {
	          file_t old_node = *node;
	          err = fsys_getroot (fsys,
	    			  unauth_dir, MACH_MSG_TYPE_COPY_SEND,
	    			  uids, num_uids, gids, num_gids,
	    			  flags & ~O_NOTRANS, retry,
	    			  retry_name, node);
	          mach_port_deallocate (mach_task_self (), unauth_dir);
	          if (! err)
	    	mach_port_deallocate (mach_task_self (), old_node);
	        }
	      mach_port_deallocate (mach_task_self (), fsys);
	    }

	  if (!err && tail)
	    /* Append TAIL to RETRY_NAME.  */
	    {
	      size_t rtn_len = strlen (retry_name);
	      if (rtn_len + 1 + strlen (tail) + 1 > sizeof (string_t))
		err = ENAMETOOLONG; /* Argh.  Lovely string_t. */
	      else
		{
		  if (rtn_len > 0 && retry_name[rtn_len - 1] != '/')
		    retry_name[rtn_len++] = '/';
		  strcpy (retry_name + rtn_len, tail);
		}
	    }

	  if (err)
	    mach_port_deallocate (mach_task_self (), *node);
	}

      return err;
    }

  /* Fetch uids for use with fsys_getroot.  */
  num_uids = geteuids (0, 0);
  if (num_uids < 0)
    return errno;
  uids = alloca (num_uids * sizeof (uid_t));
  num_uids = geteuids (num_uids, uids);
  if (num_uids < 0)
    return errno;

  /* ... and gids.  */
  num_gids = getgroups (0, 0);
  if (num_gids < 0)
    return errno;
  gids = alloca (num_gids * sizeof (gid_t));
  num_gids = getgroups (num_gids, gids);
  if (num_gids < 0)
    return errno;

  /* Look things up ...  */
  err = hurd_file_name_lookup (&_hurd_ports_use, &getdport, lookup,
			       name, flags, mode & ~getumask (),
			       &node);

  return err ? (__hurd_fail (err), MACH_PORT_NULL) : node;
}
