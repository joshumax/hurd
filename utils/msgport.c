/* Send messages to selected processes

   Copyright (C) 1998 Free Software Foundation, Inc.

   Written by Jose M. Moya <josem@gnu.org>

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA. */

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <argp.h>
#include <error.h>
#include <version.h>
#include "pids.h"

char *
_hurd_canonicalize_directory_name_internal (file_t thisdir,
                                            char *buf,
                                            size_t size);

const char *argp_program_version = STANDARD_HURD_VERSION (msgport);


static const struct argp_option options[] =
{
  {0, 0}
};

static const char doc[] =
"Send messages to selected processes";

static const char args_doc[] = 
"";



typedef error_t (*cmd_func_t) (pid_t pid, mach_port_t msgport, 
			       int argc, char *argv[]);

typedef struct cmd {
  cmd_func_t f;
  char **args;
  size_t num_args;
} cmd_t;

struct cmds_argp_params
{
  cmd_t **cmds;
  size_t *num_cmds;
};




static error_t 
cmd_getenv (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  char buf[1024], *data = buf;
  mach_msg_type_number_t len = sizeof (buf);
  if (argc)
    {
      if ((err = msg_get_env_variable (msgport, argv[0], &data, &len)))
	return err;
      printf ("%d: %s=%s\n", pid, argv[0], data);
    }
  else
    {
      char *p;
      if ((err = msg_get_environment (msgport, &data, &len)))
	return err;
      for (p=data; p < data + len; p = strchr (p, '\0') + 1)
	printf ("%d: %s\n", pid, p);
    }
  if (data != buf)
    vm_deallocate (mach_task_self (), (vm_address_t)data, len);
  return err;
}
  
static error_t 
cmd_setenv (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  task_t task;
  process_t proc = getproc ();

  err = proc_pid2task (proc, pid, &task);
  if (err)
    return err;
  err = msg_set_env_variable (msgport, task, argv[0], argv[1], 1);
  return err;
}

static error_t 
cmd_clearenv (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  task_t task;
  process_t proc = getproc ();

  err = proc_pid2task (proc, pid, &task);
  if (err)
    return err;
  err = msg_set_environment (msgport, task, 0, 0);
  return err;
}

static int
str2flags (char *str)
{
  int flags = 0;
  while (*str)
    {
      switch (*str)
	{
	case 'r': flags |= O_RDONLY; break;
	case 'w': flags |= O_WRONLY; break;
	case 'x': flags |= O_EXEC;   break;
	default:
	  /* ignore */
	}
      ++str;
    }
  return flags;
}

static error_t 
do_setfd (pid_t pid, mach_port_t msgport, size_t fd, file_t file)
{
  error_t err;
  task_t task;
  process_t proc = getproc ();

  err = proc_pid2task (proc, pid, &task);
  if (err)
    return err;
  err = msg_set_fd (msgport, task, fd, file, MACH_MSG_TYPE_MOVE_SEND);
  return err;
}

static error_t 
cmd_setfd (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  int flags = O_RDONLY;
  file_t file;

  if (argc > 2)
    flags = str2flags(argv[2]);
  if ((file = file_name_lookup (argv[1], flags, 0666)) == 
      MACH_PORT_NULL)
    return errno;
  return do_setfd (pid, msgport, atoi (argv[0]), file);
}

static error_t 
cmd_stdin (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  int flags = O_RDONLY;
  file_t file;

  if (argc > 1)
    flags = str2flags(argv[1]);
  if ((file = file_name_lookup (argv[0], flags, 0666)) == 
      MACH_PORT_NULL)
    return errno;
  return do_setfd (pid, msgport, 0, file);
}

static error_t 
cmd_stdout (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  int flags = O_WRONLY;
  file_t file;

  if (argc > 1)
    flags = str2flags(argv[1]);
  if ((file = file_name_lookup (argv[0], flags, 0666)) == 
      MACH_PORT_NULL)
    return errno;
  return do_setfd (pid, msgport, 1, file);
}

static error_t 
cmd_stderr (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  int flags = O_WRONLY;
  file_t file;

  if (argc > 1)
    flags = str2flags(argv[1]);
  if ((file = file_name_lookup (argv[0], flags, 0666)) == 
      MACH_PORT_NULL)
    return errno;
  return do_setfd (pid, msgport, 2, file);
}

static error_t 
cmd_chcwdir (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  file_t dir;
  task_t task;
  process_t proc = getproc ();

  if ((dir = file_name_lookup (argv[0], 0, 0)) == MACH_PORT_NULL)
    return errno;
  if ((err = proc_pid2task (proc, pid, &task)))
    return err;
  if ((err = msg_set_init_port (msgport, task, INIT_PORT_CWDIR, dir, 
				MACH_MSG_TYPE_MOVE_SEND)))
    return err;
  return 0;
}

static error_t 
cmd_cdroot (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  file_t dir;
  task_t task;
  process_t proc = getproc ();

  if ((err = proc_pid2task (proc, pid, &task)))
    return err;
  if ((err = msg_get_init_port (msgport, task, INIT_PORT_CRDIR, &dir)))
    return err;
  if ((err = msg_set_init_port (msgport, task, INIT_PORT_CWDIR, dir, 
				MACH_MSG_TYPE_MOVE_SEND)))
    return err;
  return 0;
}

static error_t 
cmd_chcrdir (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  file_t dir;
  task_t task;
  process_t proc = getproc ();

  if ((dir = file_name_lookup (argv[0], 0, 0)) == MACH_PORT_NULL)
    return errno;
  if ((err = proc_pid2task (proc, pid, &task)))
    return err;
  if ((err = msg_set_init_port (msgport, task, INIT_PORT_CRDIR, dir, 
				MACH_MSG_TYPE_MOVE_SEND)))
    return err;
  return 0;
}

static error_t 
cmd_pwd (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  file_t dir;
  task_t task;
  process_t proc = getproc ();

  if ((err = proc_pid2task (proc, pid, &task)))
    return err;
  if ((err = msg_get_init_port (msgport, task, INIT_PORT_CWDIR, &dir)))
    return err;
  printf ("%d: %s\n", pid,
	  _hurd_canonicalize_directory_name_internal(dir, NULL, 0));
  return 0;
}

static error_t 
cmd_getroot (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  file_t dir;
  task_t task;
  process_t proc = getproc ();

  if ((err = proc_pid2task (proc, pid, &task)))
    return err;
  if ((err = msg_get_init_port (msgport, task, INIT_PORT_CRDIR, &dir)))
    return err;
  printf ("%d: %s\n", pid,
	  _hurd_canonicalize_directory_name_internal(dir, NULL, 0));
  return 0;
}

static error_t 
cmd_umask (pid_t pid, mach_port_t msgport, int argc, char *argv[])
{
  error_t err;
  mode_t umask;
  task_t task;
  process_t proc = getproc ();

  if ((err = proc_pid2task (proc, pid, &task)))
    return err;
  if (argc)
    {
      umask = strtol(argv[0], 0, 8);
      if ((err = msg_set_init_int (msgport, task, INIT_UMASK, umask)))
	return err;
    }
  else
    {
      if ((err = msg_get_init_int (msgport, task, INIT_UMASK, &umask)))
	return err;
      printf ("%d: %03o\n", pid, umask);
    }
  return 0;
}


#define OA OPTION_ARG_OPTIONAL

#define CMD_GETENV	1000
#define CMD_SETENV	1001
#define CMD_CLRENV	1002
#define CMD_CHCWDIR	1003
#define CMD_CHCRDIR	1004
#define CMD_CDROOT	1005
#define CMD_UMASK	1006
#define CMD_SETFD      	1007
#define CMD_STDIN	1008
#define CMD_STDOUT	1009
#define CMD_STDERR	1010
#define CMD_PWD		1011
#define CMD_GETROOT	1012

static const struct argp_option cmd_options[] =
{
  {"getenv",   CMD_GETENV, "VAR",            OA, "Get environment variable"},
  {"printenv", 0,	   0,     OPTION_ALIAS},
  {"setenv",   CMD_SETENV, "VAR VALUE",       0, "Set environment variable"},
  {"clearenv", CMD_CLRENV, 0,                 0, "Clear environment"},
  {"pwd",      CMD_PWD,    0,     0, "Print current working directory"},
  {"getcwd",   0,	   0,     OPTION_ALIAS},
  {"getroot",  CMD_GETROOT,0,     0, "Print current root directory"},
  {"setfd",    CMD_SETFD,  "FD FILE [+rwxn]", 0, "Change file descriptor"},
  {"stdin",    CMD_STDIN,  "FILE [+rwxn]",    0, "Change standard input"},
  {"stdout",   CMD_STDOUT, "FILE [+rwxn]",    0, "Change standard output"},
  {"stderr",   CMD_STDERR, "FILE [+rwxn]",    0, "Change standard error"},
  {"chdir",    CMD_CHCWDIR,"DIR",   0, "Change current working directory"},
  {"cd",       0,	   0,     OPTION_ALIAS},
  {"chroot",   CMD_CHCRDIR,"DIR",   0, "Change current root directory"},
  {"cdroot",   CMD_CDROOT, 0,       0, "Change cwd to root directory"},
  {"umask",    CMD_UMASK,  "MASK", OA, "Change umask"},
  {0, 0}
};

static error_t 
cmd_add (cmd_func_t func, size_t minargs, size_t maxargs, 
	 char *arg, struct argp_state *state)
{
  cmd_t *cmd;
  size_t i = 0;

  struct cmds_argp_params *params = state->input;
  size_t num_cmds = *params->num_cmds + 1;
  cmd_t *cmds = realloc (*params->cmds, num_cmds * sizeof(cmd_t));

  *params->cmds = cmds;
  *params->num_cmds = num_cmds;

  cmd = &cmds[num_cmds-1];
  cmd->f = func;
  cmd->args = 0;
  if (maxargs)
    {
      cmd->args = malloc (maxargs * sizeof (char *));
      if (arg)
	cmd->args[i++] = arg;
      while (i < maxargs && 
	     state->argv[state->next] && 
	     state->argv[state->next][0] != '-')
	  cmd->args[i++] = state->argv[state->next++];
      if (i < minargs || i > maxargs)
	argp_usage(state);
    }
  cmd->num_args = i;
  return 0;
}

static error_t 
cmd_parse_opt (int key, char *arg, struct argp_state *state)
{
  /* A buffer used for rewriting command line arguments without dashes
     for the parser to understand them.  It gets realloced for each
     successive arg that needs it, on the assumption that args don't
     get parsed multiple times.  */
  static char *arg_hack_buf = 0;
  switch (key)
    {
    case ARGP_KEY_ARG:			/* Non-option argument.  */
      if (!isdigit (*arg) && !state->quoted)
	/* Prepend 2 dashes and feed back to getopt.  */
	{
	  size_t len = strlen (arg) + 1;
	  arg_hack_buf = realloc (arg_hack_buf, 2 + len);
	  state->argv[--state->next] = arg_hack_buf;
	  state->argv[state->next][0] = '-';
	  state->argv[state->next][1] = '-';
	  memcpy (&state->argv[state->next][2], arg, len);
	  break;
	}
      else
	return ARGP_ERR_UNKNOWN;
    case CMD_CHCWDIR:
      cmd_add (&cmd_chcwdir, 0, 1, arg, state);
      break;
    case CMD_CHCRDIR:
      cmd_add (&cmd_chcrdir, 1, 1, arg, state);
      break;
    case CMD_CDROOT:
      cmd_add (&cmd_cdroot, 0, 0, arg, state);
      break;
    case CMD_PWD:
      cmd_add (&cmd_pwd, 0, 0, arg, state);
      break;
    case CMD_GETROOT:
      cmd_add (&cmd_getroot, 0, 0, arg, state);
      break;
    case CMD_UMASK:
      cmd_add (&cmd_umask, 0, 1, arg, state);
      break;
    case CMD_GETENV:
      cmd_add (&cmd_getenv, 0, 1, arg, state);
      break;
    case CMD_SETENV:
      cmd_add (&cmd_setenv, 2, 2, arg, state);
      break;
    case CMD_CLRENV:
      cmd_add (&cmd_clearenv, 0, 0, arg, state);
      break;
    case CMD_SETFD:
      cmd_add (&cmd_setfd, 2, 3, arg, state);
      break;
    case CMD_STDIN:
      cmd_add (&cmd_stdin, 1, 2, arg, state);
      break;
    case CMD_STDOUT:
      cmd_add (&cmd_stdout, 1, 2, arg, state);
      break;
    case CMD_STDERR:
      cmd_add (&cmd_stderr, 1, 2, arg, state);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static char *
help_filter (int key, const char *text, void *input)
{
  if (key == ARGP_KEY_HELP_ARGS_DOC)
    return strdup ("CMD [ARG...]");

  return (char *)text;
}

/* A parser for selecting a command.  */
struct argp cmds_argp = { cmd_options, cmd_parse_opt, 0, 0, 0, help_filter };

static error_t
do_cmd (pid_t pid, cmd_t cmd)
{
  error_t err;
  mach_port_t msgport;
  process_t proc = getproc ();

  /* Get a msgport for PID, to which we can send requests.  */
  err = proc_getmsgport (proc, pid, &msgport);
  if (err)
    error (1, err, "%d: Cannot get process msgport", pid);

  err = (*cmd.f) (pid, msgport, cmd.num_args, cmd.args);
  if (err)
    error (2, err, "%d: Cannot execute command", pid);
	  
  mach_port_deallocate (mach_task_self (), msgport);
  return 0;
}



int 
main(int argc, char *argv[])
{
  cmd_t *cmds = 0;
  size_t num_cmds = 0;
  struct cmds_argp_params cmds_argp_params = { &cmds, &num_cmds };
  pid_t *pids = 0;              /* User-specified pids.  */
  size_t num_pids = 0;
  struct pids_argp_params pids_argp_params = { &pids, &num_pids, 0 };

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_INIT:
	  /* Initialize inputs for child parsers.  */
	  state->child_inputs[0] = &cmds_argp_params;
	  state->child_inputs[1] = &pids_argp_params;
	  break;

	case ARGP_KEY_NO_ARGS:
	  if (!num_cmds || !num_pids)
	    argp_usage (state);
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }

  struct argp_child argp_kids[] =
    { { &cmds_argp, 0,
	"Commands:", 2},
      { &pids_argp, 0,
	"Process selection:", 3},
      {0} };

  struct argp argp = { options, parse_opt, args_doc, doc, argp_kids };

  pid_t cur_pid = getpid ();
  pid_t pid;
  cmd_t cmd;
  size_t i, j;

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  for (i = 0, pid = pids[i]; i < num_pids; pid = pids[++i]) 
    if (pid != cur_pid)
      {
	for (j = 0, cmd = cmds[j]; j < num_cmds; cmd = cmds[++j])
	  {
	    error_t err = do_cmd(pid, cmd);
	    if (err)
	      error (2, err, "%d: Cannot execute command", pid);
	  }
      }

  exit (0);
}

