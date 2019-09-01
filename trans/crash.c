/* GNU Hurd standard crash dump server.

   Copyright (C) 1995, 1996, 1997, 1999, 2000, 2001, 2002, 2006, 2007
   Free Software Foundation, Inc.

   Written by Roland McGrath.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <hurd.h>
#include <fcntl.h>
#include <hurd/trivfs.h>
#include <hurd/msg.h>
#include <sys/wait.h>
#include <error.h>
#include <argp.h>
#include <argz.h>
#include <sys/mman.h>
#include <assert-backtrace.h>
#include <pthread.h>

#include <version.h>

#include "crash_S.h"
#include "crash_reply_U.h"
#include "msg_S.h"


const char *argp_program_version = STANDARD_HURD_VERSION (crash);

process_t procserver;		/* Our proc port, for easy access.  */

/* Port bucket we service requests on.  */
struct port_bucket *port_bucket;

/* Our port classes.  */
struct port_class *trivfs_control_class;
struct port_class *trivfs_protid_class;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ|O_WRITE|O_EXEC;

struct trivfs_control *fsys;

enum crash_action
{
  crash_unspecified,
  crash_suspend,
  crash_kill,
  crash_corefile
};
#define CRASH_DEFAULT		crash_suspend
#define CRASH_ORPHANS_DEFAULT	crash_corefile

static enum crash_action crash_how, crash_orphans_how;
static char *corefile_template;
pthread_mutex_t corefile_template_lock = PTHREAD_MUTEX_INITIALIZER;



/* Template parsing.  */
static int
template_valid (const char *template, const char **errp)
{
  int valid = 0;
  const char *t;
  int specifier = 0;

  for (t = template; *t; t++)
    {
      if (specifier)
	switch (*t)
	  {
	  case '%':
	  case 'p':
	  case 's':
	  case 't':
	    specifier = 0;
	    break;
	  default:
	    goto out;
	  }
      else if (*t == '%')
	specifier = 1;
    }

 out:
  valid = ! specifier;
  *errp = valid? NULL: t;
  return valid;
}

static char *
template_make_file_name (const char *template,
			 task_t task,
			 int signo)
{
  const char *t;
  char *file_name = NULL;
  size_t file_name_len = 0;
  FILE *stream;
  int specifier = 0;

  if (! template_valid (template, &t))
    {
      errno = EINVAL;
      return NULL;
    }

  stream = open_memstream (&file_name, &file_name_len);
  if (stream == NULL)
    return NULL;

  for (t = template; *t; t++)
    {
      if (specifier)
	{
	  switch (*t)
	    {
	    case '%':
	      fprintf (stream, "%%");
	      break;

	    case 'p':
	      fprintf (stream, "%d", task2pid (task));
	      break;

	    case 's':
	      fprintf (stream, "%d", signo);
	      break;

	    case 't':
	      fprintf (stream, "%d", time (NULL));
	      break;

	    default:
	      assert_backtrace (!"reached!");
	    }
	  specifier = 0;
	}
      else if (*t == '%')
	specifier = 1;
      else
	fprintf (stream, "%c", *t);
    }

  assert_backtrace (! specifier);

  fprintf (stream, "%c", 0);
  fclose (stream);

  return file_name;
}



/* This is defined in ../exec/elfcore.c, or we could have
   different implementations for other formats.  */
extern error_t dump_core (task_t task, file_t file, off_t corelimit,
			  int signo, long int sigcode, int sigerror);

/* This data structure describes a crashing task which we have suspended.
   This is attached to a receive right we have set as the process's message
   port, so we can get a msg_sig_post RPC to continue or kill the process.  */

struct crasher
  {
    struct port_info pi;

    /* How to reply to the crash_dump_task RPC.  The RPC remains "in
       progress" while the process is in crash suspension.  If the process
       is resumed with SIGCONT, we will dump a core file and then reply to
       the RPC.  */
    mach_port_t reply_port;
    mach_msg_type_name_t reply_type;

    task_t task;
    file_t core_file;
    off_t core_limit;
    int signo, sigcode, sigerror;

    mach_port_t original_msgport; /* Restore on resume.  */
    mach_port_t sidport;	/* Session ID port for SIGCONT auth.  */
    process_t proc;		/* Proc server port.  */
  };

struct port_class *crasher_portclass;

/* If the process referred to by proc port USERPROC is not orphaned,
   then send SIGTSTP to all the other members of its pgrp.  */
void
stop_pgrp (process_t userproc, mach_port_t cttyid)
{
  pid_t pid, ppid, pgrp;
  int orphaned;
  error_t err;
  size_t numpids = 20;
  pid_t pids_[numpids], *pids = pids_;
  int i;

  err = proc_getpids (userproc, &pid, &ppid, &orphaned);
  if (err || orphaned)
    return;
  err = proc_getpgrp (userproc, pid, &pgrp);
  if (err)
    return;

  /* Use USERPROC so that if it's just died we get an error and don't do
     anything. */
  err = proc_getpgrppids (userproc, pgrp, &pids, &numpids);
  if (err)
    return;

  for (i = 0; i < numpids; i++)
    if (pids[i] != pid)
      {
	mach_port_t msgport;
	if (proc_getmsgport (userproc, pids[i], &msgport))
	  continue;
	msg_sig_post (msgport, SIGTSTP, 0, cttyid);
	mach_port_deallocate (mach_task_self (), msgport);
      }
  if (pids != pids_)
    munmap (pids, numpids);
}


kern_return_t
S_crash_dump_task (mach_port_t port,
		   mach_port_t reply_port, mach_msg_type_name_t reply_type,
		   task_t task, file_t core_file,
		   int signo, integer_t sigcode, int sigerror,
		   natural_t exc, natural_t code, natural_t subcode,
		   mach_port_t ctty_id)
{
  error_t err;
  struct trivfs_protid *cred;
  mach_port_t user_proc = MACH_PORT_NULL;
  enum crash_action how;

  cred = ports_lookup_port (port_bucket, port, trivfs_protid_class);
  if (! cred)
    return EOPNOTSUPP;

  how = crash_how;
  if (crash_how != crash_orphans_how)
    {
      /* We must ascertain if this is an orphan before deciding what to do.  */
      err = proc_task2proc (procserver, task, &user_proc);
      if (!err)
	{
	  pid_t pid, ppid;
	  int orphan;
	  err = proc_getpids (user_proc, &pid, &ppid, &orphan);
	  if (!err && orphan)
	    how = crash_orphans_how;
	}
    }

  switch (how)
    {
    default:			/* NOTREACHED */
      err = EGRATUITOUS;
      break;

    case crash_suspend:
      /* Suspend the task first thing before being twiddling it.  */
      err = task_suspend (task);
      if (err)
	break;

      if (! MACH_PORT_VALID (user_proc))
	err = proc_task2proc (procserver, task, &user_proc);
      if (! err)
	{
	  struct crasher *c;

	  err = ports_create_port (crasher_portclass, port_bucket,
				   sizeof *c, &c);
	  if (! err)
	    {
	      mach_port_t msgport;

	      stop_pgrp (user_proc, ctty_id);

	      /* Install our port as the crasher's msgport.
		 We will wait for signals to resume (crash) it.  */
	      msgport = ports_get_send_right (c);
	      err = proc_setmsgport (user_proc, msgport, &c->original_msgport);
	      mach_port_deallocate (mach_task_self (), msgport);

	      c->reply_port = reply_port;
	      c->reply_type = reply_type;
	      if (proc_getsidport (user_proc, &c->sidport))
		c->sidport = MACH_PORT_NULL;
	      c->proc = user_proc;

	      /* Tell the proc server the crasher stopped.  */
	      proc_mark_stop (user_proc, signo, sigcode);

	      c->task = task;
	      task = MACH_PORT_NULL;
	      c->core_file = core_file;
	      core_file = MACH_PORT_NULL;
	      c->core_limit = (off_t) -1; /* XXX should core limit in RPC */
	      c->signo = signo;
	      c->sigcode = sigcode;
	      c->sigerror = sigerror;

	      err = MIG_NO_REPLY;
	      ports_port_deref (c);
	    }
	}
      if (err != MIG_NO_REPLY)
	task_resume (task);
      break;

    case crash_corefile:
      err = task_suspend (task);
      if (!err)
	{
	  file_t sink = core_file;
	  pthread_mutex_lock (&corefile_template_lock);
	  if (corefile_template)
	    {
	      char *file_name;

	      file_name = template_make_file_name (corefile_template,
						   task, signo);
	      pthread_mutex_unlock (&corefile_template_lock);

	      if (file_name == NULL)
		error (0, errno, "template_make_file_name");
	      else
		{
		  sink = file_name_lookup (file_name, O_WRONLY|O_CREAT,
					   S_IRUSR);
		  if (! MACH_PORT_VALID (sink))
		    {
		      error (0, errno, "%s", file_name);
		      sink = core_file;
		    }
		  free (file_name);
		}
	    }
	  else
	    pthread_mutex_unlock (&corefile_template_lock);

	  err = dump_core (task, sink,
			   (off_t) -1,	/* XXX should get core limit in RPC */
			   signo, sigcode, sigerror);
	  task_resume (task);

	  if (sink != core_file)
	    {
	      mach_port_deallocate (mach_task_self (), sink);

	      /* We return an error so that the libc discards
		 CORE_FILE.  */
	      if (! err)
		err = EEXIST;
	    }
	}
      break;

    case crash_kill:
      {
	if (user_proc != MACH_PORT_NULL)
	  err = 0;
	else
	  err = proc_task2proc (procserver, task, &user_proc);
	if (!err)
	  err = proc_mark_exit (user_proc, W_EXITCODE (0, signo), sigcode);
	err = task_terminate (task);
      }
    }

  if (user_proc != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), user_proc);
  if (err == 0 || err == MIG_NO_REPLY)
    {
      if (MACH_PORT_VALID (task))
	mach_port_deallocate (mach_task_self (), task);
      if (MACH_PORT_VALID (core_file))
	mach_port_deallocate (mach_task_self (), core_file);
      if (MACH_PORT_VALID (ctty_id))
	mach_port_deallocate (mach_task_self (), ctty_id);
    }

  ports_port_deref (cred);
  return err;
}



/* Handle an attempt to send a signal to crashing task C.  */

static error_t
signal_crasher (struct crasher *c, int signo, int sigcode, mach_port_t refport)
{
  error_t err;

  if (refport != c->task && (refport != c->sidport || signo != SIGCONT))
    err = EPERM;
  else
    switch (signo)
      {
      case SIGTERM:
      case SIGKILL:
	/* Kill it as asked.  */
	proc_mark_exit (c->proc, W_EXITCODE (0, signo), sigcode);
	err = task_terminate (c->task);
	break;

      case SIGCONT:
      case SIGQUIT:
	{
	  /* Resuming the process should make it dump core.  */

	  mach_port_t old;

	  /* First, restore its msg port.  */
	  err = proc_setmsgport (c->proc, c->original_msgport, &old);
	  mach_port_deallocate (mach_task_self (), old);

	  /* Tell the proc server it has resumed.  */
	  proc_mark_cont (c->proc);

	  /* Reset the proc server port stored in C, and then destroy the
	     receive right.  The null proc port tells dead_crasher to dump
	     a core file.  */
	  mach_port_deallocate (mach_task_self (), c->proc);
	  c->proc = MACH_PORT_NULL;
	  ports_destroy_right (c);
	}
	break;

      default:
	err = EBUSY;
	break;
      }

  ports_port_deref (c);
  return err;
}

kern_return_t
S_msg_sig_post (mach_port_t port,
		mach_port_t reply_port, mach_msg_type_name_t reply_type,
		int signo, natural_t sigcode, mach_port_t refport)
{
  struct crasher *c = ports_lookup_port (port_bucket, port, crasher_portclass);

  if (! c)
    return EOPNOTSUPP;

  return signal_crasher (c, signo, sigcode, refport);
}

kern_return_t
S_msg_sig_post_untraced (mach_port_t port,
			 mach_port_t reply_port,
			 mach_msg_type_name_t reply_type,
			 int signo, natural_t sigcode, mach_port_t refport)
{
  error_t err;
  struct crasher *c = ports_lookup_port (port_bucket, port, crasher_portclass);

  if (! c)
    return EOPNOTSUPP;

  if (signo != 0 && signo != c->signo)
    return signal_crasher (c, signo, sigcode, refport);

  if (refport != c->task)
    err = EPERM;
  else
    {
      /* Debugger attaching to the process and continuing it.
	 The debugger is welcome to this crasher, so let's
	 just restore his msgport and forget him.  */

      mach_port_t old;

      /* First, restore its msg port.  */
      err = proc_setmsgport (c->proc, c->original_msgport, &old);
      if (! err)
	{
	  mach_port_deallocate (mach_task_self (), old);

	  /* Tell the proc server it has stopped (again)
	     with the original crash signal.  */
	  proc_mark_stop (c->proc, c->signo, c->sigcode);

	  /* We don't need to listen on this msgport any more.  */
	  ports_destroy_right (c);
	}
    }

  ports_port_deref (c);
  return err;
}

/* This gets called when the receive right for a crasher message port dies.  */

void
dead_crasher (void *ptr)
{
  struct crasher *c = ptr;

  if (c->proc != MACH_PORT_NULL)
    {
      /* This message port just died.  Clean it up.  */
      mach_port_deallocate (mach_task_self (), c->proc);
      if (c->reply_port != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), c->reply_port);
    }
  else
    {
      /* C->proc was cleared in S_msg_sig_post as a marker that
	 this crasher should get a core dump when we clean him up.  */
      error_t err = dump_core (c->task, c->core_file, c->core_limit,
			       c->signo, c->sigcode, c->sigerror);
      /* Now reply to the crasher's original RPC which started this whole
         party.  He should now report his own death (with core dump iff ERR
         reports successful dumping) to his proc server.  */
      crash_dump_task_reply (c->reply_port, c->reply_type, err);
      /* Resume the task so it can receive our reply and die happily.  */
      task_resume (c->task);
    }

  /* Deallocate the other saved ports.  */
  mach_port_deallocate (mach_task_self (), c->original_msgport);
  mach_port_deallocate (mach_task_self (), c->sidport);
  mach_port_deallocate (mach_task_self (), c->task);
  mach_port_deallocate (mach_task_self (), c->core_file);
  mach_port_deallocate (mach_task_self (), c->sidport);

  /* The port data structures are cleaned up when we return.  */

  /* See if we are going away and this was the last thing keeping us up.  */
  if (ports_count_class (trivfs_control_class) == 0)
    {
      /* We have no fsys control port, so we are detached from the
	 parent filesystem.  Maybe we have no users left either.  */
      if (ports_count_class (trivfs_protid_class) == 0)
	{
	  /* We have no user ports left.  Maybe we have no crashers still
	     around either.  */
	  if (ports_count_class (crasher_portclass) == 0)
	    /* Nobody talking.  Time to die.  */
	    exit (0);
	  ports_enable_class (crasher_portclass);
	}
      ports_enable_class (trivfs_protid_class);
    }
  ports_enable_class (trivfs_control_class);
}


static const struct argp_option options[] =
{
  {0,0,0,0,"These options specify the disposition of a crashing process:", 1},
  {"action",	'a', "ACTION",	0, "Action taken on crashing processes", 1},
  {"orphan-action", 'O', "ACTION", 0, "Action taken on crashing orphans", 1},

  {0,0,0,0,"These options are synonyms for --action=OPTION:", 2},
  {"suspend",	's', 0,		0, "Suspend the process", 2},
  {"kill",	'k', 0,		0, "Kill the process", 2},
  {"core-file", 'c', 0,		0, "Dump a core file", 2},
  {"dump-core",   0, 0,		OPTION_ALIAS },
  {"core-file-name", 'C', "TEMPLATE", 0,
   "Specify core file name (see below)", 2},
  {0}
};
static const char doc[] =
"Server to handle crashing tasks and dump core files or equivalent.\v"
"The ACTION values can be `suspend', `kill', or `core-file'.\n\n"
"If `--orphan-action' is not specified, the `--action' value is used for "
"orphans.  The default is `--action=suspend --orphan-action=core-file'.\n"
"\n"
"The core file is either written to the file provided by the "
"crashing process, or if a TEMPLATE value is given, to the file "
"with the name constructed by expanding TEMPLATE value.  "
"TEMPLATE may contain % specifiers:\n"
"\n"
"\t%%  just %\n"
"\t%p  the process' PID\n"
"\t%s  the signal number that caused the dump\n"
"\t%t  time of crash in seconds since the EPOCH\n";

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  error_t parse_action (enum crash_action *how)
    {
      if (!strcmp (arg, "suspend"))
	*how = crash_suspend;
      else if (!strcmp (arg, "kill"))
	*how = crash_kill;
      else if (!strcmp (arg, "core-file"))
	*how = crash_corefile;
      else
	{
	  argp_error (state,
		      "action must be one of: suspend, kill, core-file");
	  return EINVAL;
	}
      return 0;
    }

  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_INIT:
    case ARGP_KEY_ERROR:
      break;

    case 'a':
      return parse_action (&crash_how);
    case 'O':
      return parse_action (&crash_orphans_how);

    case 's': crash_how = crash_suspend;	break;
    case 'k': crash_how = crash_kill;		break;
    case 'c': crash_how = crash_corefile;	break;
    case 'C':
      {
	char *errp;
	if (! template_valid (arg, &errp))
	  {
	    argp_error (state, "Invalid template: ...'%s'", errp);
	    return EINVAL;
	  }
      }
      pthread_mutex_lock (&corefile_template_lock);
      free (corefile_template);
      if (strlen (arg) == 0)
	corefile_template = NULL;
      else
	{
	  corefile_template = strdup (arg);
	  if (corefile_template == NULL)
	    {
	      pthread_mutex_unlock (&corefile_template_lock);
	      argp_failure (state, 1, errno, "strdup");
	      return errno;
	    }
	}
      pthread_mutex_unlock (&corefile_template_lock);
      break;

    case ARGP_KEY_SUCCESS:
      if (crash_orphans_how == crash_unspecified)
	crash_orphans_how = (crash_how == crash_unspecified
			     ? CRASH_ORPHANS_DEFAULT : crash_how);
      if (crash_how == crash_unspecified)
	crash_how = CRASH_DEFAULT;
      break;
    }
  return 0;
}

error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  error_t err;
  const char *opt;

  switch (crash_how)
    {
    case crash_suspend:		opt = "--action=suspend";	break;
    case crash_kill:		opt = "--action=kill";		break;
    case crash_corefile:	opt = "--action=core-file";	break;
    default:
      return EGRATUITOUS;
    }
  err = argz_add (argz, argz_len, opt);

  if (!err)
    {
      switch (crash_orphans_how)
        {
	case crash_suspend:	opt = "--orphan-action=suspend";	break;
	case crash_kill:	opt = "--orphan-action=kill";		break;
	case crash_corefile:	opt = "--orphan-action=core-file";	break;
	default:
	  return EGRATUITOUS;
        }
      err = argz_add (argz, argz_len, opt);
    }

  pthread_mutex_lock (&corefile_template_lock);
  if (!err && corefile_template)
    {
      char *template;
      if (asprintf (&template, "--core-file-name=%s", corefile_template) < 0)
	err = errno;
      else
	{
	  err = argz_add (argz, argz_len, template);
	  free (template);
	}
    }
  pthread_mutex_unlock (&corefile_template_lock);

  return err;
}

struct argp crash_argp = { options, parse_opt, 0, doc };
struct argp *trivfs_runtime_argp = &crash_argp;


static int
crash_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int crash_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  extern int msg_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  return (crash_server (inp, outp) ||
	  msg_server (inp, outp) ||
	  trivfs_demuxer (inp, outp));
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  argp_parse (&crash_argp, argc, argv, 0,0,0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  /* Fetch our proc server port for easy use.  */
  procserver = getproc ();

  crasher_portclass = ports_create_class (dead_crasher, 0);

  err = trivfs_add_control_port_class (&trivfs_control_class);
  if (err)
    error (1, 0, "error creating control port class");

  err = trivfs_add_protid_port_class (&trivfs_protid_class);
  if (err)
    error (1, 0, "error creating protid port class");

  /* Reply to our parent.  */
  err = trivfs_startup (bootstrap, 0,
                        trivfs_control_class, NULL,
                        trivfs_protid_class, NULL, &fsys);

  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "Contacting parent");

  port_bucket = fsys->pi.bucket;

  /* Launch.  */
  do
    ports_manage_port_operations_multithread (port_bucket, crash_demuxer,
					      10 * 1000, /* idle thread */
					      10 * 60 * 1000, /* idle server */
					      0);
  /* That returns when 10 minutes pass without an RPC.  Try shutting down
     as if sent fsys_goaway; if we have any users who need us to stay
     around, this returns EBUSY and we loop to service more RPCs.  */
  while (trivfs_goaway (fsys, 0));

  return 0;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;

  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_control_class);
  ports_inhibit_class_rpcs (trivfs_protid_class);

  /* Are there any extant user ports for the /servers/crash file?  */
  count = ports_count_class (trivfs_protid_class);
  if (count == 0 || (flags & FSYS_GOAWAY_FORCE))
    {
      /* No users.  Disconnect from the filesystem.  */
      mach_port_deallocate (mach_task_self (), fsys->underlying);

      /* Are there any crasher message ports we are listening on?  */
      count = ports_count_class (crasher_portclass);
      if (count == 0)
	/* Nope.  We got no reason to live.  */
	exit (0);

      /* Continue babysitting crashing tasks we previously suspended.  */
      ports_enable_class (crasher_portclass);

      /* No more communication with the parent filesystem.  */
      ports_destroy_right (fsys);

      return 0;
    }
  else
    {
      /* We won't go away, so start things going again...  */
      ports_enable_class (trivfs_protid_class);
      ports_resume_class_rpcs (trivfs_control_class);
      ports_resume_class_rpcs (trivfs_protid_class);

      return EBUSY;
    }
}

/* Stubs for unused msgport RPCs.  */

kern_return_t
S_msg_proc_newids (mach_port_t process,
		   mach_port_t task,
		   pid_t ppid,
		   pid_t pgrp,
		   int orphaned)
{ return EBUSY; }

kern_return_t
S_msg_add_auth (mach_port_t process,
		auth_t auth)
{ return EBUSY; }

kern_return_t
S_msg_del_auth (mach_port_t process,
		mach_port_t task,
		intarray_t uids,
		mach_msg_type_number_t uidsCnt,
		intarray_t gids,
		mach_msg_type_number_t gidsCnt)
{ return EBUSY; }

kern_return_t
S_msg_get_init_port (mach_port_t process,
		     mach_port_t refport,
		     int which,
		     mach_port_t *port,
		     mach_msg_type_name_t *portPoly)
{ return EBUSY; }

kern_return_t
S_msg_set_init_port (mach_port_t process,
		     mach_port_t refport,
		     int which,
		     mach_port_t port)
{ return EBUSY; }

kern_return_t
S_msg_get_init_ports (mach_port_t process,
		      mach_port_t refport,
		      portarray_t *ports,
		      mach_msg_type_name_t *portsPoly,
		      mach_msg_type_number_t *portsCnt)
{ return EBUSY; }

kern_return_t
S_msg_set_init_ports (mach_port_t process,
		      mach_port_t refport,
		      portarray_t ports,
		      mach_msg_type_number_t portsCnt)
{ return EBUSY; }

kern_return_t
S_msg_get_init_int (mach_port_t process,
		    mach_port_t refport,
		    int which,
		    int *value)
{ return EBUSY; }

kern_return_t
S_msg_set_init_int (mach_port_t process,
		    mach_port_t refport,
		    int which,
		    int value)
{ return EBUSY; }

kern_return_t
S_msg_get_init_ints (mach_port_t process,
		     mach_port_t refport,
		     intarray_t *values,
		     mach_msg_type_number_t *valuesCnt)
{ return EBUSY; }

kern_return_t
S_msg_set_init_ints (mach_port_t process,
		     mach_port_t refport,
		     intarray_t values,
		     mach_msg_type_number_t valuesCnt)
{ return EBUSY; }

kern_return_t
S_msg_get_dtable (mach_port_t process,
		  mach_port_t refport,
		  portarray_t *dtable,
		  mach_msg_type_name_t *dtablePoly,
		  mach_msg_type_number_t *dtableCnt)
{ return EBUSY; }

kern_return_t
S_msg_set_dtable (mach_port_t process,
		  mach_port_t refport,
		  portarray_t dtable,
		  mach_msg_type_number_t dtableCnt)
{ return EBUSY; }

kern_return_t
S_msg_get_fd (mach_port_t process,
	      mach_port_t refport,
	      int fd,
	      mach_port_t *port,
	      mach_msg_type_name_t *portPoly)
{ return EBUSY; }

kern_return_t
S_msg_set_fd (mach_port_t process,
	      mach_port_t refport,
	      int fd,
	      mach_port_t port)
{ return EBUSY; }

kern_return_t
S_msg_get_environment (mach_port_t process,
		       data_t *value,
		       mach_msg_type_number_t *valueCnt)
{ return EBUSY; }

kern_return_t
S_msg_set_environment (mach_port_t process,
		       mach_port_t refport,
		       data_t value,
		       mach_msg_type_number_t valueCnt)
{ return EBUSY; }

kern_return_t
S_msg_get_env_variable (mach_port_t process,
			string_t variable,
			data_t *value,
			mach_msg_type_number_t *valueCnt)
{ return EBUSY; }

kern_return_t
S_msg_set_env_variable (mach_port_t process,
			mach_port_t refport,
			string_t variable,
			string_t value,
			boolean_t replace)
{ return EBUSY; }
kern_return_t
S_msg_get_exec_flags (mach_port_t process, mach_port_t refport, int *flags)
{ return EBUSY; }
kern_return_t
S_msg_set_all_exec_flags (mach_port_t process, mach_port_t refport, int flags)
{ return EBUSY; }
kern_return_t
S_msg_set_some_exec_flags (mach_port_t process, mach_port_t refport, int flags)
{ return EBUSY; }
kern_return_t
S_msg_clear_some_exec_flags (mach_port_t process, mach_port_t refport,
			     int flags)
{ return EBUSY; }
error_t
S_msg_report_wait (mach_port_t process, thread_t thread,
		   string_t desc, mach_msg_id_t *rpc)
{ return EBUSY; }
error_t
S_msg_describe_ports (mach_port_t msgport, mach_port_t refport,
		      mach_port_t *ports, mach_msg_type_number_t nports,
		      data_t *desc, mach_msg_type_number_t *desclen)
{ return EBUSY; }
