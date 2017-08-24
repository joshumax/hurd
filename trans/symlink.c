/* Translator for S_IFLNK nodes
   Copyright (C) 1994, 2000, 2001, 2002 Free Software Foundation

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <hurd/fsys.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <version.h>
#include "fsys_S.h"

mach_port_t realnode;

/* We return this for O_NOLINK lookups */
mach_port_t realnodenoauth;

/* We return this for non O_NOLINK lookups */
char *linktarget;

extern int fsys_server (mach_msg_header_t *, mach_msg_header_t *);

const char *argp_program_version = STANDARD_HURD_VERSION (symlink);

static const struct argp_option options[] =
  {
    { 0 }
  };

static const char args_doc[] = "TARGET";
static const char doc[] = "A translator for symlinks."
"\vA symlink is an alias for another node in the filesystem."
"\n"
"\nA symbolic link refers to its target `by name', and contains no actual"
" reference to the target.  The target referenced by the symlink is"
" looked up in the namespace of the client.";

/* Parse a single option/argument.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  if (key == ARGP_KEY_ARG && state->arg_num == 0)
    linktarget = arg;
  else if (key == ARGP_KEY_ARG || key == ARGP_KEY_NO_ARGS)
    argp_usage (state);
  else
    return ARGP_ERR_UNKNOWN;
  return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };


int
main (int argc, char **argv)
{
  mach_port_t bootstrap;
  mach_port_t control;
  error_t err;

  /* Parse our options...  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  linktarget = argv[1];

  /* Reply to our parent */
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &control);
  mach_port_insert_right (mach_task_self (), control, control,
			  MACH_MSG_TYPE_MAKE_SEND);
  err =
    fsys_startup (bootstrap, 0, control, MACH_MSG_TYPE_COPY_SEND, &realnode);
  mach_port_deallocate (mach_task_self (), control);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (1, err, "Starting up translator");

  io_restrict_auth (realnode, &realnodenoauth, 0, 0, 0, 0);
  mach_port_deallocate (mach_task_self (), realnode);

  /* Mark us as important.  */
  mach_port_t proc = getproc ();
  if (proc == MACH_PORT_NULL)
    error (2, err, "cannot get a handle to our process");

  err = proc_mark_important (proc);
  /* This might fail due to permissions or because the old proc server
     is still running, ignore any such errors.  */
  if (err && err != EPERM && err != EMIG_BAD_ID)
    error (2, err, "Cannot mark us as important");

  mach_port_deallocate (mach_task_self (), proc);

  /* Launch */
  while (1)
    {
      /* The timeout here is 10 minutes */
      err = mach_msg_server_timeout (fsys_server, 0, control,
				     MACH_RCV_TIMEOUT, 1000 * 60 * 10);
      if (err == MACH_RCV_TIMED_OUT)
	exit (0);
    }
}

error_t
S_fsys_getroot (mach_port_t fsys_t,
		mach_port_t dotdotnode,
		uid_t *uids, size_t nuids,
		uid_t *gids, size_t ngids,
		int flags,
		retry_type *do_retry,
		char *retry_name,
		mach_port_t *ret,
		mach_msg_type_name_t *rettype)
{
  if (flags & O_NOLINK)
    {
      /* Return our underlying node. */
      *ret = realnodenoauth;
      *rettype = MACH_MSG_TYPE_COPY_SEND;
      *do_retry = FS_RETRY_REAUTH;
      retry_name[0] = '\0';
      return 0;
    }
  else
    {
      /* Return telling the user to follow the link */
      strcpy (retry_name, linktarget);
      if (linktarget[0] == '/')
	{
	  *do_retry = FS_RETRY_MAGICAL;
	  *ret = MACH_PORT_NULL;
	  *rettype = MACH_MSG_TYPE_COPY_SEND;
	}
      else
	{
	  *do_retry = FS_RETRY_REAUTH;
	  *ret = dotdotnode;
	  *rettype = MACH_MSG_TYPE_MOVE_SEND;
	}
    }
  return 0;
}

error_t
S_fsys_goaway (mach_port_t control, int flags)
{
  exit (0);
}

error_t
S_fsys_syncfs (mach_port_t control,
	       int wait,
	       int recurse)
{
  return 0;
}
