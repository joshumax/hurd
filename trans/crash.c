/* GNU Hurd standard crash dump server.
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#include <sys/wait.h>
#include <error.h>

#include "crash_S.h"
#include "crash_reply_U.h"
#include "msg_S.h"


process_t procserver;		/* Our proc port, for easy access.  */

/* Port bucket we service requests on.  */
struct port_bucket *port_bucket;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ|O_WRITE|O_EXEC;

struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;

struct trivfs_control *fsys;


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
    string_t target;
    int signo, sigcode, sigerror;

    mach_port_t original_msgport; /* Restore on resume.  */
    mach_port_t sidport;	/* Session ID port for SIGCONT auth.  */
    process_t proc;		/* Proc server port.  */
  };

struct port_class *crasher_portclass;


kern_return_t
S_crash_dump_task (mach_port_t port,
		   mach_port_t reply_port, mach_msg_type_name_t reply_type,
		   task_t task, file_t core_file, char *target,
		   int signo, int sigcode, int sigerror,
		   natural_t exc, natural_t code, natural_t subcode,
		   mach_port_t ctty_id)

{
  error_t err;
  struct trivfs_protid *cred;
  mach_port_t user_proc;

  cred = ports_lookup_port (port_bucket, port, trivfs_protid_portclasses[0]);
  if (! cred)
    return EOPNOTSUPP;

  /* Suspend the task first thing before being twiddling it.  */
  err = task_suspend (task);

  if (! err)
    {
      err = proc_task2proc (procserver, task, &user_proc);
      if (! err)
	{
	  struct crasher *c;

	  err = ports_create_port (crasher_portclass, port_bucket,
				   sizeof *c, &c);
	  if (! err)
	    {
	      /* Install our port as the crasher's msgport.
		 We will wait for signals to resume (crash) it.  */
	      mach_port_t msgport = ports_get_right (c);
	      mach_port_insert_right (mach_task_self (), msgport,
				      msgport, MACH_MSG_TYPE_MAKE_SEND);
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
	      c->core_file = core_file;
	      strcpy (c->target, target);
	      c->signo = signo;
	      c->sigcode = sigcode;
	      c->sigerror = sigerror;

	      err = MIG_NO_REPLY;
	      ports_port_deref (c);
	    }
	}
      if (err != MIG_NO_REPLY)
	task_resume (task);
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

error_t
dump_core (task_t task, file_t core_file, const char *target,
	   int signo, long int sigcode, int sigerror)
{
  return ENOSYS;		/* XXX */
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
      error_t err = dump_core (c->task, c->core_file, c->target,
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
  if (ports_count_class (trivfs_cntl_portclasses[0]) == 0)
    {
      /* We have no fsys control port, so we are detached from the
	 parent filesystem.  Maybe we have no users left either.  */
      if (ports_count_class (trivfs_protid_portclasses[0]) == 0)
	{
	  /* We have no user ports left.  Maybe we have no crashers still
	     around either.  */
	  if (ports_count_class (crasher_portclass) == 0)
	    /* Nobody talking.  Time to die.  */
	    exit (0);
	  ports_enable_class (crasher_portclass);
	}
      ports_enable_class (trivfs_protid_portclasses[0]);
    }
  ports_enable_class (trivfs_cntl_portclasses[0]);
}



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


  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  /* Fetch our proc server port for easy use.  */
  procserver = getproc ();

  port_bucket = ports_create_bucket ();
  trivfs_cntl_portclasses[0] = ports_create_class (trivfs_clean_cntl, 0);
  trivfs_protid_portclasses[0] = ports_create_class (trivfs_clean_protid, 0);
  crasher_portclass = ports_create_class (dead_crasher, 0);

  /* Reply to our parent.  */
  err = trivfs_startup (bootstrap, 0,
			trivfs_cntl_portclasses[0], port_bucket,
			trivfs_protid_portclasses[0], port_bucket,
			&fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "Contacting parent");

  /* Launch.  */
  do
    ports_manage_port_operations_multithread (port_bucket, crash_demuxer,
					      10 * 1000, /* idle thread */
					      10 * 60 * 1000, /* idle server */
					      0, MACH_PORT_NULL);
  /* That returns when 10 minutes pass without an RPC.  Try shutting down
     as if sent fsys_goaway; if we have any users who need us to stay
     around, this returns EBUSY and we loop to service more RPCs.  */
  while (trivfs_goaway (fsys, 0));

  return 0;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_fstype = FSTYPE_MISC;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;

  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_cntl_portclasses[0]);
  ports_inhibit_class_rpcs (trivfs_protid_portclasses[0]);

  /* Are there any extant user ports for the /servers/crash file?  */
  count = ports_count_class (trivfs_protid_portclasses[0]);
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
      ports_enable_class (trivfs_protid_portclasses[0]);
      ports_resume_class_rpcs (trivfs_cntl_portclasses[0]);
      ports_resume_class_rpcs (trivfs_protid_portclasses[0]);

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
S_msg_startup_dosync (mach_port_t process)
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
		   string_t desc, int *rpc)
{ return EBUSY; }
