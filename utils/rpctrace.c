/* Trace RPCs sent to selected ports

   Copyright (C) 1998, 1999 Free Software Foundation, Inc.

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
#include <hurd/ports.h>
#include <hurd/ihash.h>
#include <mach/message.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <argp.h>
#include <error.h>
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (rpctrace);

static const struct argp_option options[] = {
  {"output", 'o', "FILE", 0, "Send trace output to FILE instead of stderr."},
  {0}
};

static const char *args_doc = "COMMAND [ARG...]";
static const char *doc =
"Trace Mach Remote Procedure Calls."
"\v.";


struct traced_info
{
  struct port_info pi;
  mach_port_t forward;
};

task_t traced_task;
ihash_t traced_names;
struct port_class *traced_class;
struct port_bucket *traced_bucket;
FILE *ostream;
cthread_t traced_thread;
char *cmd;
char **cmd_argv;
char **cmd_envp;

int print_message (FILE *stream, mach_msg_header_t *msg);

#if 1
int
print_message (FILE *stream, mach_msg_header_t *msg)
{
  fprintf (stream, "[%d] %d -> %d\n", msg->msgh_id, 
	   msg->msgh_local_port, msg->msgh_remote_port);
  return 0;
}
#endif

mach_port_t
trace_wrapper (mach_port_t orig)
{
  struct traced_info *info;
  mach_port_t port = MACH_PORT_NULL;

  /* Never wrap the null port. */
  if (orig == MACH_PORT_NULL)
    return orig;

  info = ihash_find (traced_names, orig);
  if (! info)
    {
      error_t err;
      mach_port_type_t type;
      mach_msg_type_name_t typename;
      
      ports_create_port (traced_class, traced_bucket,
			 sizeof (struct traced_info), &info);
      port = ports_get_right (info);
      info->forward = MACH_PORT_NULL;

      mach_port_type (traced_task, orig, &type);
      /* FIXME: type == 0x11bdf68: how do I get the type of right? */

      if (type & MACH_PORT_TYPE_SEND)
	{
	  err = mach_port_extract_right (traced_task, orig,
				   MACH_MSG_TYPE_MOVE_SEND,
				   &info->forward, &typename);
	  assert_perror (err);
	  err = mach_port_insert_right (traced_task, orig, port,
				  MACH_MSG_TYPE_MAKE_SEND);
	  assert_perror (err);
	}
      if (type & MACH_PORT_TYPE_SEND_ONCE)
	{
	  err = mach_port_extract_right (traced_task, orig, 
				   MACH_MSG_TYPE_MOVE_SEND_ONCE,
				   &info->forward, &typename);
	  assert_perror (err);
	  err = mach_port_insert_right (traced_task, orig, port,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE);
	  assert_perror (err);
	}
      if (info->forward == MACH_PORT_NULL)
	{
	  err = mach_port_extract_right (traced_task, orig, 
				   MACH_MSG_TYPE_MAKE_SEND,
				   &info->forward, &typename);
	  assert_perror (err);
	}

      ihash_add (traced_names, orig, info, NULL);

    }
  else
    port = ports_get_right (info);
  mach_port_insert_right (mach_task_self (), port, port,
			  MACH_MSG_TYPE_MAKE_SEND);
  return port;
}

int
trace_and_forward (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  struct traced_info *info;
  void *msg_buf_ptr = (void *) inp + sizeof (*inp);
  
  /* Print the message trace.  */
  print_message (ostream, inp); fflush (ostream);

  /* Wrap ports.  */
  while (msg_buf_ptr < (void *) inp + inp->msgh_size)
    {
      mach_msg_type_t type = *(mach_msg_type_t *) msg_buf_ptr;
      msg_buf_ptr += sizeof (mach_msg_type_t);
      if (type.msgt_name == MACH_MSG_TYPE_PORT_NAME)
	{
	  mach_port_t *port = msg_buf_ptr;
	  *port = trace_wrapper (*port);
	}
      msg_buf_ptr += type.msgt_size / 8;
    }

  info = ports_lookup_port (traced_bucket, inp->msgh_local_port, 
			    traced_class);
  if (! info)
    return 0;

  /* Swap the header data like a crossover cable. */
  inp->msgh_bits
    = MACH_MSGH_BITS (MACH_MSGH_BITS_LOCAL (inp->msgh_bits),
		      MACH_MSGH_BITS_REMOTE (inp->msgh_bits));
  inp->msgh_local_port = trace_wrapper (inp->msgh_remote_port);
  inp->msgh_remote_port = info->forward;

  /* Resend the message to the tracee.  */
  mach_msg (inp, MACH_SEND_MSG, inp->msgh_size, 0,
	    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

  return 1;
}

static any_t
trace_thread_function (struct port_bucket *bucket)
{
  ports_manage_port_operations_one_thread (bucket, trace_and_forward, 0);
  return 0;
}

static any_t
traced_thread_function (struct port_bucket *bucket)
{
  mach_port_t wrapper;
  struct traced_info *info;
  file_t file = file_name_path_lookup (cmd, getenv ("PATH"), O_EXEC, 0, 0);

  if (file == MACH_PORT_NULL)
    error (1, errno, "cannot lookup `%s' in PATH", cmd);
  task_create (mach_task_self (), 0, &traced_task);

  /* Create a trace wrapper for the task port. */
  ports_create_port (traced_class, traced_bucket, sizeof (*info), &info);
  info->forward = traced_task;
  ihash_add (traced_names, wrapper, info, NULL);

  /* Make sure the traced task uses this wrapper. */
  wrapper = ports_get_right (info);
  mach_port_insert_right (mach_task_self (), wrapper,
			  wrapper, MACH_MSG_TYPE_MAKE_SEND);
  task_set_special_port (traced_task, TASK_KERNEL_PORT, wrapper);
  
  /* Now actually run the command they told us to trace. */
  proc_child (getproc (), traced_task);
  _hurd_exec (traced_task, file, cmd_argv, cmd_envp);

  /* FIXME: falls through immediately if we don't spin */
  for (;;) ;
  return 0;
}

int
main (int argc, char **argv, char **envp)
{

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'o':
	  ostream = fopen (arg, "w");
	  break;
	  
	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	  return EINVAL;

	case ARGP_KEY_ARG:
	  cmd = arg;
	  cmd_argv = &state->argv[state->next-1];
	  cmd_envp = envp;
	  state->next = state->argc;
	  traced_thread = cthread_fork ((cthread_fn_t)
					traced_thread_function, 0);
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  traced_class = ports_create_class (0, 0);
  traced_bucket = ports_create_bucket();

  ihash_create (&traced_names);
  ostream = stderr;
  
  cthread_detach (cthread_fork ((cthread_fn_t)trace_thread_function,
				traced_bucket));
  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  cthread_join (traced_thread);

  fprintf (stderr, "***************hola***************\n");
  exit (0);
}

/*
  Local Variables:
  compile-command: "gcc -Wall -g -D_GNU_SOURCE=1 -o rpctrace rpctrace.c -lthreads -lports -lihash"
  End:
*/
