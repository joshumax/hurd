/*
 * Copyright (C) 2020 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <argp.h>
#include <version.h>

#include "libmachdev/machdev.h"
#include "libshouldbeinlibc/wire.h"
#include "block-rump.h"
#include <pthread.h>
#include <mach.h>

mach_port_t bootstrap_resume_task = MACH_PORT_NULL;

static const struct argp_option options[] = {
  {"host-priv-port",	'h', "PORT", 0, "Host private port PORT"},
  {"device-master-port",'d', "PORT", 0, "Device master port PORT"},
  {"next-task",		'N', "TASK", 0, "Next bootstrap task TASK"},
  {0}
};


/* Parse a command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  /* We save our parsed values in this structure, hung off STATE->hook.
     Only after parsing all options successfully will we use these values.  */
  struct
  {
    int host_priv;
    int dev_master;
    int next_task;
  } *values = state->hook;

  switch (key)
    {
    case 'h':
      values->host_priv = atoi(arg);
      break;
    case 'd':
      values->dev_master = atoi(arg);
      break;
    case 'N':
      values->next_task = atoi(arg);
      break;

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
      values = malloc (sizeof *values);
      if (values == 0)
        return ENOMEM;
      state->hook = values;
      memset (values, 0, sizeof *values);
      break;

    case ARGP_KEY_SUCCESS:
      /* All options parsed successfully */
      _hurd_host_priv = values->host_priv;
      _hurd_device_master = values->dev_master;
      bootstrap_resume_task = values->next_task;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp_child empty_argp_children[] = {{0}};
static struct argp rumpdisk_argp = {options, parse_opt, 0, 0, empty_argp_children};
static const struct argp *rumpdisk_argp_bootup = &rumpdisk_argp;

int
main (int argc, char **argv)
{
  mach_port_t bootstrap = MACH_PORT_NULL;
  int err;
  pthread_t t;

  setenv ("RUMP_NCPU", "1", 1);
  setenv ("RUMP_VERBOSE", "1", 1);
  setenv ("RUMP_HOSTNAME", "HURD0", 1);
  setenv ("HOSTNAME", "HURD0", 1);
  setenv ("RUMP_PANIC", "1", 1);

  err = argp_parse (rumpdisk_argp_bootup, argc, argv, 0, 0, NULL);
  if (err)
    {
      error(1, err, "Missing parameters for bootstrap");
    }

  rump_register_block ();
  machdev_trivfs_init (argc, argv, bootstrap_resume_task, "rumpdisk", "/dev/rumpdisk", &bootstrap);

  /* Make sure we will not swap out, in case we drive the disk used for
     swapping.  */
  err = wire_task_self ();
  if (err)
    error (1, errno, "cannot lock all memory");

  machdev_device_init ();
  err = pthread_create (&t, NULL, machdev_server, NULL);
  if (err)
    return err;
  pthread_detach (t);
  machdev_trivfs_server (bootstrap);
  return 0;
}
