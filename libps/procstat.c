/* The proc_stat type, which holds information about a hurd process.

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
#include <assert.h>
#include <string.h>

#include "ps.h"
#include "common.h"

#include "ps_msg.h"

/* ---------------------------------------------------------------- */

/* These directly correspond to the bits in a state, starting from 0.  See
   ps.h for an explanation of what each of these means.  */
char *proc_stat_state_tags = "TZRHDSIN<u+slfmpoxwg";

/* ---------------------------------------------------------------- */

/* Return the PSTAT_STATE_ bits describing the state of an individual thread,
   from that thread's thread_basic_info_t struct */
static int 
thread_state (thread_basic_info_t bi)
{
  int state = 0;

  switch (bi->run_state)
    {
    case TH_STATE_RUNNING:
      state |= PSTAT_STATE_T_RUN;
      break;
    case TH_STATE_UNINTERRUPTIBLE:
      state |= PSTAT_STATE_T_WAIT;
      break;
    case TH_STATE_HALTED:
      state |= PSTAT_STATE_T_HALT;
      break;
    case TH_STATE_STOPPED:
      state |= PSTAT_STATE_T_HALT | PSTAT_STATE_T_UNCLEAN;
      break;
    case TH_STATE_WAITING:
      /* Act like unix: waits of less than 20 seconds means a process is
	 `sleeping' and >= 20 seconds means it's `idle' */
      state |= bi->sleep_time < 20 ? PSTAT_STATE_T_SLEEP : PSTAT_STATE_T_IDLE;
      break;
    }

  if (bi->base_priority < 12)
    state |= PSTAT_STATE_T_NASTY;
  else if (bi->base_priority > 12)
    state |= PSTAT_STATE_T_NICE;

  return state;
}

/* ---------------------------------------------------------------- */

/* The set of PSTAT_ flags that we get using proc_getprocinfo.  */
#define PSTAT_PROCINFO \
  (PSTAT_PROC_INFO | PSTAT_TASK_BASIC | PSTAT_NUM_THREADS \
   | PSTAT_THREAD_BASIC | PSTAT_THREAD_SCHED | PSTAT_THREAD_WAIT)
/* The set of things we get from procinfo that's thread dependent.  */
#define PSTAT_PROCINFO_THREAD \
 (PSTAT_NUM_THREADS |PSTAT_THREAD_BASIC |PSTAT_THREAD_SCHED |PSTAT_THREAD_WAIT)

/* Fetches process information from the set in PSTAT_PROCINFO, returning it
   in PI & PI_SIZE.  NEED is the information, and HAVE is the what we already
   have.  */
static error_t
fetch_procinfo (process_t server, pid_t pid,
		ps_flags_t need, ps_flags_t have,
		struct procinfo **pi, size_t *pi_size,
		char **waits, size_t *waits_len)
{
  int pi_flags = 0;

  if ((need & PSTAT_TASK_BASIC) && !(have & PSTAT_TASK_BASIC))
    pi_flags |= PI_FETCH_TASKINFO;
  if ((need & PSTAT_NUM_THREADS) && !(have & PSTAT_NUM_THREADS))
    pi_flags |= PI_FETCH_THREADS;
  if ((need & PSTAT_THREAD_BASIC) && !(have & PSTAT_THREAD_BASIC))
    pi_flags |= PI_FETCH_THREAD_BASIC | PI_FETCH_THREADS;
  if ((need & PSTAT_THREAD_BASIC) && !(have & PSTAT_THREAD_BASIC))
    pi_flags |= PI_FETCH_THREAD_BASIC | PI_FETCH_THREADS;
  if ((need & PSTAT_THREAD_SCHED) && !(have & PSTAT_THREAD_SCHED))
    pi_flags |= PI_FETCH_THREAD_SCHED | PI_FETCH_THREADS;
  if ((need & PSTAT_THREAD_WAIT) && !(have & PSTAT_THREAD_WAIT))
    pi_flags |= PI_FETCH_THREAD_WAITS | PI_FETCH_THREADS;

  if (pi_flags || ((need & PSTAT_PROC_INFO) && !(have & PSTAT_PROC_INFO)))
    {
      error_t err =
	proc_getprocinfo (server, pid, &pi_flags, (procinfo_t *)pi, pi_size,
			  waits, waits_len);
      return err;
    }
  else
    return 0;
}

/* Fetches process information from the set in PSTAT_PROCINFO, returning it
   in PI & PI_SIZE, and if *PI_SIZE is non-zero, merges the new information
   with what was in *PI, and deallocates *PI.  NEED is the information, and
   HAVE is the what we already have.  */
static error_t
merge_procinfo (process_t server, pid_t pid,
		ps_flags_t need, ps_flags_t have,
		struct procinfo **pi, size_t *pi_size,
		char **waits, size_t *waits_len)
{
  struct procinfo *new_pi;
  size_t new_pi_size = 0;
  char *new_waits = 0;
  size_t new_waits_len = 0;
  /* We always re-fetch any thread-specific info, as the set of threads could
     have changed since the last time we did this, and we can't tell.  */
  error_t err =
    fetch_procinfo (server, pid,
		    (need | (have & PSTAT_PROCINFO_THREAD)),
		    have & ~PSTAT_PROCINFO_THREAD,
		    &new_pi, &new_pi_size,
		    &new_waits, &new_waits_len);

  if (err)
    return err;

  if (*pi_size > 0)
    /* There was old information, try merging it. */
    {
      if (have & PSTAT_TASK_BASIC)
	/* Task info */
	bcopy (&(*pi)->taskinfo, &new_pi->taskinfo,
	       sizeof (struct task_basic_info));
      /* That's it for now.  */

      vm_deallocate (mach_task_self (), (vm_address_t)*pi, *pi_size);
    }
  if (*waits_len > 0)
    vm_deallocate (mach_task_self (), (vm_address_t)*waits, *waits_len);

  *pi = new_pi;
  *pi_size = new_pi_size;
  *waits = new_waits;
  *waits_len = new_waits_len;

  return 0;
}


/* ---------------------------------------------------------------- */

/* Returns FLAGS augmented with any other flags that are necessary
   preconditions to setting them.  */
static ps_flags_t 
add_preconditions (ps_flags_t flags, struct ps_context *context)
{
  /* Implement any inter-flag dependencies: if the new flags in FLAGS depend on
     some other set of flags to be set, make sure those are also in FLAGS. */

  if ((flags & PSTAT_USER_MASK)
      && context->user_hooks && context->user_hooks->dependencies)
    /* There are some user flags needing to be set...  See what they need.  */
    flags |= (*context->user_hooks->dependencies) (flags & PSTAT_USER_MASK);

  if (flags & PSTAT_TTY)
    flags |= PSTAT_CTTYID;
  if (flags & PSTAT_STATE)
    flags |= PSTAT_PROC_INFO | PSTAT_THREAD_BASIC;
  if (flags & PSTAT_OWNER)
    flags |= PSTAT_OWNER_UID;
  if (flags & PSTAT_OWNER_UID)
    flags |= PSTAT_PROC_INFO;
  if (flags & PSTAT_SUSPEND_COUNT)
    /* We just request the resources require for both the thread and task
       versions, as the extraneous info won't be possible to aquire anyway. */
    flags |= PSTAT_TASK_BASIC | PSTAT_THREAD_BASIC;
  if (flags & (PSTAT_CTTYID | PSTAT_CWDIR | PSTAT_AUTH | PSTAT_UMASK)
      && !(flags & PSTAT_NO_MSGPORT))
    {
      flags |= PSTAT_MSGPORT;
      flags |= PSTAT_TASK;	/* for authentication */
    }
  if (flags & PSTAT_TASK_EVENTS)
    flags |= PSTAT_TASK;

  return flags;
}

/* Those flags that should be set before calling should_suppress_msgport.  */
#define PSTAT_TEST_MSGPORT \
  (PSTAT_NUM_THREADS | PSTAT_SUSPEND_COUNT | PSTAT_THREAD_BASIC)

/* Those flags that need the msg port, perhaps implicitly.  */
#define PSTAT_USES_MSGPORT (PSTAT_MSGPORT | PSTAT_THREAD_WAIT)

/* Return true when there's some condition indicating that we shouldn't use
   PS's msg port.  For this routine to work correctly, PS's flags should
   contain as many flags in PSTAT_TEST_MSGPORT as possible.  */
static int
should_suppress_msgport (struct proc_stat *ps)
{
  ps_flags_t have = ps->flags;

  if ((have & PSTAT_SUSPEND_COUNT) && ps->suspend_count != 0)
    /* Task is suspended.  */
    return TRUE;

  if ((have & PSTAT_THREAD_BASIC) && ps->thread_basic_info->suspend_count != 0)
    /* All threads are suspended.  */
    return TRUE;

  if ((have & PSTAT_NUM_THREADS) && ps->num_threads == 0)
    /* No threads (some bug could cause the procserver still to think there's
       a msgport).  */
    return TRUE;

  return FALSE;
}

/* Returns FLAGS with PSTAT_MSGPORT turned off and PSTAT_NO_MSGPORT on.  */
#define SUPPRESS_MSGPORT_FLAGS(flags) \
   (((flags) & ~PSTAT_USES_MSGPORT) | PSTAT_NO_MSGPORT)

/* ---------------------------------------------------------------- */

/* Returns a new malloced struct thread_basic_info containing a summary of
   all the thread basic info in PI.  Sizes and cumulative times are summed,
   delta time are averaged.  The run_states are added by having running
   thread take precedence over waiting ones, and if there are any other
   incompatible states, simply using a bogus value of -1.  */
static struct thread_basic_info *
summarize_thread_basic_info (struct procinfo *pi)
{
  int i;
  unsigned num_threads = 0, num_run_threads = 0;
  thread_basic_info_t tbi = malloc (sizeof (struct thread_basic_info));
  int run_base_priority = 0, run_cur_priority = 0;
  int total_base_priority = 0, total_cur_priority = 0;

  if (!tbi)
    return 0;

  bzero (tbi, sizeof *tbi);

  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died)
      {
	thread_basic_info_t bi = &pi->threadinfos[i].pis_bi;
	int thread_run_state = bi->run_state;

	/* Construct some aggregate run-state for the process:  besides the
	   defined thread run_states, we use 0 to mean no threads, and -1
	   to mean two threads have conflicting run_stats.  */

	if (tbi->run_state == 0)
	  /* No prior state, just copy this thread's.  */
	  tbi->run_state = thread_run_state;
	else if (tbi->run_state == TH_STATE_RUNNING
		 || thread_run_state == TH_STATE_RUNNING)
	  /* If any thread is running, mark the process as running.  */
	  tbi->run_state = TH_STATE_RUNNING;
	else if (tbi->run_state != bi->run_state)
	  /* Otherwise there are two conflicting non-running states, so
	     just give up and say we don't know.  */
	  tbi->run_state = -1;

	tbi->cpu_usage += bi->cpu_usage;
	tbi->sleep_time += bi->sleep_time;

	/* The aggregate suspend count is the minimum of all threads.  */
	if (i == 0 || tbi->suspend_count > bi->suspend_count)
	  tbi->suspend_count = bi->suspend_count;

	tbi->user_time.seconds += bi->user_time.seconds;
	tbi->user_time.microseconds += bi->user_time.microseconds;
	tbi->system_time.seconds += bi->system_time.seconds;
	tbi->system_time.microseconds += bi->system_time.microseconds;

	if (tbi->run_state == TH_STATE_RUNNING)
	  {
	    run_base_priority += bi->base_priority;
	    run_cur_priority += bi->base_priority;
	    num_run_threads++;
	  }
	else
	  {
	    total_base_priority += bi->base_priority;
	    total_cur_priority += bi->base_priority;
	  }

	num_threads++;
      }

  if (num_threads > 0)
    {
      tbi->sleep_time /= num_threads;
      if (num_run_threads > 0)
	{
	  tbi->base_priority = run_base_priority / num_run_threads;
	  tbi->cur_priority = run_cur_priority / num_run_threads;
	}
      else
	{
	  tbi->base_priority = total_base_priority / num_threads;
	  tbi->cur_priority = total_cur_priority / num_threads;
	}
    }

  tbi->user_time.seconds += tbi->user_time.microseconds / 1000000;
  tbi->user_time.microseconds %= 1000000;
  tbi->system_time.seconds += tbi->system_time.microseconds / 1000000;
  tbi->system_time.microseconds %= 1000000;

  return tbi;
}

/* Returns a new malloced struct thread_sched_info containing a summary of
   all the thread scheduling info in PI.  The prioritys are an average of the
   thread priorities.  */
static struct thread_sched_info *
summarize_thread_sched_info (struct procinfo *pi)
{
  int i;
  unsigned num_threads = 0;
  thread_sched_info_t tsi = malloc (sizeof (struct thread_sched_info));

  if (!tsi)
    return 0;

  bzero (tsi, sizeof *tsi);

  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died)
      {
	thread_sched_info_t si = &pi->threadinfos[i].pis_si;
	tsi->base_priority += si->base_priority;
	tsi->cur_priority += si->cur_priority;
	tsi->max_priority += si->max_priority;
	tsi->depress_priority += si->depress_priority;
	num_threads++;
      }

  if (num_threads > 0)
    {
      tsi->base_priority /= num_threads;
      tsi->cur_priority /= num_threads;
      tsi->max_priority /= num_threads;
      tsi->depress_priority /= num_threads;
    }

  return tsi;
}

/* Returns the union of the state bits for all the threads in PI.  */
static int
summarize_thread_states (struct procinfo *pi)
{
  int i;
  int state = 0;

  /* The union of all thread state bits...  */
  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died)
      state |= thread_state (&pi->threadinfos[i].pis_bi);

  return state;
}

/* Returns what's blocking the first blocked thread in PI in WAIT and RPC.  */
static void
summarize_thread_waits (struct procinfo *pi, char *waits, size_t waits_len,
			char **wait, int *rpc)
{
  int i;
  char *next_wait = waits;

  *wait = 0;			/* Defaults */
  *rpc = 0;

  /* The union of all thread state bits...  */
  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died)
      if (next_wait > waits + waits_len)
	break;
      else if (*next_wait
	       && strncmp (next_wait, "msgport",
			   waits + waits_len - next_wait) != 0)
	{
	  *wait = next_wait;
	  *rpc = pi->threadinfos[i].rpc_block;
	}
      else
	next_wait++;
}

/* Returns the number of threads in PI that aren't marked dead.  */
static unsigned
count_threads (struct procinfo *pi)
{
  int i;
  unsigned num_threads = 0;

  /* The union of all thread state bits...  */
  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died)
      num_threads++;

  return num_threads;
}

typedef typeof (((struct procinfo *)0)->threadinfos[0]) *threadinfo_t;

/* Returns the threadinfo for the INDEX'th thread from PI that isn't marked
   dead.  */
threadinfo_t
get_thread_info (struct procinfo *pi, unsigned index)
{
  int i;

  /* The union of all thread state bits...  */
  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died && index-- == 0)
      return &pi->threadinfos[i];

  return 0;
}

/* Returns a pointer to the Nth entry in the '\0'-separated vector of strings
   in ARGZ & ARGZ_LEN.  */
char *
get_thread_wait (char *waits, size_t waits_len, unsigned n)
{
  char *wait = waits;
  while (n && wait)
    if (wait >= waits + waits_len)
      wait = 0;
    else
      wait = memchr (wait, '\0', wait + waits_len - waits);
  return wait;
}

/* Returns a malloced block of memory SIZE bytes long, containing a copy of
   SRC.  */
static void *
clone (void *src, size_t size)
{
  void *dst = malloc (size);
  if (dst)
    bcopy (src, dst, size);
  return dst;
}

/* Add FLAGS to PS's flags, fetching information as necessary to validate
   the corresponding fields in PS.  Afterwards you must still check the flags
   field before using new fields, as something might have failed.  Returns
   a system error code if a fatal error occurred, or 0 if none.  */
error_t
proc_stat_set_flags (struct proc_stat *ps, ps_flags_t flags)
{
  ps_flags_t have = ps->flags;	/* flags set in ps */
  ps_flags_t need;		/* flags not set in ps, but desired to be */
  ps_flags_t no_msgport_flags;	/* a version of FLAGS for use when we can't
				   use the msg port.  */
  process_t server = ps_context_server (ps->context);

  /* Turn off use of the msg port if we decide somewhere along the way that
     it's hosed.  */
  void suppress_msgport ()
    {
      /* Turn off those things that were only good given the msg port.  */
      need &= ~(flags & ~no_msgport_flags);
      have = SUPPRESS_MSGPORT_FLAGS (have);
    }

  flags &= ~ps->failed;		/* Don't try to get things we can't.  */

  /* Propagate PSTAT_NO_MSGPORT.  */
  if (flags & PSTAT_NO_MSGPORT)
    have = SUPPRESS_MSGPORT_FLAGS (have);
  if (have & PSTAT_NO_MSGPORT)
    flags = SUPPRESS_MSGPORT_FLAGS (flags);

  no_msgport_flags =
    add_preconditions (SUPPRESS_MSGPORT_FLAGS (flags), ps->context);
  flags = add_preconditions (flags, ps->context);

  if (flags & PSTAT_USES_MSGPORT)
    /* Add in some values that we can use to determine whether the msgport
       shouldn't be used.  */
    flags |= add_preconditions (PSTAT_TEST_MSGPORT, ps->context);

  need = flags & ~have & ~ps->failed;

  /* MGET: If we're trying to set FLAG, and the preconditions PRECOND are set
     in the flags already, then eval CALL and set ERR to the result.
     If the resulting ERR is 0 add FLAG to the set of valid flags.  ERR is
     returned.  */
#define MGET(_flag, _precond, call) \
  ({ ps_flags_t flag = (_flag), precond = (_precond); \
     error_t err; \
     if (!(need & (flag)) || (have & (precond)) != (precond)) \
       err = 0; \
     else \
       { \
	 err = (call); \
	 if (!err) \
	   have |= flag; \
       } \
     err; \
   })

  /* A version of MGET specifically for the msg port, that turns off the msg
     port if a call to it times out.  It also implies a precondition of
     PSTAT_MSGPORT.  */
#define MP_MGET(flag, precond, call) \
  ({ error_t err = MGET (flag, (precond) | PSTAT_MSGPORT, call); \
     if (err) suppress_msgport (); \
     err; \
   })

  /* the process's struct procinfo as returned by proc_getprocinfo.  */
  if ((need & PSTAT_PROCINFO) & ~(have & PSTAT_PROCINFO))
    if (have & PSTAT_PID)
      {
	error_t err;

	if (!(have & PSTAT_PROCINFO))
	  /* Never got any before; zero out our pointers.  */
	  {
	    ps->proc_info = 0;
	    ps->proc_info_size = 0;
	    ps->thread_waits = 0;
	    ps->thread_waits_len = 0;
	  }

	err =
	  merge_procinfo (server, ps->pid, need, have,
			  &ps->proc_info, &ps->proc_info_size,
			  &ps->thread_waits, &ps->thread_waits_len);
	if (!err)
	  {
	    struct procinfo *pi = ps->proc_info;
	    ps_flags_t added =
	      (need & PSTAT_PROCINFO) & ~(have & PSTAT_PROCINFO);

	    have |= added;

	    /* Update dependent fields.  We redo these even if we've already
	       gotten them, as the information will be newer.  */
	    if (have & PSTAT_TASK_BASIC)
	      ps->task_basic_info = &pi->taskinfo;
	    if (have & PSTAT_THREAD_BASIC)
	      {
		if (! (added & PSTAT_THREAD_BASIC))
		  free (ps->thread_basic_info);
		ps->thread_basic_info = summarize_thread_basic_info (pi);
	      }
	    if (have & PSTAT_THREAD_SCHED)
	      {
		if (! (added & PSTAT_THREAD_SCHED))
		  free (ps->thread_sched_info);
		ps->thread_sched_info = summarize_thread_sched_info (pi);
	      }
	    if (have & PSTAT_THREAD_WAIT)
	      summarize_thread_waits (pi,
				      ps->thread_waits, ps->thread_waits_len,
				      &ps->thread_wait, &ps->thread_rpc);

	    if (have & PSTAT_PROCINFO_THREAD)
	      /* Any thread information automatically gets us this for free. */
	      {
		have |= PSTAT_NUM_THREADS;
		ps->num_threads = count_threads (pi);
	      }
	  }
      }
    else
      /* For a thread, we get use the proc_info from the containing process. */
      {
	struct proc_stat *origin = ps->thread_origin;
	ps_flags_t oflags = need & PSTAT_PROCINFO_THREAD;

	proc_stat_set_flags (origin, oflags);
	oflags = origin->flags;

	if (oflags & PSTAT_PROCINFO_THREAD)
	  /* Got some threadinfo at least.  */
	  {
	    threadinfo_t ti =
	      get_thread_info (origin->proc_info, ps->thread_index);

	    /* Now copy out the information for this particular thread from the
	       ORIGIN's list of thread information.  */

	    if ((need & PSTAT_THREAD_BASIC) && ! (have & PSTAT_THREAD_BASIC)
		&& (oflags & PSTAT_THREAD_BASIC)
		&& (ps->thread_basic_info =
		    clone (&ti->pis_bi, sizeof (struct thread_basic_info))))
	      have |= PSTAT_THREAD_BASIC;

	    if ((need & PSTAT_THREAD_SCHED) && ! (have & PSTAT_THREAD_SCHED)
		&& (oflags & PSTAT_THREAD_SCHED)
		&& (ps->thread_sched_info =
		    clone (&ti->pis_si, sizeof (struct thread_sched_info))))
	      have |= PSTAT_THREAD_SCHED;

	    if ((need & PSTAT_THREAD_WAIT) && ! (have & PSTAT_THREAD_WAIT)
		&& (oflags & PSTAT_THREAD_WAIT))
	      {
		ps->thread_wait =
		  get_thread_wait (origin->thread_waits,
				   origin->thread_waits_len,
				   ps->thread_index);
		if (ps->thread_wait)
		  {
		    ps->thread_rpc = ti->rpc_block;
		    have |= PSTAT_THREAD_WAIT;
		  }
	      }
	  }
      }

  if ((need & PSTAT_SUSPEND_COUNT)
      &&
      ((have & PSTAT_PID)
       ? (have & PSTAT_TASK_BASIC)
       : (have & PSTAT_THREAD_BASIC)))
    {
      if (have & PSTAT_PID)
	ps->suspend_count = ps->task_basic_info->suspend_count;
      else
	ps->suspend_count = ps->thread_basic_info->suspend_count;
      have |= PSTAT_SUSPEND_COUNT;
    }

  ps->flags = have;		/* should_suppress_msgport looks at them.  */
  if (should_suppress_msgport (ps))
    suppress_msgport ();

  /* The process's libc msg port (see <hurd/msg.defs>).  */
  MGET(PSTAT_MSGPORT, PSTAT_PID, proc_getmsgport (server, ps->pid, &ps->msgport));
  /* The process's process port.  */
  MGET(PSTAT_PROCESS, PSTAT_PID, proc_pid2proc (server, ps->pid, &ps->process));
  /* The process's task port.  */
  MGET(PSTAT_TASK, PSTAT_PID, proc_pid2task (server, ps->pid, &ps->task));

  /* VM statistics for the task.  See <mach/task_info.h>.  */
  if ((need & PSTAT_TASK_EVENTS) && (have & PSTAT_TASK))
    {
      ps->task_events_info = &ps->task_events_info_buf;
      ps->task_events_info_size = TASK_EVENTS_INFO_COUNT;
      if (task_info (ps->task, TASK_EVENTS_INFO,
		    (task_info_t)&ps->task_events_info,
		    &ps->task_events_info_size)
	  == 0)
	have |= PSTAT_TASK_EVENTS;
    }

  /* PSTAT_STATE_ bits for the process and all its threads.  */
  if ((need & PSTAT_STATE) && (have & (PSTAT_PROC_INFO | PSTAT_THREAD_BASIC)))
    {
      ps->state = 0;

      if (have & PSTAT_THREAD_BASIC)
	/* Thread states.  */
	if (have & PSTAT_THREAD)
	  ps->state |= thread_state (ps->thread_basic_info);
	else
	  /* For a process, we use the thread list instead of
	     PS->thread_basic_info because it contains more information.  */
	  ps->state |= summarize_thread_states (ps->proc_info);

      if (have & PSTAT_PROC_INFO)
	/* Process state.  */
	{
	  int pi_flags = ps->proc_info->state;
	  if (pi_flags & PI_STOPPED)
	    ps->state |= PSTAT_STATE_P_STOP;
	  if (pi_flags & PI_ZOMBIE)
	    ps->state |= PSTAT_STATE_P_ZOMBIE;
	  if (pi_flags & PI_SESSLD)
	    ps->state |= PSTAT_STATE_P_SESSLDR;
	  if (pi_flags & PI_LOGINLD)
	    ps->state |= PSTAT_STATE_P_LOGINLDR;
	  if (!(pi_flags & PI_EXECED))
	    ps->state |= PSTAT_STATE_P_FORKED;
	  if (pi_flags & PI_NOMSG)
	    ps->state |= PSTAT_STATE_P_NOMSG;
	  if (pi_flags & PI_NOPARENT)
	    ps->state |= PSTAT_STATE_P_NOPARENT;
	  if (pi_flags & PI_ORPHAN)
	    ps->state |= PSTAT_STATE_P_ORPHAN;
	  if (pi_flags & PI_TRACED)
	    ps->state |= PSTAT_STATE_P_TRACE;
	  if (pi_flags & PI_WAITING)
	    ps->state |= PSTAT_STATE_P_WAIT;
	  if (pi_flags & PI_GETMSG)
	    ps->state |= PSTAT_STATE_P_GETMSG;
	}

      have |= PSTAT_STATE;
    }

  /* The process's exec arguments */
  if ((need & PSTAT_ARGS) && (have & PSTAT_PID))
    {
      ps->args_len = 0;
      if (proc_getprocargs (server, ps->pid, &ps->args, &ps->args_len))
	ps->args_len = 0;
      else
	have |= PSTAT_ARGS;
    }

  /* The ctty id port; note that this is just a magic cookie;
     we use it to fetch a port to the actual terminal -- it's not useful for
     much else.  */
  MP_MGET (PSTAT_CTTYID, PSTAT_TASK,
	   ps_msg_get_init_port (ps->msgport, ps->task,
				 INIT_PORT_CTTYID, &ps->cttyid));

  /* A port to the process's current working directory.  */
  MP_MGET (PSTAT_CWDIR, PSTAT_TASK,
	   ps_msg_get_init_port (ps->msgport, ps->task,
				 INIT_PORT_CWDIR, &ps->cwdir));

  /* The process's auth port, which we can use to determine who the process
     is authenticated as.  */
  MP_MGET (PSTAT_AUTH, PSTAT_TASK,
	   ps_msg_get_init_port (ps->msgport, ps->task, INIT_PORT_AUTH,
				 &ps->auth));

  /* The process's umask, which controls which protection bits won't be set
     when creating a file.  */
  MP_MGET (PSTAT_UMASK, PSTAT_TASK,
	   ps_msg_get_init_int (ps->msgport, ps->task, INIT_UMASK,
				&ps->umask));

  if ((need & PSTAT_OWNER_UID) && (have & PSTAT_PROC_INFO))
    {
      if (ps->proc_info->state & PI_NOTOWNED)
	ps->owner_uid = -1;
      else
	ps->owner_uid = ps->proc_info->owner;
      have |= PSTAT_OWNER_UID;
    }

  /* A ps_user object for the process's owner.  */
  if ((need & PSTAT_OWNER) && (have & PSTAT_OWNER_UID))
    if (ps->owner_uid < 0)
      {
	ps->owner = 0;
	have |= PSTAT_OWNER;
      }
    else if (! ps_context_find_user (ps->context, ps->owner_uid, &ps->owner))
      have |= PSTAT_OWNER;

  /* A ps_tty for the process's controlling terminal, or NULL if it
     doesn't have one.  */
  if ((need & PSTAT_TTY) && (have & PSTAT_CTTYID))
    if (ps_context_find_tty_by_cttyid (ps->context, ps->cttyid, &ps->tty) == 0)
      have |= PSTAT_TTY;

  /* Update PS's flag state.  We haven't tried user flags yet, so don't mark
     them as having failed.  We do this before checking user bits so that the
     user fetch hook sees PS in a consistent state.  */
  ps->failed |= (need & ~PSTAT_USER_MASK) & ~have;
  ps->flags = have;

  need &= PSTAT_USER_MASK;	/* Only consider user bits now.  */
  if (need && ps->context->user_hooks && ps->context->user_hooks->fetch)
    /* There is some user state we need to fetch.  */
    {
      have |= (*ps->context->user_hooks->fetch) (ps, need, have);
      /* Update the flag state again having tried the user bits.  */
      ps->failed |= need & ~have;
      ps->flags = have;
    }

  return 0;
}

/* ---------------------------------------------------------------- */
/* Discard PS and any resources it holds.  */
void 
_proc_stat_free (ps)
     struct proc_stat *ps;
{
  if (ps->context->user_hooks && ps->context->user_hooks->cleanup)
    /* Free any user state.  */
    (*ps->context->user_hooks->cleanup) (ps);

  /* Free the mach port PORT if FLAG is set in PS's flags.  */
#define MFREEPORT(flag, port) \
    ((ps->flags & (flag)) \
     ? mach_port_deallocate(mach_task_self (), (ps->port)) : 0)

  /* If FLAG is set in PS's flags, vm_deallocate MEM, consisting of SIZE 
     elements of type ELTYPE, *unless* MEM == SBUF, which usually means
     that MEM points to a static buffer somewhere instead of vm_alloc'd
     memory.  */
#define MFREEVM(flag, mem, size, sbuf, eltype) \
    (((ps->flags & (flag)) && ps->mem != sbuf) \
     ? (VMFREE(ps->mem, ps->size * sizeof (eltype))) : 0)

  /* if FLAG is set in PS's flags, free the malloc'd memory MEM. */
#define MFREEM(flag, mem) ((ps->flags & (flag)) ? free (ps->mem) : 0)

  /* free PS's ports */
  MFREEPORT (PSTAT_PROCESS, process);
  MFREEPORT (PSTAT_TASK, task);
  MFREEPORT (PSTAT_MSGPORT, msgport);
  MFREEPORT (PSTAT_CTTYID, cttyid);
  MFREEPORT (PSTAT_CWDIR, cwdir);
  MFREEPORT (PSTAT_AUTH, auth);

  /* free any allocated memory pointed to by PS */
  MFREEVM (PSTAT_PROCINFO, proc_info, proc_info_size, 0, char);
  MFREEM (PSTAT_THREAD_BASIC, thread_basic_info);
  MFREEM (PSTAT_THREAD_SCHED, thread_sched_info);
  MFREEVM (PSTAT_ARGS, args, args_len, 0, char);
  MFREEVM (PSTAT_TASK_EVENTS,
	  task_events_info, task_events_info_size,
	  &ps->task_events_info_buf, char);

  FREE (ps);
}

/* Returns in PS a new proc_stat for the process PID at the process context
   CONTEXT.  If a memory allocation error occurs, ENOMEM is returned,
   otherwise 0.  */
error_t
_proc_stat_create (pid_t pid, struct ps_context *context, struct proc_stat **ps)
{
  *ps = NEW (struct proc_stat);
  if (*ps == NULL)
    return ENOMEM;

  (*ps)->pid = pid;
  (*ps)->flags = PSTAT_PID;
  (*ps)->failed = 0;
  (*ps)->context = context;
  (*ps)->hook = 0;

  return 0;
}

/* ---------------------------------------------------------------- */

/* Returns in THREAD_PS a proc_stat for the Nth thread in the proc_stat
   PS (N should be between 0 and the number of threads in the process).  The
   resulting proc_stat isn't fully functional -- most flags can't be set in
   it.  It also contains a pointer to PS, so PS shouldn't be freed without
   also freeing THREAD_PS.  If N was out of range, EINVAL is returned.  If a
   memory allocation error occured, ENOMEM is returned.  Otherwise, 0 is
   returned.  */
error_t
proc_stat_thread_create (struct proc_stat *ps, unsigned index, struct proc_stat **thread_ps)
{
  error_t err = proc_stat_set_flags (ps, PSTAT_NUM_THREADS);

  if (err)
    return err;
  else if (index >= ps->num_threads)
    return EINVAL;
  else
    {
      struct proc_stat *tps = NEW (struct proc_stat);

      if (tps == NULL)
	return ENOMEM;

      /* A value of -1 for the PID indicates that this is a thread.  */
      tps->pid = -1;
      tps->flags = PSTAT_THREAD;

      tps->thread_origin = ps;
      tps->thread_index = index;

      tps->context = ps->context;

      *thread_ps = tps;

      return 0;
    }
}
