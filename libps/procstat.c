/* The proc_stat type, which holds information about a hurd process.

   Copyright (C) 1995,96,97,98,99,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <assert-backtrace.h>
#include <string.h>

#include "ps.h"
#include "common.h"

#include "ps_msg.h"

/* ---------------------------------------------------------------- */

/* These directly correspond to the bits in a state, starting from 0.  See
   ps.h for an explanation of what each of these means.  */
char *proc_stat_state_tags = "TZRHDSIN<u+slfmpoxwg";

/* ---------------------------------------------------------------- */

/* The type of the per-thread data returned by proc_getprocinfo.  */
typedef typeof (((struct procinfo *)0)->threadinfos[0]) threadinfo_data_t;
typedef threadinfo_data_t *threadinfo_t;

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

  if (bi->base_priority < 25)
    state |= PSTAT_STATE_T_NASTY;
  else if (bi->base_priority > 25)
    state |= PSTAT_STATE_T_NICE;

  return state;
}

/* ---------------------------------------------------------------- */

/* The set of things we get from procinfo that are per-thread.  */
#define PSTAT_PROCINFO_THREAD \
 (PSTAT_THREAD_BASIC | PSTAT_THREAD_SCHED | PSTAT_THREAD_WAIT)

/* The set of things we get from procinfo that are per-task, and thread dependent. */
#define PSTAT_PROCINFO_TASK_THREAD_DEP \
 (PSTAT_PROCINFO_THREAD | PSTAT_NUM_THREADS | PSTAT_THREAD_WAITS)

/* The set of things we get from procinfo that are per-task (note that this
   includes thread fields, because tasks use them for thread summaries).  */
#define PSTAT_PROCINFO_TASK \
 (PSTAT_PROCINFO_TASK_THREAD_DEP | PSTAT_PROC_INFO \
  | PSTAT_TASK_BASIC | PSTAT_TASK_EVENTS)

/* The set of PSTAT_ flags that we get using proc_getprocinfo.  */
#define PSTAT_PROCINFO PSTAT_PROCINFO_TASK

/* The set of things in PSTAT_PROCINFO that we will not attempt to refetch on
   subsequent getprocinfo calls.  */
#define PSTAT_PROCINFO_MERGE    (PSTAT_TASK_BASIC | PSTAT_TASK_EVENTS)
#define PSTAT_PROCINFO_REFETCH  (PSTAT_PROCINFO - PSTAT_PROCINFO_MERGE)

/* Fetches process information from the set in PSTAT_PROCINFO, returning it
   in PI & PI_SIZE.  NEED is the information, and HAVE is the what we already
   have.  */
static error_t
fetch_procinfo (process_t server, pid_t pid,
		ps_flags_t need, ps_flags_t *have,
		struct procinfo **pi, size_t *pi_size,
		char **waits, size_t *waits_len)
{
  static const struct { ps_flags_t ps_flag; int pi_flags; } map[] =
  {
    { PSTAT_TASK_BASIC,     PI_FETCH_TASKINFO				},
    { PSTAT_TASK_EVENTS,    PI_FETCH_TASKEVENTS				},
    { PSTAT_NUM_THREADS,    PI_FETCH_THREADS				},
    { PSTAT_THREAD_BASIC,   PI_FETCH_THREAD_BASIC | PI_FETCH_THREADS	},
    { PSTAT_THREAD_SCHED,   PI_FETCH_THREAD_SCHED | PI_FETCH_THREADS	},
    { PSTAT_THREAD_WAITS,   PI_FETCH_THREAD_WAITS | PI_FETCH_THREADS	},
    { 0, }
  };
  int pi_flags = 0;
  int i;

  for (i = 0; map[i].ps_flag; i++)
    if ((need & map[i].ps_flag) && !(*have & map[i].ps_flag))
      pi_flags |= map[i].pi_flags;

  if (pi_flags || ((need & PSTAT_PROC_INFO) && !(*have & PSTAT_PROC_INFO)))
    {
      error_t err;

      *pi_size /= sizeof (int);	/* getprocinfo takes an array of ints.  */
      err = proc_getprocinfo (server, pid, &pi_flags,
			      (procinfo_t *)pi, pi_size, waits, waits_len);
      *pi_size *= sizeof (int);

      if (! err)
	/* Update *HAVE to reflect what we've successfully fetched.  */
	{
	  *have |= PSTAT_PROC_INFO;
	  for (i = 0; map[i].ps_flag; i++)
	    if ((pi_flags & map[i].pi_flags) == map[i].pi_flags)
	      *have |= map[i].ps_flag;
	}
      return err;
    }
  else
    return 0;
}

/* The size of the initial buffer malloced to try and avoid getting
   vm_alloced memory for the procinfo structure returned by getprocinfo.
   Here we just give enough for four threads.  */
#define PROCINFO_MALLOC_SIZE \
  (sizeof (struct procinfo) + 4 * sizeof (threadinfo_data_t))

#define WAITS_MALLOC_SIZE	128

/* Fetches process information from the set in PSTAT_PROCINFO, returning it
   in PI & PI_SIZE, and if *PI_SIZE is non-zero, merges the new information
   with what was in *PI, and deallocates *PI.  NEED is the information, and
   *HAVE is the what we already have (which will be updated).  */
static ps_flags_t
merge_procinfo (struct proc_stat *ps, ps_flags_t need, ps_flags_t have)
{
  error_t err;
  struct procinfo *new_pi, old_pi_hdr;
  size_t new_pi_size;
  char *new_waits = 0;
  size_t new_waits_len = 0;
  /* We always re-fetch any thread-specific info, as the set of threads could
     have changed since the last time we did this, and we can't tell.  */
  ps_flags_t really_need = need | (have & PSTAT_PROCINFO_REFETCH);
  ps_flags_t really_have = have & ~PSTAT_PROCINFO_REFETCH;

  /* Give NEW_PI, the default buffer to receive procinfo data, some storage. */
  if (have & PSTAT_PROCINFO)
    /* We already have some procinfo stuff, so try to reuse its storage,
       first saving the old values.  We know that below we'll never try to
       merge anything beyond the static struct procinfo header, so just save
       that.  */
    old_pi_hdr = *ps->proc_info;
  else
    /* The first time we're getting procinfo stuff.  Malloc a block that's
       probably big enough for everything.  */
    {
      ps->proc_info = malloc (PROCINFO_MALLOC_SIZE);
      ps->proc_info_size = PROCINFO_MALLOC_SIZE;
      ps->proc_info_vm_alloced = 0;
      if (! ps->proc_info)
	return ENOMEM;
    }
  new_pi = ps->proc_info;
  new_pi_size = ps->proc_info_size;

  if (really_need & PSTAT_THREAD_WAITS)
    /* We're going to get thread waits info, so make some storage for it too.*/
    {
      if (! (have & PSTAT_THREAD_WAITS))
	{
	  ps->thread_waits = malloc (WAITS_MALLOC_SIZE);
	  ps->thread_waits_len = WAITS_MALLOC_SIZE;
	  ps->thread_waits_vm_alloced = 0;
	}
      new_waits = ps->thread_waits;
      new_waits_len = ps->thread_waits_len;
    }

  err = fetch_procinfo (ps->context->server, ps->pid, really_need, &really_have,
			&new_pi, &new_pi_size,
			&new_waits, &new_waits_len);
  if (err)
    /* Just keep what we had before.  If that was nothing, we have to free
       the first-time storage we malloced.  */
    {
      if (! (have & PSTAT_PROCINFO))
	free (new_pi);
      if ((really_need & PSTAT_THREAD_WAITS) && !(have & PSTAT_THREAD_WAITS))
	free (new_waits);
      return have;
    }

  /* There was old information, try merging it. */
  if (have & PSTAT_TASK_BASIC)
    /* Task info.  */
    memcpy (&new_pi->taskinfo, &old_pi_hdr.taskinfo,
	    sizeof (struct task_basic_info));
  if (have & PSTAT_TASK_EVENTS)
    /* Event info. */
    memcpy (&new_pi->taskevents, &old_pi_hdr.taskevents,
	    sizeof (struct task_events_info));
  /* That's it for now.  */

  if (new_pi != ps->proc_info)
    /* We got new memory vm_alloced by the getprocinfo, discard the old.  */
    {
      if (ps->proc_info_vm_alloced)
	munmap (ps->proc_info, ps->proc_info_size);
      else
	free (ps->proc_info);
      ps->proc_info = new_pi;
      ps->proc_info_size = new_pi_size;
      ps->proc_info_vm_alloced = 1;
    }

  if (really_need & PSTAT_THREAD_WAITS)
    /* We were trying to get thread waits....  */
    {
      if (! (really_have & PSTAT_THREAD_WAITS))
	/* Failed to do so.  Make sure we deallocate memory for it.  */
	new_waits = 0;
      if (new_waits != ps->thread_waits)
	/* We got new memory vm_alloced by the getprocinfo, discard the old. */
	{
	  if (ps->thread_waits_vm_alloced)
	    munmap (ps->thread_waits, ps->thread_waits_len);
	  else
	    free (ps->thread_waits);
	  ps->thread_waits = new_waits;
	  ps->thread_waits_len = new_waits_len;
	  ps->thread_waits_vm_alloced = 1;
	}
    }

  /* Return what thread info we have -- this may *decrease*, as all
     previously fetched thread-info is out-of-date now, so we have to make do
     with whatever we've fetched this time.  */
  return really_have;
}

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
       versions, as the extraneous info won't be possible to acquire anyway. */
    flags |= PSTAT_TASK_BASIC | PSTAT_THREAD_BASIC;
  if (flags & PSTAT_TIMES)
    flags |= PSTAT_TASK_BASIC | PSTAT_THREAD_BASIC;
  if (flags & (PSTAT_CTTYID | PSTAT_CWDIR | PSTAT_AUTH | PSTAT_UMASK)
      && !(flags & PSTAT_NO_MSGPORT))
    {
      flags |= PSTAT_MSGPORT;
      flags |= PSTAT_TASK;	/* for authentication */
    }

  return flags;
}

/* Those flags that should be set before calling should_suppress_msgport.  */
#define PSTAT_TEST_MSGPORT \
  (PSTAT_NUM_THREADS | PSTAT_SUSPEND_COUNT | PSTAT_THREAD_BASIC)

/* Those flags that need the msg port, perhaps implicitly.  */
#define PSTAT_USES_MSGPORT \
  (PSTAT_MSGPORT | PSTAT_THREAD_WAIT | PSTAT_THREAD_WAITS)

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
summarize_thread_basic_info (struct procinfo *pi, ps_flags_t have)
{
  int i;
  unsigned num_threads = 0, num_run_threads = 0;
  thread_basic_info_t tbi = malloc (sizeof (struct thread_basic_info));
  int run_base_priority = 0, run_cur_priority = 0;
  int total_base_priority = 0, total_cur_priority = 0;

  if (!tbi)
    return 0;

  memset (tbi, 0, sizeof *tbi);

  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died
	&& ! (pi->threadinfos[i].pis_bi.flags & TH_FLAGS_IDLE))
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

  /* For tasks, include the run time of terminated threads.  */
  if (have & PSTAT_TASK_BASIC)
    {
      tbi->user_time.seconds += pi->taskinfo.user_time.seconds;
      tbi->user_time.microseconds += pi->taskinfo.user_time.microseconds;
      tbi->system_time.seconds += pi->taskinfo.system_time.seconds;
      tbi->system_time.microseconds += pi->taskinfo.system_time.microseconds;
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

  memset (tsi, 0, sizeof *tsi);

  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died
	&& ! (pi->threadinfos[i].pis_bi.flags & TH_FLAGS_IDLE))
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
    if (! pi->threadinfos[i].died
	&& ! (pi->threadinfos[i].pis_bi.flags & TH_FLAGS_IDLE))
      state |= thread_state (&pi->threadinfos[i].pis_bi);

  return state;
}

/* Returns what's blocking the first blocked thread in PI in WAIT and RPC.  */
static void
summarize_thread_waits (struct procinfo *pi, char *waits, size_t waits_len,
			char **wait, mach_msg_id_t *rpc)
{
  int i;
  char *next_wait = waits;

  *wait = 0;			/* Defaults */
  *rpc = 0;

  for (i = 0; i < pi->nthreads; i++)
    if (! pi->threadinfos[i].died)
      {
	if (next_wait > waits + waits_len)
	  break;
	else
	  {
	    int left = waits + waits_len - next_wait;

	    if (pi->threadinfos[i].pis_bi.flags & TH_FLAGS_IDLE)
	      ;			/* kernel idle thread; ignore */
	    else if (strncmp (next_wait, "msgport", left) == 0
		     || strncmp (next_wait, "itimer", left) == 0)
	      ;		/* libc internal threads; ignore.  */
	    else if (*wait)
	      /* There are multiple user threads.  Punt.  */
	      {
		*wait = "*";
		*rpc = 0;
		break;
	      }
	    else
	      {
		*wait = next_wait;
		*rpc = pi->threadinfos[i].rpc_block;
	      }

	    /* Advance NEXT_WAIT to the next wait string.  */
	    next_wait += strnlen (next_wait, left) + 1;
	  }
      }
}

/* Returns the number of threads in PI that aren't marked dead.  */
static unsigned
count_threads (struct procinfo *pi, ps_flags_t have)
{
  if (have & (PSTAT_PROCINFO_TASK_THREAD_DEP & ~PSTAT_NUM_THREADS))
    /* If we have thread info besides the number of threads, then the
       threadinfos structures in PI are valid (we use the died bit).  */
    {
      int i;
      unsigned num_threads = 0;

      /* The union of all thread state bits...  */
      for (i = 0; i < pi->nthreads; i++)
	if (! pi->threadinfos[i].died)
	  num_threads++;

      return num_threads;
    }
  else
    /* Otherwise just use the number proc gave us.  */
    return pi->nthreads;
}

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
   in ARGZ & ARGZ_LEN.  Note that we don't have to do the bit with only
   counting non-dead threads like get_thread_info does, because the
   thread_waits string vector only contains entries for live threads.  */
char *
get_thread_wait (char *waits, size_t waits_len, unsigned n)
{
  char *wait = waits;
  while (n-- && wait)
    if (wait >= waits + waits_len)
      wait = 0;
    else
      wait += strnlen (wait, waits + waits_len - wait) + 1;
  return wait;
}

/* Returns a malloced block of memory SIZE bytes long, containing a copy of
   SRC.  */
static void *
clone (void *src, size_t size)
{
  void *dst = malloc (size);
  if (dst)
    memcpy (dst, src, size);
  return dst;
}

/* Add the information specified by NEED to PS which we can get with
   proc_getprocinfo.  */
static ps_flags_t
set_procinfo_flags (struct proc_stat *ps, ps_flags_t need, ps_flags_t have)
{
  if (have & PSTAT_PID)
    {
      struct procinfo *pi;
      ps_flags_t had = have;

      if (! (have & PSTAT_PROCINFO))
	/* Never got any before; zero out our pointers.  */
	{
	  ps->proc_info = 0;
	  ps->proc_info_size = 0;
	  ps->thread_waits = 0;
	  ps->thread_waits_len = 0;
	}

      if ((need & PSTAT_THREAD_WAIT) && !(need & PSTAT_THREAD_WAITS))
	/* We need thread wait information only for summarization.  This is
	   expensive and pointless for lots of threads, so try to avoid it
	   in that case.  */
	{
	  if (! (have & PSTAT_NUM_THREADS))
	    /* We've don't know how many threads there are yet; find out. */
	    {
	      have = merge_procinfo (ps, PSTAT_NUM_THREADS, have);
	      if (have & PSTAT_NUM_THREADS)
		ps->num_threads = count_threads (ps->proc_info, have);
	    }
	  if ((have & PSTAT_NUM_THREADS) && ps->num_threads <= 3)
	    /* Perhaps only 1 user thread -- thread-wait info may be
	       meaningful!  */
	    need |= PSTAT_THREAD_WAITS;
	}

      have = merge_procinfo (ps, need, have);
      pi = ps->proc_info;

      /* Update dependent fields.  We redo these even if we've already
	 gotten them, as the information will be newer.  */
      if (have & PSTAT_TASK_BASIC)
	ps->task_basic_info = &pi->taskinfo;
      if (have & PSTAT_TASK_EVENTS)
	ps->task_events_info = &pi->taskevents;
      if (have & PSTAT_NUM_THREADS)
	ps->num_threads = count_threads (pi, have);
      if (had & PSTAT_THREAD_BASIC)
	free (ps->thread_basic_info);
      if (have & PSTAT_THREAD_BASIC)
	ps->thread_basic_info = summarize_thread_basic_info (pi, have);
      if (had & PSTAT_THREAD_SCHED)
	free (ps->thread_sched_info);
      if (have & PSTAT_THREAD_SCHED)
	ps->thread_sched_info = summarize_thread_sched_info (pi);
      if (have & PSTAT_THREAD_WAITS)
	/* Thread-waits info can be used to generate thread-wait info. */
	{
	  summarize_thread_waits (pi,
				  ps->thread_waits, ps->thread_waits_len,
				  &ps->thread_wait, &ps->thread_rpc);
	  have |= PSTAT_THREAD_WAIT;
	}
      else if (!(have & PSTAT_NO_MSGPORT)
	       && (have & PSTAT_NUM_THREADS) && ps->num_threads > 3)
	/* More than 3 threads (1 user thread + libc signal thread +
	   possible itimer thread) always results in this value for the
	   process's thread_wait field.  For fewer threads, we should
	   have fetched thread_waits info and hit the previous case.  */
	{
	  ps->thread_wait = "*";
	  ps->thread_rpc = 0;
	  have |= PSTAT_THREAD_WAIT;
	}
    }
  else
    /* For a thread, we get use the proc_info from the containing process. */
    {
      struct proc_stat *origin = ps->thread_origin;
      /* Fetch for the containing process basically the same information we
	 want for the thread, but it also needs all the thread wait info.  */
      ps_flags_t oflags =
	(need & PSTAT_PROCINFO_THREAD)
	  | ((need & PSTAT_THREAD_WAIT) ? PSTAT_THREAD_WAITS : 0);

      proc_stat_set_flags (origin, oflags);
      oflags = origin->flags;

      if (oflags & PSTAT_PROCINFO_THREAD)
	/* Got some threadinfo at least.  */
	{
	  threadinfo_t ti =
	    get_thread_info (origin->proc_info, ps->thread_index);

	  /* Now copy out the information for this particular thread from the
	     ORIGIN's list of thread information.  */

	  need &= ~have;

	  if ((need & PSTAT_THREAD_BASIC) && (oflags & PSTAT_THREAD_BASIC)
	      && (ps->thread_basic_info =
		  clone (&ti->pis_bi, sizeof (struct thread_basic_info))))
	    have |= PSTAT_THREAD_BASIC;

	  if ((need & PSTAT_THREAD_SCHED) && (oflags & PSTAT_THREAD_SCHED)
	      && (ps->thread_sched_info =
		  clone (&ti->pis_si, sizeof (struct thread_sched_info))))
	    have |= PSTAT_THREAD_SCHED;

	  if ((need & PSTAT_THREAD_WAIT) && (oflags & PSTAT_THREAD_WAITS))
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

      /* Mark things that don't apply to threads (note that we don't do the
	 analogous thing for tasks above, as tasks do have thread fields
	 containing summary information for all their threads).  */
      ps->inapp |= need & ~have & PSTAT_PROCINFO & ~PSTAT_PROCINFO_THREAD;
    }

  return have;
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
  ps_flags_t test_msgport_flags; /* Flags needed to test for msgport
				    validity, including any preconditions.  */
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
    {
      test_msgport_flags = add_preconditions (PSTAT_TEST_MSGPORT, ps->context);
      flags |= test_msgport_flags;
    }
  else
    test_msgport_flags = 0;

  need = flags & ~have & ~ps->failed;

  /* Returns true if (1) FLAGS is in NEED, and (2) the appropriate
     preconditions PRECOND are available; if only (1) is true, FLAG is added
     to the INAPP set if appropriate (to distinguish it from an error), and
     returns false.  */
#define NEED(flag, precond)						      \
  ({									      \
    ps_flags_t __flag = (flag), _precond = (precond);			      \
    int val;								      \
    if (! (__flag & need))						      \
      val = 0;								      \
    else if ((_precond & have) == _precond)				      \
      val = 1;								      \
    else								      \
      {									      \
	val = 0;							      \
	if (_precond & ps->inapp)					      \
	  ps->inapp |= __flag;						      \
      }									      \
    val;								      \
  })

  /* MGET: If we're trying to set FLAG, and the preconditions PRECOND are set
     in the flags already, then eval CALL and set ERR to the result.
     If the resulting ERR is 0 add FLAG to the set of valid flags.  ERR is
     returned.  */
#define MGET(flag, precond, call)					      \
  ({									      \
    error_t err;							      \
    ps_flags_t _flag = (flag);						      \
    if (NEED (_flag, precond))						      \
      {									      \
	err = (call);							      \
	if (!err)							      \
	  have |= _flag;						      \
      }									      \
    else								      \
      err = 0;								      \
    err;								      \
  })

  /* A version of MGET specifically for the msg port, that turns off the msg
     port if a call to it times out.  It also implies a precondition of
     PSTAT_MSGPORT.  */
#define MP_MGET(flag, precond, call) \
  ({ error_t err = MGET (flag, (precond) | PSTAT_MSGPORT, call); \
     if (err == EMACH_RCV_TIMED_OUT) suppress_msgport (); \
     err; \
   })

  if (need & ~have & test_msgport_flags & PSTAT_PROCINFO)
    /* Pre-fetch information returned by set_procinfo_flags that we need for
       msgport validity testing; if we need other procinfo stuff, we get that
       later.  */
    have = set_procinfo_flags (ps, need & ~have & test_msgport_flags, have);

  if (NEED (PSTAT_SUSPEND_COUNT,
	    ((have & PSTAT_PID) ? PSTAT_TASK_BASIC : PSTAT_THREAD_BASIC)))
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

  /* the process's struct procinfo as returned by proc_getprocinfo.  */
  if (need & ~have & PSTAT_PROCINFO)
    have = set_procinfo_flags (ps, need, have);

  /* The process's libc msg port (see <hurd/msg.defs>).  */
  MGET(PSTAT_MSGPORT, PSTAT_PID, proc_getmsgport (server, ps->pid, &ps->msgport));
  /* The process's process port.  */
  MGET(PSTAT_PROCESS, PSTAT_PID, proc_pid2proc (server, ps->pid, &ps->process));
  /* The process's task port.  */
  MGET(PSTAT_TASK, PSTAT_PID, proc_pid2task (server, ps->pid, &ps->task));

  /* PSTAT_STATE_ bits for the process and all its threads.  */
  if ((need & PSTAT_STATE) && (have & (PSTAT_PROC_INFO | PSTAT_THREAD_BASIC)))
    {
      ps->state = 0;

      if (have & PSTAT_THREAD_BASIC)
	{
	  /* Thread states.  */
	  if (have & PSTAT_THREAD)
	    ps->state |= thread_state (ps->thread_basic_info);
	  else
	    /* For a process, we use the thread list instead of
	       PS->thread_basic_info because it contains more information.  */
	    ps->state |= summarize_thread_states (ps->proc_info);
	}

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
  if (NEED (PSTAT_ARGS, PSTAT_PID))
    {
      char *buf = malloc (100);
      ps->args_len = 100;
      ps->args = buf;
      if (ps->args)
	{
	  if (proc_getprocargs (server, ps->pid, &ps->args, &ps->args_len))
	    free (buf);
	  else
	    {
	      have |= PSTAT_ARGS;
	      ps->args_vm_alloced = (ps->args != buf);
	      if (ps->args_vm_alloced)
		free (buf);
	    }
	}
    }

  /* The process's exec environment */
  if (NEED (PSTAT_ENV, PSTAT_PID))
    {
      char *buf = malloc (100);
      ps->env_len = 100;
      ps->env = buf;
      if (ps->env)
	{
	  if (proc_getprocenv (server, ps->pid, &ps->env, &ps->env_len))
	    free (buf);
	  else
	    {
	      have |= PSTAT_ENV;
	      ps->env_vm_alloced = (ps->env != buf);
	      if (ps->env_vm_alloced)
		free (buf);
	    }
	}
    }

  /* The process's path to binary executable */
  if (NEED (PSTAT_EXE, PSTAT_PID))
    {
      ps->exe = malloc (sizeof(string_t));
      if (ps->exe)
	{
	  if (proc_get_exe (server, ps->pid, ps->exe))
	    free (ps->exe);
	  else
	    {
	      ps->exe_len = strlen(ps->exe);
	      have |= PSTAT_EXE;
	      ps->exe_vm_alloced = 0;
	    }
	}
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
				(int *) &ps->umask));

  if (NEED (PSTAT_OWNER_UID, PSTAT_PROC_INFO))
    {
      if (ps->proc_info->state & PI_NOTOWNED)
	ps->owner_uid = -1;
      else
	ps->owner_uid = ps->proc_info->owner;
      have |= PSTAT_OWNER_UID;
    }

  /* A ps_user object for the process's owner.  */
  if (NEED (PSTAT_OWNER, PSTAT_OWNER_UID))
    {
      if (ps->owner_uid < 0)
	{
	  ps->owner = 0;
	  have |= PSTAT_OWNER;
	}
      else if (! ps_context_find_user (ps->context, ps->owner_uid, &ps->owner))
	have |= PSTAT_OWNER;
    }

  /* A ps_tty for the process's controlling terminal, or NULL if it
     doesn't have one.  */
  if (NEED (PSTAT_TTY, PSTAT_CTTYID))
    if (ps_context_find_tty_by_cttyid (ps->context, ps->cttyid, &ps->tty) == 0)
      have |= PSTAT_TTY;

  /* The number of Mach ports in the task. */
  MGET (PSTAT_NUM_PORTS, PSTAT_PID,
        proc_getnports (server, ps->pid, &ps->num_ports));

  /* User and system times.  */
  if ((need & PSTAT_TIMES) && (have & (PSTAT_TASK_BASIC | PSTAT_THREAD_BASIC)))
    have |= PSTAT_TIMES;

  /* Update PS's flag state.  We haven't tried user flags yet, so don't mark
     them as having failed.  We do this before checking user bits so that the
     user fetch hook sees PS in a consistent state.  */
  ps->failed |= (need & ~PSTAT_USER_MASK) & ~have;
  ps->flags = have;

  need &= ~have;
  if (need && ps->context->user_hooks && ps->context->user_hooks->fetch)
    /* There is some user state we need to fetch.  */
    {
      have |= (*ps->context->user_hooks->fetch) (ps, need, have);
      /* Update the flag state again having tried the user bits.  We allow
	 the user hook to turn on non-user bits, in which case we remove them
	 from the failed set; the user hook may know some way of getting the
	 info that we don't.  */
      ps->failed = (ps->failed | need) & ~have;
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

  /* If FLAG is set in PS's flags, then if VM_ALLOCED is zero, free the malloced
     field MEM in PS; othrewise, vm_deallocate MEM, consisting of SIZE
     elements of type ELTYPE, *unless* MEM == SBUF, which usually means
     that MEM points to a static buffer somewhere instead of vm_alloc'd
     memory.  */
#define MFREEMEM(flag, mem, size, vm_alloced, sbuf, eltype) \
    (((ps->flags & (flag)) && ps->mem != sbuf) \
     ? (vm_alloced ? (VMFREE(ps->mem, size * sizeof (eltype))) : free (ps->mem)) : 0)

  /* free PS's ports */
  MFREEPORT (PSTAT_PROCESS, process);
  MFREEPORT (PSTAT_TASK, task);
  MFREEPORT (PSTAT_MSGPORT, msgport);
  MFREEPORT (PSTAT_CTTYID, cttyid);
  MFREEPORT (PSTAT_CWDIR, cwdir);
  MFREEPORT (PSTAT_AUTH, auth);

  /* free any allocated memory pointed to by PS */
  MFREEMEM (PSTAT_PROC_INFO, proc_info, ps->proc_info_size,
	    ps->proc_info_vm_alloced, 0, char);
  MFREEMEM (PSTAT_THREAD_BASIC, thread_basic_info, 0, 0, 0, 0);
  MFREEMEM (PSTAT_THREAD_SCHED, thread_sched_info, 0, 0, 0, 0);
  MFREEMEM (PSTAT_ARGS, args, ps->args_len, ps->args_vm_alloced, 0, char);
  MFREEMEM (PSTAT_ENV, env, ps->env_len, ps->env_vm_alloced, 0, char);
  MFREEMEM (PSTAT_TASK_EVENTS, task_events_info, ps->task_events_info_size,
	    0, &ps->task_events_info_buf, char);
  MFREEMEM (PSTAT_THREAD_WAITS, thread_waits, ps->thread_waits_len,
	    ps->thread_waits_vm_alloced, 0, char);

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
  (*ps)->inapp = PSTAT_THREAD;
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
   memory allocation error occurred, ENOMEM is returned.  Otherwise, 0 is
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
      tps->failed = 0;
      tps->inapp = PSTAT_PID;

      tps->thread_origin = ps;
      tps->thread_index = index;

      tps->context = ps->context;

      *thread_ps = tps;

      return 0;
    }
}
