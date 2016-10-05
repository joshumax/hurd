/* Translate message ids to symbolic names.

   Copyright (C) 1998-2015 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <argz.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <fnmatch.h>
#include <hurd/ihash.h>
#include <mach.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msgids.h"

static void
msgid_ihash_cleanup (void *element, void *arg)
{
  struct msgid_info *info = element;
  free (info->name);
  free (info->subsystem);
  free (info);
}

static struct hurd_ihash msgid_ihash
  = HURD_IHASH_INITIALIZER (HURD_IHASH_NO_LOCP);

/* Parse a file of RPC names and message IDs as output by mig's -list
   option: "subsystem base-id routine n request-id reply-id".  Put each
   request-id value into `msgid_ihash' with the routine name as its value.  */
static error_t
parse_msgid_list (const char *filename)
{
  FILE *fp;
  char *buffer = NULL;
  size_t bufsize = 0;
  unsigned int lineno = 0;
  char *name, *subsystem;
  unsigned int msgid;
  error_t err;

  fp = fopen (filename, "r");
  if (fp == NULL)
    return errno;

  while (getline (&buffer, &bufsize, fp) > 0)
    {
      ++lineno;
      if (buffer[0] == '#' || buffer[0] == '\0')
	continue;
      if (sscanf (buffer, "%ms %*u %ms %*u %u %*u\n",
		  &subsystem, &name, &msgid) != 3)
	error (0, 0, "%s:%u: invalid format in RPC list file",
	       filename, lineno);
      else
	{
	  struct msgid_info *info = malloc (sizeof *info);
	  if (info == 0)
	    error (1, errno, "malloc");
	  info->name = name;
	  info->subsystem = subsystem;
	  err = hurd_ihash_add (&msgid_ihash, msgid, info);
	  if (err)
	    return err;
	}
    }

  free (buffer);
  fclose (fp);
  return 0;
}

/* Look for a name describing MSGID.  We check the table directly, and
   also check if this looks like the ID of a reply message whose request
   ID is already in the table.  */
const struct msgid_info *
msgid_info (mach_msg_id_t msgid)
{
  const struct msgid_info *info = hurd_ihash_find (&msgid_ihash, msgid);
  if (info == 0 && (msgid / 100) % 2 == 1)
    {
      /* This message ID is not in the table, and its number makes it
	 what should be an RPC reply message ID.  So look up the message
	 ID of the corresponding RPC request and synthesize a name from
	 that.  Then stash that name in the table so the next time the
	 lookup will match directly.  */
      info = hurd_ihash_find (&msgid_ihash, msgid - 100);
      if (info != 0)
	{
	  struct msgid_info *reply_info = malloc (sizeof *info);
	  if (reply_info != 0)
	    {
	      reply_info->subsystem = strdup (info->subsystem);
	      reply_info->name = 0;
	      asprintf (&reply_info->name, "%s-reply", info->name);
	      hurd_ihash_add (&msgid_ihash, msgid, reply_info);
	      info = reply_info;
	    }
	  else
	    info = 0;
	}
    }
  return info;
}

static int
msgids_file_p (const struct dirent *eps)
{
  return fnmatch ("*.msgids", eps->d_name, 0) != FNM_NOMATCH;
}

static void
scan_msgids_dir (char **argz, size_t *argz_len, char *dir, bool append)
{
  struct dirent **eps;
  int n;

  n = scandir (dir, &eps, msgids_file_p, NULL);
  if (n >= 0)
    {
      for (int cnt = 0; cnt < n; ++cnt)
	{
	  char *msgids_file;

	  if (asprintf (&msgids_file, "%s/%s", dir, eps[cnt]->d_name) < 0)
	    error (1, errno, "asprintf");

	  if (append == TRUE)
	    {
	      if (argz_add (argz, argz_len, msgids_file) != 0)
		error (1, errno, "argz_add");
	    }
	  else
	    {
	      if (argz_insert (argz, argz_len, *argz, msgids_file) != 0)
		error (1, errno, "argz_insert");
	    }
	  free (msgids_file);
	}
    }

  /* If the directory couldn't be scanned for whatever reason, just ignore
     it. */
}

/* Argument parsing.  */

static char *msgids_files_argz = NULL;
static size_t msgids_files_argz_len = 0;
static bool nostdinc = FALSE;

#define STD_MSGIDS_DIR DATADIR "/msgids/"
#define OPT_NOSTDINC -1

static const struct argp_option options[] =
{
  {"nostdinc", OPT_NOSTDINC, 0, 0,
   "Do not search inside the standard system directory, `" STD_MSGIDS_DIR
   "', for `.msgids' files.", 0},
  {"rpc-list", 'i', "FILE", 0,
   "Read FILE for assocations of message ID numbers to names.", 0},
  {0, 'I', "DIR", 0,
   "Add the directory DIR to the list of directories to be searched for files "
   "containing message ID numbers.", 0},
  {0}
};

/* Parse our options...  */
static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ARGP_KEY_INIT:
      hurd_ihash_set_cleanup (&msgid_ihash, msgid_ihash_cleanup, 0);
      break;

    case OPT_NOSTDINC:
      nostdinc = TRUE;
      break;

    case 'i':
      if (argz_add (&msgids_files_argz, &msgids_files_argz_len,
		    arg) != 0)
	{
	  argp_failure (state, 1, errno, "argz_add");
	  return errno;
	}
      break;

    case 'I':
      scan_msgids_dir (&msgids_files_argz, &msgids_files_argz_len,
		       arg, TRUE);
      break;

    case ARGP_KEY_NO_ARGS:
      return 0;

    case ARGP_KEY_ARG:
      return EINVAL;

    case ARGP_KEY_END:
      /* Insert the files from STD_MSGIDS_DIR at the beginning of the
	 list, so that their content can be overridden by subsequently
	 parsed files.  */
      if (nostdinc == FALSE)
	scan_msgids_dir (&msgids_files_argz, &msgids_files_argz_len,
			 STD_MSGIDS_DIR, FALSE);

      if (msgids_files_argz != NULL)
	{
	  error_t err = 0;
	  char *msgids_file = NULL;

	  while (! err
		 && (msgids_file = argz_next (msgids_files_argz,
					      msgids_files_argz_len,
					      msgids_file)))
	    err = parse_msgid_list (msgids_file);

	  free (msgids_files_argz);
	  if (err)
	    argp_failure (state, 1, err, "%s", msgids_file);
	}
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

const struct argp msgid_argp = {
  .options = options,
  .parser = parse_opt,
  .doc = "msgid doc",
};
