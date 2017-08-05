/* The type proc_stat_list_t, which holds lists of proc_stats.

   Copyright (C) 1995,96,2002 Free Software Foundation, Inc.

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
#include <assert-backtrace.h>
#include <string.h>

#include "ps.h"
#include "common.h"

/* Creates a new proc_stat_list_t for processes from CONTEXT, which is
   returned in PP, and returns 0, or else returns ENOMEM if there wasn't
   enough memory.  */
error_t
proc_stat_list_create (struct ps_context *context, struct proc_stat_list **pp)
{
  *pp = NEW (struct proc_stat_list);
  if (*pp == NULL)
    return ENOMEM;

  (*pp)->proc_stats = 0;
  (*pp)->num_procs = 0;
  (*pp)->alloced = 0;
  (*pp)->context = context;

  return 0;
}

/* Free PP, and any resources it consumes.  */
void
proc_stat_list_free (struct proc_stat_list *pp)
{
  proc_stat_list_remove_threads (pp);
  FREE (pp->proc_stats);
  FREE (pp);
}

/* Returns a copy of PP in COPY, or an error.  */
error_t
proc_stat_list_clone (struct proc_stat_list *pp, struct proc_stat_list **copy)
{
  struct proc_stat_list *new = NEW (struct proc_stat_list);
  struct proc_stat **procs = NEWVEC (struct proc_stat *, pp->num_procs);

  if (!new || !procs)
    {
      free (new);
      free (procs);
      return ENOMEM;
    }

  memcpy (procs, pp->proc_stats, sizeof *procs * pp->num_procs);

  new->proc_stats = procs;
  new->num_procs = pp->num_procs;
  new->alloced = pp->num_procs;
  new->context = pp->context;
  *copy = new;

  return 0;
}

/* Make sure there are at least AMOUNT new locations allocated in PP's
   proc_stat array (but without changing NUM_PROCS).  Returns ENOMEM if a
   memory allocation error occurred, 0 otherwise.  */
static error_t
proc_stat_list_grow (struct proc_stat_list *pp, int amount)
{
  amount += pp->num_procs;

  if (amount > pp->alloced)
    {
      struct proc_stat **new_procs =
	GROWVEC (pp->proc_stats, struct proc_stat *, amount);

      if (new_procs == NULL)
	return ENOMEM;

      pp->alloced = amount;
      pp->proc_stats = new_procs;
    }

  return 0;
}

/* Add proc_stat entries to PP for each process with a process id in the
   array PIDS (where NUM_PROCS is the length of PIDS).  Entries are only
   added for processes not already in PP.  ENOMEM is returned if a memory
   allocation error occurs, otherwise 0.  PIDs is not referenced by the
   resulting proc_stat_list_t, and so may be subsequently freed.  If
   PROC_STATS is non-NULL, a malloced array NUM_PROCS entries long of the
   resulting proc_stats is returned in it.  */
error_t
proc_stat_list_add_pids (struct proc_stat_list *pp,
			 pid_t *pids, unsigned num_procs,
			 struct proc_stat ***proc_stats)
{
  error_t err = proc_stat_list_grow (pp, num_procs);

  if (err)
    return err;
  else
    {
      int i;
      struct proc_stat **end = pp->proc_stats + pp->num_procs;

      if (proc_stats)
	*proc_stats = NEWVEC (struct proc_stat *, num_procs);

      for (i = 0; i < num_procs; i++)
	{
	  int pid = *pids++;
	  struct proc_stat *ps = proc_stat_list_pid_proc_stat (pp, pid);

	  if (ps == NULL)
	    {
	      err = ps_context_find_proc_stat (pp->context, pid, end);
	      if (err)
		{
		  if (proc_stats)
		    free (*proc_stats);
		  return err;
		}
	      else
		ps = *end++;
	    }

	  if (proc_stats)
	    (*proc_stats)[i] = ps;
	}

      pp->num_procs = end - pp->proc_stats;

      return 0;
    }
}

/* Add a proc_stat for the process designated by PID at PP's proc context to
   PP.  If PID already has an entry in PP, nothing is done.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  If PS is
   non-NULL, the resulting entry is returned in it.  */
error_t
proc_stat_list_add_pid (struct proc_stat_list *pp, pid_t pid, struct proc_stat **ps)
{
  struct proc_stat *_ps = proc_stat_list_pid_proc_stat (pp, pid);

  if (_ps == NULL)
    {
      error_t err;

      if (pp->num_procs == pp->alloced)
	{
	  err = proc_stat_list_grow (pp, 32);
	  if (err)
	    return err;
	}

      err = ps_context_find_proc_stat (pp->context, pid, &_ps);
      if (err)
	return err;

      pp->proc_stats[pp->num_procs++] = _ps;
    }

  if (ps)
    *ps = _ps;

  return 0;
}

/* ---------------------------------------------------------------- */

/* Returns the proc_stat in PP with a process-id of PID, if there's one,
   otherwise, NULL.  */
struct proc_stat *
proc_stat_list_pid_proc_stat (struct proc_stat_list *pp, pid_t pid)
{
  unsigned nprocs = pp->num_procs;
  struct proc_stat **procs = pp->proc_stats;

  while (nprocs-- > 0)
    if (proc_stat_pid (*procs) == pid)
      return *procs;
    else
      procs++;

  return NULL;
}

/* ---------------------------------------------------------------- */

/* Adds all proc_stats in MERGEE to PP that don't correspond to processes
   already in PP; the resulting order of proc_stats in PP is undefined.
   If MERGEE and PP point to different proc contexts, EINVAL is returned.  If a
   memory allocation error occurs, ENOMEM is returned.  Otherwise 0 is
   returned, and MERGEE is freed.  */
error_t
proc_stat_list_merge (struct proc_stat_list *pp, struct proc_stat_list *mergee)
{
  if (pp->context != mergee->context)
    return EINVAL;
  else
    {
      /* Make sure there's room for the max number of new elements in PP. */
      error_t err = proc_stat_list_grow (pp, mergee->num_procs);

      if (err)
	return err;
      else
	{
	  int mnprocs = mergee->num_procs;
	  struct proc_stat **mprocs = mergee->proc_stats;
	  int nprocs = pp->num_procs;
	  struct proc_stat **procs = pp->proc_stats;

	  /* Transfer over any proc_stats from MERGEE to PP that don't
	     already exist there; for each of these, we set its entry in
	     MERGEE's proc_stat array to NULL, which prevents
	     proc_list_free () from freeing them.  */
	  while (mnprocs-- > 0)
	    if (proc_stat_list_pid_proc_stat(pp, proc_stat_pid (mprocs[mnprocs]))
		== NULL)
	      {
		procs[nprocs++] = mprocs[mnprocs];
		mprocs[mnprocs] = NULL;
	      }

	  proc_stat_list_free (mergee);

	  return 0;
	}
    }
}

/* ---------------------------------------------------------------- */

/* the number of max number pids that will fit in our static buffers (above
   which mig will vm_allocate space for them) */
#define STATICPIDS 200

/* Add to PP entries for all processes in the collection fetched from the
   proc server by the function FETCH_FN.  If an error occurs, the system
   error code is returned, otherwise 0.  If PROC_STATS and NUM_PROCS are
   non-NULL, a malloced vector of the resulting entries is returned in them. */
static error_t
proc_stat_list_add_fn_pids (struct proc_stat_list *pp,
			    kern_return_t (*fetch_fn)(process_t proc,
						      pid_t **pids,
						      size_t *num_pids),
			    struct proc_stat ***proc_stats, size_t *num_procs)
{
  error_t err;
  pid_t pid_array[STATICPIDS], *pids = pid_array;
  size_t num_pids = STATICPIDS;

  err = (*fetch_fn)(ps_context_server (pp->context), &pids, &num_pids);
  if (err)
    return err;

  err = proc_stat_list_add_pids (pp, pids, num_pids, proc_stats);
  if (!err && num_procs)
    *num_procs = num_pids;

  if (pids != pid_array)
    VMFREE(pids, sizeof (pid_t) * num_pids);

  return err;
}

/* Add to PP entries for all processes in the collection fetched from the
   proc server by the function FETCH_FN and ID.  If an error occurs, the
   system error code is returned, otherwise 0.  If PROC_STATS and NUM_PROCS
   are non-NULL, a malloced vector of the resulting entries is returned in
   them.  */
static error_t
proc_stat_list_add_id_fn_pids (struct proc_stat_list *pp, unsigned id,
			       kern_return_t (*fetch_fn)(process_t proc,
							 pid_t id,
							 pid_t **pids,
							 size_t *num_pids),
			       struct proc_stat ***proc_stats,
			       size_t *num_procs)
{
  error_t id_fetch_fn (process_t proc, pid_t **pids, size_t *num_pids)
    {
      return (*fetch_fn)(proc, id, pids, num_pids);
    }
  return proc_stat_list_add_fn_pids (pp, id_fetch_fn, proc_stats, num_procs);
}

/* ---------------------------------------------------------------- */

/* Add to PP entries for all processes at its context.  If an error occurs,
   the system error code is returned, otherwise 0.  If PROC_STATS and
   NUM_PROCS are non-NULL, a malloced vector of the resulting entries is
   returned in them.  */
error_t
proc_stat_list_add_all (struct proc_stat_list *pp,
			struct proc_stat ***proc_stats, size_t *num_procs)
{
  return
    proc_stat_list_add_fn_pids (pp, proc_getallpids, proc_stats, num_procs);
}

/* Add to PP entries for all processes in the login collection LOGIN_ID at
   its context.  If an error occurs, the system error code is returned,
   otherwise 0.  If PROC_STATS and NUM_PROCS are non-NULL, a malloced vector
   of the resulting entries is returned in them.  */
error_t
proc_stat_list_add_login_coll (struct proc_stat_list *pp, pid_t login_id,
			       struct proc_stat ***proc_stats,
			       size_t *num_procs)
{
  return
    proc_stat_list_add_id_fn_pids (pp, login_id, proc_getloginpids,
				   proc_stats, num_procs);
}

/* Add to PP entries for all processes in the session SESSION_ID at its
   context.  If an error occurs, the system error code is returned, otherwise
   0.  If PROC_STATS and NUM_PROCS are non-NULL, a malloced vector of the
   resulting entries is returned in them.  */
error_t
proc_stat_list_add_session (struct proc_stat_list *pp, pid_t session_id,
			    struct proc_stat ***proc_stats,
			    size_t *num_procs)
{
  return
    proc_stat_list_add_id_fn_pids (pp, session_id, proc_getsessionpids,
				   proc_stats, num_procs);
}

/* Add to PP entries for all processes in the process group PGRP at its
   context.  If an error occurs, the system error code is returned, otherwise
   0.  If PROC_STATS and NUM_PROCS are non-NULL, a malloced vector of the
   resulting entries is returned in them.  */
error_t
proc_stat_list_add_pgrp (struct proc_stat_list *pp, pid_t pgrp,
			 struct proc_stat ***proc_stats, size_t *num_procs)
{
  return
    proc_stat_list_add_id_fn_pids (pp, pgrp, proc_getpgrppids,
				   proc_stats, num_procs);
}

/* ---------------------------------------------------------------- */

/* Try to set FLAGS in each proc_stat in PP (but they may still not be set
   -- you have to check).  If a fatal error occurs, the error code is
   returned, otherwise 0.  */
error_t
proc_stat_list_set_flags (struct proc_stat_list *pp, ps_flags_t flags)
{
  unsigned nprocs = pp->num_procs;
  struct proc_stat **procs = pp->proc_stats;

  while (nprocs-- > 0)
    {
      struct proc_stat *ps = *procs++;

      if (!proc_stat_has (ps, flags))
	{
	  error_t err = proc_stat_set_flags (ps, flags);
	  if (err)
	    return err;
	}
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Destructively modify PP to only include proc_stats for which the
   function PREDICATE returns true; if INVERT is true, only proc_stats for
   which PREDICATE returns false are kept.  FLAGS is the set of pstat_flags
   that PREDICATE requires be set as precondition.  Regardless of the value
   of INVERT, all proc_stats for which the predicate's preconditions can't
   be satisfied are kept.  If a fatal error occurs, the error code is
   returned, it returns 0.  */
error_t
proc_stat_list_filter1(struct proc_stat_list *pp,
		       int (*predicate)(struct proc_stat *ps), ps_flags_t flags,
		       int invert)
{
  unsigned which = 0;
  unsigned num_procs = pp->num_procs;
  struct proc_stat **procs = pp->proc_stats;
  /* We compact the proc array as we filter, and KEPT points to end of the
     compacted part that we've already processed.  */
  struct proc_stat **kept = procs;
  error_t err = proc_stat_list_set_flags (pp, flags);

  if (err)
    return err;

  invert = !!invert;		/* Convert to a boolean.  */

  while (which < num_procs)
    {
      struct proc_stat *ps = procs[which++];

      /* See if we should keep PS; if PS doesn't satisfy the set of flags we
	 need, we don't attempt to call PREDICATE at all, and keep PS.  */

      if (!proc_stat_has(ps, flags) || !!predicate (ps) != invert)
	*kept++ = ps;
      /* ... otherwise implicitly delete PS from PP by not putting it in the
	 KEPT sequence.  */
    }

  pp->num_procs = kept - procs;

  return 0;
}

/* Destructively modify PP to only include proc_stats for which the
   predicate function in FILTER returns true; if INVERT is true, only
   proc_stats for which the predicate returns false are kept.  Regardless
   of the value of INVERT, all proc_stats for which the predicate's
   preconditions can't be satisfied are kept.  If a fatal error occurs,
   the error code is returned, it returns 0.  */
error_t
proc_stat_list_filter (struct proc_stat_list *pp,
		       const struct ps_filter *filter, int invert)
{
  return
    proc_stat_list_filter1(pp,
			   ps_filter_predicate (filter),
			   ps_filter_needs (filter),
			   invert);
}

/* ---------------------------------------------------------------- */

/* Destructively sort proc_stats in PP by ascending value of the field
   returned by GETTER, and compared by CMP_FN; If REVERSE is true, use the
   opposite order.  If a fatal error occurs, the error code is returned, it
   returns 0.  */
error_t
proc_stat_list_sort1 (struct proc_stat_list *pp,
		      const struct ps_getter *getter,
		      int (*cmp_fn)(struct proc_stat *ps1,
				    struct proc_stat *ps2,
				    const struct ps_getter *getter),
		      int reverse)
{
  int needs = ps_getter_needs (getter);
  struct proc_stat **procs = pp->proc_stats;
  error_t err = proc_stat_list_set_flags (pp, needs);

  /* Lessp is a nested function so it may use state variables from
     proc_stat_list_sort1, which qsort gives no other way of passing in.  */
  int lessp (const void *p1, const void *p2)
  {
    struct proc_stat *ps1 = *(struct proc_stat **)p1;
    struct proc_stat *ps2 = *(struct proc_stat **)p2;
    int is_th_1 = proc_stat_is_thread (ps1);
    int is_th_2 = proc_stat_is_thread (ps2);

    if (!is_th_1 || !is_th_2
	|| proc_stat_thread_origin(ps1) != proc_stat_thread_origin (ps2))
      /* Compare the threads' origins to keep them ordered after their
	 respective processes.  The exception is when they're both from the
	 same process, in which case we want to compare them directly so that
	 a process's threads are sorted among themselves (in most cases this
	 just fails because the thread doesn't have the proper fields; this
	 should just result in the threads remaining in their original
	 order).  */
      {
	if (is_th_1)
	  ps1 = proc_stat_thread_origin (ps1);
	if (is_th_2)
	  ps2 = proc_stat_thread_origin (ps2);
      }

    if (ps1 == ps2 || !proc_stat_has(ps1, needs) || !proc_stat_has (ps2, needs))
      /* If we're comparing a thread with it's process (and so the thread's
	 been replaced by the process), or we can't call CMP_FN on either
	 proc_stat due to lack of the necessary preconditions, then compare
	 their original positions, to retain the same order.  */
      return p1 - p2;
    else if (reverse)
      return cmp_fn (ps2, ps1, getter);
    else
      return cmp_fn (ps1, ps2, getter);
  }

  if (err)
    return err;

  qsort((void *)procs, (size_t)pp->num_procs, sizeof (struct proc_stat *), lessp);

  return 0;
}

/* Destructively sort proc_stats in PP by ascending value of the field KEY;
   if REVERSE is true, use the opposite order.  If KEY isn't a valid sort
   key, EINVAL is returned.  If a fatal error occurs the error code is
   returned.  Otherwise, 0 is returned.  */
error_t
proc_stat_list_sort (struct proc_stat_list *pp,
		     const struct ps_fmt_spec *key, int reverse)
{
  int (*cmp_fn)() = ps_fmt_spec_compare_fn (key);
  if (cmp_fn == NULL)
    return EINVAL;
  else
    return
      proc_stat_list_sort1 (pp, ps_fmt_spec_getter (key), cmp_fn, reverse);
}

/* ---------------------------------------------------------------- */

/* Format a description as instructed by FMT, of the processes in PP to
   STREAM, separated by newlines (and with a terminating newline).  If a
   fatal error occurs, the error code is returned, otherwise 0.  */
error_t
proc_stat_list_fmt (struct proc_stat_list *pp, struct ps_fmt *fmt,
		    struct ps_stream *stream)
{
  unsigned nprocs = pp->num_procs;
  struct proc_stat **procs = pp->proc_stats;
  error_t err = proc_stat_list_set_flags(pp, ps_fmt_needs (fmt));

  while (!err && nprocs-- > 0)
    {
      err = ps_fmt_write_proc_stat (fmt, *procs++, stream);
      if (! err)
	ps_stream_newline (stream);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Modifies FLAGS to be the subset which can't be set in any proc_stat in
   PP (and as a side-effect, adds as many bits from FLAGS to each proc_stat
   as possible).  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t
proc_stat_list_find_bogus_flags (struct proc_stat_list *pp, ps_flags_t *flags)
{
  unsigned nprocs = pp->num_procs;
  struct proc_stat **procs = pp->proc_stats;
  error_t err = proc_stat_list_set_flags (pp, *flags);

  if (err)
    return err;

  while (nprocs-- > 0 && *flags != 0)
    *flags &= ~proc_stat_flags (*procs++);

  return 0;
}

/* ---------------------------------------------------------------- */

/* Add thread entries for for every process in PP, located immediately after
   the containing process in sequence.  Subsequent sorting of PP will leave
   the thread entries located after the containing process, although the
   order of the thread entries themselves may change.  If a fatal error
   occurs, the error code is returned, otherwise 0.  */
error_t
proc_stat_list_add_threads (struct proc_stat_list *pp)
{
  error_t err = proc_stat_list_set_flags (pp, PSTAT_NUM_THREADS);

  if (err)
    return err;
  else
    {
      int new_entries = 0;
      int nprocs = pp->num_procs;
      struct proc_stat **procs = pp->proc_stats;

      /* First, count the number of threads that will be added.  */
      while (nprocs-- > 0)
	{
	  struct proc_stat *ps = *procs++;
	  if (proc_stat_has (ps, PSTAT_NUM_THREADS))
	    new_entries += proc_stat_num_threads (ps);
	}

      /* And make room for them...  */
      err = proc_stat_list_grow (pp, new_entries);
      if (err)
	return err;
      else
	/* Now add thread entries for every existing entry in PP; we go
	   through them backwards so we can do it in place.  */
	{
	  struct proc_stat **end = pp->proc_stats + pp->num_procs + new_entries;

	  nprocs = pp->num_procs;
	  procs = pp->proc_stats + nprocs;

	  while (nprocs-- > 0)
	    {
	      struct proc_stat *ps = *--procs;
	      if (proc_stat_has (ps, PSTAT_NUM_THREADS))
		{
		  int nthreads = proc_stat_num_threads (ps);
		  while (nthreads-- > 0)
		    proc_stat_thread_create (ps, nthreads, --end);
		}
	      *--end = ps;
	    }

	  pp->num_procs += new_entries;
	}
    }

  return 0;
}

error_t
proc_stat_list_remove_threads (struct proc_stat_list *pp)
{
  int is_thread (struct proc_stat *ps)
    {
      return proc_stat_is_thread (ps);
    }
  return proc_stat_list_filter1(pp, is_thread, 0, FALSE);
}

/* ---------------------------------------------------------------- */

/* Calls FN in order for each proc_stat in PP.  If FN ever returns a non-zero
   value, then the iteration is stopped, and the value is returned
   immediately; otherwise, 0 is returned.  */
int
proc_stat_list_for_each (struct proc_stat_list *pp, int (*fn)(struct proc_stat *ps))
{
  unsigned nprocs = pp->num_procs;
  struct proc_stat **procs = pp->proc_stats;

  while (nprocs-- > 0)
    {
      int val = (*fn)(*procs++);
      if (val)
	return val;
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Returns true if SPEC is `nominal' in every entry in PP.  */
int
proc_stat_list_spec_nominal (struct proc_stat_list *pp,
			     const struct ps_fmt_spec *spec)
{
  int (*nominal_fn)(struct proc_stat *ps, const struct ps_getter *getter) =
    spec->nominal_fn;

  if (nominal_fn == NULL)
    return FALSE;
  else
    {
      const struct ps_getter *getter = ps_fmt_spec_getter (spec);
      ps_flags_t needs = ps_getter_needs (getter);
      int interesting (struct proc_stat *ps)
	{
	  return proc_stat_has (ps, needs) && !(*nominal_fn)(ps, getter);
	}

      proc_stat_list_set_flags (pp, needs);

      return !proc_stat_list_for_each (pp, interesting);
    }
}
