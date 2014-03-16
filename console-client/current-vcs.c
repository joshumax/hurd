/* current-vcs.c -- Add a "current vcs" symlink to the cons node.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Samuel Thibault.
   
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

#include <argp.h>
#include <driver.h>
#include <input.h>
#include <stdio.h>
#include <error.h>
#include <sys/mman.h>

#include "trans.h"


/* We use the same infrastructure as the kbd and mouse repeaters.  */

/* The default name of the node of the repeater.  */
#define DEFAULT_REPEATER_NODE "vcs"

/* The name of the repeater node.  */
static char *repeater_node = DEFAULT_REPEATER_NODE;

/* The repeater node.  */
static consnode_t vcs_node;


/* Callbacks for vcs console node.  */

/* Reading the link to get current vcs, returns length of path (trailing \0
   excluded).  */
static error_t
vcs_readlink (struct iouser *user, struct node *np, char *buf)
{
  int cur;
  error_t ret = 0;

  ret = console_current_id (&cur);
  if (!ret)
    {
      if (!buf)
	ret = snprintf (NULL, 0, "%s/%d", cons_file, cur);
      else
	ret = sprintf (buf, "%s/%d", cons_file, cur);

      if (ret < 0)
	ret = -errno;
    }
  else
    ret = -ret;
  return ret;
}

static error_t
vcs_read (struct protid *user, char **data,
	  mach_msg_type_number_t * datalen, off_t offset,
	  mach_msg_type_number_t amount)
{
  int err;
  int size;
  char *buf;

  if (amount > 0)
    {
      size = vcs_readlink (user->user, NULL, NULL);
      if (size < 0)
	return -size;

      buf = alloca (size);

      err = vcs_readlink (user->user, NULL, buf);

      if (err < 0)
	return -err;

      if (offset + amount > size)
	amount = size - offset;
      if (amount < 0)
	amount = 0;

      if (*datalen < amount)
	{
	  *data = mmap (0, amount, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    return ENOMEM;
	}

      memcpy (*data, buf + offset, amount);
      *datalen = amount;
    }
  return 0;
}

/* Making a link to set current vcs.
   Relative values perform relative switches.  */
static error_t
vcs_mksymlink (struct iouser *user, struct node *np, char *name)
{
  char *c, *d;
  int vt, delta = 0;

  c = strrchr (name, '/');
  if (!c)
    c = name;
  else
    c++;
  if (!*c)
    return EINVAL;

  if (*c == '-' || *c == '+')
    delta = 1;

  vt = strtol (c, &d, 10);
  if (*d)
    /* Bad number.  */
    return EINVAL;

  if (!vt)
    return 0;
  return console_switch (delta ? 0 : vt, delta ? vt : 0);
}


static const char doc[] = "Current VCS Driver";

static const struct argp_option options[] = 
  {
    { "repeater",	'r', "NODE", OPTION_ARG_OPTIONAL,
      "Set a current vcs translator on NODE (default: " DEFAULT_REPEATER_NODE ")"},
    { 0 }
  };

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  int *pos = (int *) state->input;

  switch (key)
    {
    case 'r':
      repeater_node = arg ? arg: DEFAULT_REPEATER_NODE;
      break;

    case ARGP_KEY_END:
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  *pos = state->next;
  return 0;
}

static struct argp argp = {options, parse_opt, 0, doc};

/* Initialize the current VCS driver.  */
static error_t
current_vcs_init (void **handle, int no_exit, int argc, char *argv[], int *next)
{
  error_t err;
  int pos = 1;

  /* Parse the arguments.  */
  err = argp_parse (&argp, argc, argv, ARGP_IN_ORDER | ARGP_NO_EXIT
		    | ARGP_SILENT, 0, &pos);
  *next += pos - 1;

  if (err && err != EINVAL)
    return err;

  return 0;
}

static error_t
current_vcs_start (void *handle)
{
  error_t err;
  
  err = console_create_consnode (repeater_node, &vcs_node);
  if (err)
    return err;

  vcs_node->read = vcs_read;
  vcs_node->write = NULL;
  vcs_node->select = NULL;
  vcs_node->open = NULL;
  vcs_node->close = NULL;
  vcs_node->demuxer = NULL;
  vcs_node->readlink = vcs_readlink;
  vcs_node->mksymlink = vcs_mksymlink;
  console_register_consnode (vcs_node);

  return 0;
}

static error_t
current_vcs_fini (void *handle, int force)
{
  console_unregister_consnode (vcs_node);
  console_destroy_consnode (vcs_node);
  return 0;
}


struct driver_ops driver_current_vcs_ops =
  {
    current_vcs_init,
    current_vcs_start,
    current_vcs_fini
  };
