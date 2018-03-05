/* An RPC scanner for the Hurd.

   Copyright (C) 2015 Free Software Foundation, Inc.

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

#include <mach.h>
#include <hurd.h>
#include <hurd/ihash.h>
#include <mach/message.h>
#include <unistd.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <version.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <argz.h>

#include "msgids.h"

/* Send messages with arbitrary message ids.  */
struct Request {
  mach_msg_header_t Head;
} RequestTemplate;

struct Reply {
  mach_msg_header_t Head;
  mach_msg_type_t RetCodeType;
  kern_return_t RetCode;
  char Body[4096];
};

union {
  struct Request Request;
  struct Reply Reply;
} Message;

error_t
setup (mach_port_t request, mach_msg_type_name_t requestType)
{
  RequestTemplate.Head.msgh_remote_port = request;
  if (! MACH_PORT_VALID (RequestTemplate.Head.msgh_local_port))
    RequestTemplate.Head.msgh_local_port = mach_reply_port ();
  RequestTemplate.Head.msgh_bits =
    MACH_MSGH_BITS (requestType, MACH_MSG_TYPE_MAKE_SEND_ONCE);
  return 0;
}

error_t
send (mach_msg_id_t msgid)
{
  error_t err;
  Message.Request = RequestTemplate;
  Message.Request.Head.msgh_id = msgid;
  err = mach_msg (&Message.Request.Head,
		  MACH_SEND_MSG|MACH_RCV_MSG|MACH_MSG_OPTION_NONE,
		  sizeof Message.Request,
		  sizeof Message.Reply,
		  Message.Request.Head.msgh_local_port,
		  MACH_MSG_TIMEOUT_NONE,
		  MACH_PORT_NULL);
  if (err)
    return err;

  /* XXX typecheck */
  return Message.Reply.RetCode;
}

typedef error_t (*setup_function_t) ();
setup_function_t setup_target;
void *setup_argument;

error_t
setup_task_target (void)
{
  error_t err;
  static task_t task;
  static mach_msg_type_name_t taskType = MACH_MSG_TYPE_COPY_SEND;

  if (MACH_PORT_VALID (task))
    {
      task_terminate (task);
      mach_port_deallocate (mach_task_self (), task);
    }

  err = task_create (mach_task_self (), 0, &task);
  if (err)
    return err;

  return setup (task, taskType);
}

error_t
setup_thread_target (void)
{
  error_t err;
  static task_t task;
  static thread_t thread;

  if (MACH_PORT_VALID (thread))
    {
      thread_terminate (thread);
      mach_port_deallocate (mach_task_self (), thread);
    }

  if (MACH_PORT_VALID (task))
    {
      task_terminate (task);
      mach_port_deallocate (mach_task_self (), task);
    }

  err = task_create (mach_task_self (), 0, &task);
  if (err)
    return err;

  err = thread_create (task, &thread);
  if (err)
    return err;

  return setup (thread, MACH_MSG_TYPE_COPY_SEND);
}

error_t
setup_proc_target (void)
{
  error_t err;
  static task_t task;
  static process_t proc, target;
  static mach_msg_type_name_t targetType = MACH_MSG_TYPE_COPY_SEND;

  if (! MACH_PORT_VALID (proc))
    proc = getproc ();
  if (MACH_PORT_VALID (task))
    mach_port_deallocate (mach_task_self (), task);
  if (MACH_PORT_VALID (target))
    mach_port_deallocate (mach_task_self (), target);

  err = task_create (mach_task_self (), 0, &task);
  if (err)
    return err;

  err = proc_task2proc (proc, task, &target);
  if (err)
    return err;

  return setup (target, targetType);
}

error_t
setup_auth_target (void)
{
  static auth_t auth;
  static mach_msg_type_name_t authType = MACH_MSG_TYPE_COPY_SEND;

  if (MACH_PORT_VALID (auth))
    mach_port_deallocate (mach_task_self (), auth);

  auth = getauth ();
  if (! MACH_PORT_VALID (auth))
    return errno;

  return setup (auth, authType);
}

error_t
setup_hurd_target (void)
{
  char *name = (char *) setup_argument;
  mach_port_t request;
  mach_msg_type_name_t requestType;

  request = file_name_lookup (name, 0, 0);
  if (! MACH_PORT_VALID (request))
    return errno;
  requestType = MACH_MSG_TYPE_COPY_SEND;

  return setup (request, requestType);
}

task_t extract_target_task;
mach_port_t extract_target_port;
mach_msg_type_name_t extract_target_type;

error_t
setup_extract_target (void)
{
  error_t err;
  mach_port_t request;
  mach_msg_type_name_t requestType;

  err = mach_port_extract_right (extract_target_task,
                                 extract_target_port,
                                 extract_target_type,
                                 &request,
                                 &requestType);
  if (err)
    error (1, err, "mach_port_extract_right");
  if (err)
    return err;
  requestType = MACH_MSG_TYPE_COPY_SEND;
  return setup (request, requestType);
}

const char *argp_program_version = STANDARD_HURD_VERSION (rpcscan);

char **cmd_argv;
int verbose;
int numeric;
int subsystem;

#define OPT_TARGET_TASK		-1
#define OPT_TARGET_THREAD	-2
#define OPT_TARGET_PROC		-3
#define OPT_TARGET_AUTH		-4
#define OPT_TARGET_EXTRACT	-5

static const struct argp_option options[] =
{
  {NULL, 0, NULL, OPTION_DOC, "Target selection", 1},
  {"task", OPT_TARGET_TASK, NULL, 0, "target a task port", 1},
  {"thread", OPT_TARGET_THREAD, NULL, 0, "target a thread port", 1},
  {"proc", OPT_TARGET_PROC, NULL, 0, "target a proc port", 1},
  {"auth", OPT_TARGET_AUTH, NULL, 0, "target an auth port", 1},
  {"extract", OPT_TARGET_EXTRACT, "PID.PORT", 0, "target port PORT of PID", 1},

  {NULL, 0, NULL, OPTION_DOC, "Options", 2},
  {"verbose", 'v', NULL, 0, "be verbose", 2},
  {"numeric", 'n', NULL, 0, "show numeric message ids", 2},
  {"subsystem", 's', NULL, 0, "show subsystem names", 2},
  {0}
};

static const char args_doc[] = "[FILE]";
static const char doc[] = "Scan a given Mach port.";

/* Parse our options...	 */
error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  error_t err;
  switch (key)
    {
    case 'v':
      verbose = 1;
      break;

    case 'n':
      numeric = 1;
      break;

    case 's':
      subsystem = 1;
      break;

#define SELECT_TARGET(target)					\
      if (setup_target)						\
	argp_error (state, "Multiple targets specified.");	\
      setup_target = target;

    case OPT_TARGET_TASK:
      SELECT_TARGET (setup_task_target);
      break;

    case OPT_TARGET_THREAD:
      SELECT_TARGET (setup_thread_target);
      break;

    case OPT_TARGET_PROC:
      SELECT_TARGET (setup_proc_target);
      break;

    case OPT_TARGET_AUTH:
      SELECT_TARGET (setup_auth_target);
      break;

    case OPT_TARGET_EXTRACT:;
      process_t proc;
      pid_t pid;
      char *end;

      pid = strtol (arg, &end, 10);
      if (arg == end || *end != '.')
        argp_error (state, "Expected format PID.PORT, got `%s'.", arg);

      arg = end + 1;
      extract_target_port = strtol (arg, &end, 10);
      if (arg == end || *end != '\0')
        argp_error (state, "Expected format PORT, got `%s'.", arg);

      proc = getproc ();
      err = proc_pid2task (proc, pid, &extract_target_task);
      if (err)
        argp_failure (state, 1, err,
                      "Could not get task of process %d", pid);

      extract_target_type = MACH_MSG_TYPE_COPY_SEND; /* XXX */
      SELECT_TARGET (setup_extract_target);
      break;

    case ARGP_KEY_ARG:
      SELECT_TARGET (setup_hurd_target);
      setup_argument = arg;
      break;
#undef SELECT_TARGET

    case ARGP_KEY_NO_ARGS:
      if (setup_target == NULL)
	argp_usage (state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

const struct argp_child children[] =
  {
    { .argp=&msgid_argp, },
    { 0 }
  };

const struct argp argp = { options, parse_opt, args_doc, doc, children };

void
format_msgid (char *buf, size_t len, mach_msg_id_t id)
{
  if (numeric)
  numeric:
    snprintf (buf, len, "%d", id);
  else
    {
      const struct msgid_info *info;
      info = msgid_info (id);
      if (info == NULL)
        goto numeric;

      if (subsystem)
        snprintf (buf, len, "%s/%s", info->subsystem, info->name);
      else
        snprintf (buf, len, "%s", info->name);
    }
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_msg_id_t msgid;

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  err = setup_target ();
  if (err)
    /* Initial setup failed.  Bail out.  */
    error (1, err, "%s",
	setup_target == setup_hurd_target? (char *) setup_argument: "setup");

  for (msgid = 0; msgid < 500000; msgid++)
    {
      err = send (msgid);
      switch (err)
	{
	case MACH_SEND_INVALID_DEST:
	  err = setup_target ();
	  if (err)
	    error (1, err, "setup");
	  msgid--;	/* redo */
	  continue;

	case MIG_BAD_ID:
	  /* do nothing */
	  break;

	default:;
          char buf[80];
          format_msgid (buf, sizeof buf, msgid);
          if (verbose)
            error (0, err, "%s", buf);
          else
            fprintf (stdout, "%s\n", buf);
	}
    }

  return EXIT_SUCCESS;
}
