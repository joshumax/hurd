/* The type proc_stat_list_t, which holds lists of proc_stat_t's.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/* Creates a new proc_stat_list_t for processes from SERVER, which is
   returned in PP, and returns 0, or else returns ENOMEM if there wasn't
   enough memory.  */
error_t
proc_stat_list_create(process_t server, proc_stat_list_t *pp)
{
  *pp = NEW(struct proc_stat_list);
  if (*pp == NULL)
    return ENOMEM;

  (*pp)->proc_stats = 0;
  (*pp)->num_procs = 0;
  (*pp)->alloced = 0;
  (*pp)->server = server;

  return 0;
}

/* Free PP, and any resources it consumes.  */
void 
proc_stat_list_free(proc_stat_list_t pp)
{
  int i;
  for (i = 0; i < pp->num_procs; i++)
    if (pp->proc_stats[i] != NULL)
      proc_stat_free(pp->proc_stats[i]);
  FREE(pp->proc_stats);
  FREE(pp);
}

/* ---------------------------------------------------------------- */

/* Make sure there are at least AMOUNT new locations allocated in PP's
   proc_stat_t array (but without changing NUM_PROCS).  Returns ENOMEM if a
   memory allocation error occurred, 0 otherwise.  */
static error_t
proc_stat_list_grow(proc_stat_list_t pp, int amount)
{
  amount += pp->num_procs;

  if (amount > pp->alloced)
    {
      proc_stat_t *new_procs = GROWVEC(pp->proc_stats, proc_stat_t, amount);

      if (new_procs == NULL)
	return ENOMEM;

      pp->alloced = amount;
      pp->proc_stats = new_procs;
    }

  return 0;
}

/* Add proc_stat_t entries to PP for each process with a process id in the
   array PIDS (where NUM_PROCS is the length of PIDS).  Entries are only
   added for processes not already in PP.  ENOMEM is returned if a memory
   allocation error occurs, otherwise 0.  PIDs is not referenced by the
   resulting proc_stat_list_t, and so may be subsequently freed.  */
error_t
proc_stat_list_add_pids(proc_stat_list_t pp, int *pids, int num_procs)
{
  error_t err = proc_stat_list_grow(pp, num_procs);

  if (err)
    return err;
  else
    {
      proc_stat_t *end = pp->proc_stats + pp->num_procs;

      while (num_procs-- > 0)
	{
	  int pid = *pids++;

	  if (proc_stat_list_pid_proc_stat(pp, pid) == NULL)
	    {
	      err = proc_stat_create(pid, pp->server, end);
	      if (err)
		/* Whoops, back out what we've done so far.  */
		{
		  while (end > pp->proc_stats + pp->num_procs)
		    proc_stat_free(*--end);
		  return err;
		}
	      else
		end++;
	    }
	}

      pp->num_procs = end - pp->proc_stats;

      return 0;
    }
}

/* Add a proc_stat_t for the process designated by PID at PP's proc server to
   PP.  If PID already has an entry in PP, nothing is done.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t
proc_stat_list_add_pid(proc_stat_list_t pp, int pid)
{
  if (proc_stat_list_pid_proc_stat(pp, pid) == NULL)
    {
      error_t err;

      if (pp->num_procs == pp->alloced)
	{
	  err = proc_stat_list_grow(pp, 32);
	  if (err)
	    return err;
	}

      err = proc_stat_create(pid, pp->server, &pp->proc_stats[pp->num_procs]);
      if (err)
	return err;

      pp->num_procs++;
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Returns the proc_stat_t in PP with a process-id of PID, if there's one,
   otherwise, NULL.  */
proc_stat_t
proc_stat_list_pid_proc_stat(proc_stat_list_t pp, int pid)
{
  int nprocs = pp->num_procs;
  proc_stat_t *procs = pp->proc_stats;

  while (nprocs-- > 0)
    if (proc_stat_pid(*procs) == pid)
      return *procs;
    else
      procs++;

  return NULL;
}

/* ---------------------------------------------------------------- */

/* Adds all proc_stat_t's in MERGEE to PP that don't correspond to processes
   already in PP; the resulting order of proc_stat_t's in PP is undefined.
   If MERGEE and PP point to different proc servers, EINVAL is returned.  If a
   memory allocation error occurs, ENOMEM is returned.  Otherwise 0 is
   returned, and MERGEE is freed.  */
error_t
proc_stat_list_merge(proc_stat_list_t pp, proc_stat_list_t mergee)
{
  if (pp->server != mergee->server)
    return EINVAL;
  else
    {
      /* Make sure there's room for the max number of new elements in PP. */
      error_t err = proc_stat_list_grow(pp, mergee->num_procs);

      if (err)
	return err;
      else
	{
	  int mnprocs = mergee->num_procs;
	  proc_stat_t *mprocs = mergee->proc_stats;
	  int nprocs = pp->num_procs;
	  proc_stat_t *procs = pp->proc_stats;

	  /* Transfer over any proc_stat_t's from MERGEE to PP that don't
	     already exist there; for each of these, we set its entry in
	     MERGEE's proc_stat array to NULL, which prevents
	     proc_list_free() from freeing them.  */
	  while (mnprocs-- > 0)
	    if (proc_stat_list_pid_proc_stat(pp, proc_stat_pid(mprocs[mnprocs]))
		== NULL)
	      {
		procs[nprocs++] = mprocs[mnprocs];
		mprocs[mnprocs] = NULL;
	      }

	  proc_stat_list_free(mergee);

	  return 0;
	}
    }
}

/* ---------------------------------------------------------------- */

/* the number of max number pids that will fit in our static buffers (above
   which mig will vm_allocate space for them) */
#define STATICPIDS 200

/* Add to PP entries for all processes at its server.  If an error occurs,
   the system error code is returned, otherwise 0.  */
error_t
proc_stat_list_add_all(proc_stat_list_t pp)
{
  error_t err;
  pid_t pid_array[STATICPIDS], *pids = pid_array, num_procs = STATICPIDS;

  err = proc_getallpids(pp->server, &pids, &num_procs);
  if (err)
    return err;

  err = proc_stat_list_add_pids(pp, pids, num_procs);

  if (pids != pid_array)
    VMFREE(pids, sizeof(pid_t) * num_procs);

  return err;
}

/* Add to PP entries for all processes in the login collection LOGIN_ID at
   its server.  If an error occurs, the system error code is returned,
   otherwise 0.  */
error_t
proc_stat_list_add_login_coll(proc_stat_list_t pp, int login_id)
{
  error_t err;
  pid_t pid_array[STATICPIDS], *pids = pid_array, num_procs = STATICPIDS;

  err = proc_getloginpids(pp->server, login_id, &pids, &num_procs);
  if (err)
    return err;

  err = proc_stat_list_add_pids(pp, pids, num_procs);

  if (pids != pid_array)
    VMFREE(pids, sizeof(pid_t) * num_procs);

  return err;
}

/* Add to PP entries for all processes in the session SESSION_ID at its
   server.  If an error occurs, the system error code is returned, otherwise
   0.  */
error_t
proc_stat_list_add_session(proc_stat_list_t pp, int session_id)
{
  error_t err;
  pid_t pid_array[STATICPIDS], *pids = pid_array, num_procs = STATICPIDS;

  err = proc_getsessionpids(pp->server, session_id, &pids, &num_procs);
  if (err)
    return err;

  err = proc_stat_list_add_pids(pp, pids, num_procs);

  if (pids != pid_array)
    VMFREE(pids, sizeof(pid_t) * num_procs);

  return err;
}

/* ---------------------------------------------------------------- */

/* Try to set FLAGS in each proc_stat_t in PP (but they may still not be set
   -- you have to check).  If a fatal error occurs, the error code is
   returned, otherwise 0.  */
error_t
proc_stat_list_set_flags(proc_stat_list_t pp, int flags)
{
  int nprocs = pp->num_procs;
  proc_stat_t *procs = pp->proc_stats;

  while (nprocs-- > 0)
    {
      proc_stat_t ps = *procs++;

      if (!proc_stat_has(ps, flags))
	{
	  error_t err = proc_stat_set_flags(ps, flags);
	  if (err)
	    return err;
	}
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Destructively modify PP to only include proc_stat_t's for which the
   function PREDICATE returns true; if INVERT is true, only proc_stat_t's for
   which PREDICATE returns false are kept.  FLAGS is the set of pstat_flags
   that PREDICATE requires be set as precondition.  Regardless of the value
   of INVERT, all proc_stat_t's for which the predicate's preconditions can't
   be satisfied are kept.  If a fatal error occurs, the error code is
   returned, it returns 0.  */
error_t
proc_stat_list_filter1(proc_stat_list_t pp,
		       int (*predicate)(proc_stat_t ps), int flags,
		       bool invert)
{
  int which = 0;
  int num_procs = pp->num_procs;
  proc_stat_t *procs = pp->proc_stats;
  /* We compact the proc array as we filter, and KEPT points to end of the
     compacted part that we've already processed.  */
  proc_stat_t *kept = procs;
  error_t err = proc_stat_list_set_flags(pp, flags);

  if (err)
    return err;

  invert = !!invert;		/* Convert to a boolean.  */

  while (which < num_procs)
    {
      proc_stat_t ps = procs[which++];

      /* See if we should keep PS; if PS doesn't satisfy the set of flags we
	 need, we don't attempt to call PREDICATE at all, and keep PS.  */

      if (!proc_stat_has(ps, flags) || !!predicate(ps) != invert)
	*kept++ = ps;
      else
	/* Implicitly delete PS from PP by not putting it in the KEPT
	   sequence.  */
	proc_stat_free(ps);
    }

  pp->num_procs = kept - procs;

  return 0;
}

/* Destructively modify PP to only include proc_stat_t's for which the
   predicate function in FILTER returns true; if INVERT is true, only
   proc_stat_t's for which the predicate returns false are kept.  Regardless
   of the value of INVERT, all proc_stat_t's for which the predicate's
   preconditions can't be satisfied are kept.  If a fatal error occurs,
   the error code is returned, it returns 0.  */
error_t
proc_stat_list_filter(proc_stat_list_t pp, ps_filter_t filter, bool invert)
{
  return
    proc_stat_list_filter1(pp,
			   ps_filter_predicate(filter),
			   ps_filter_needs(filter),
			   invert);
}

/* ---------------------------------------------------------------- */

/* Destructively sort proc_stats in PP by ascending value of the field
   returned by GETTER, and compared by CMP_FN; If REVERSE is true, use the
   opposite order.  If a fatal error occurs, the error code is returned, it
   returns 0.  */
error_t
proc_stat_list_sort1(proc_stat_list_t pp,
		     ps_getter_t getter,
		     int (*cmp_fn)(proc_stat_t ps1, proc_stat_t ps2,
				   ps_getter_t getter),
		     bool reverse)
{
  int needs = ps_getter_needs(getter);
  proc_stat_t *procs = pp->proc_stats;
  error_t err = proc_stat_list_set_flags(pp, needs);

  /* Lessp is a nested function so it may use state variables from
     proc_stat_list_sort1, which qsort gives no other way of passing in.  */
  int lessp(const void *p1, const void *p2)
  {
    proc_stat_t ps1 = *(proc_stat_t *)p1;
    proc_stat_t ps2 = *(proc_stat_t *)p2;
    bool is_th_1 = proc_stat_is_thread(ps1);
    bool is_th_2 = proc_stat_is_thread(ps2);

    if (!is_th_1 || !is_th_2
	|| proc_stat_thread_origin(ps1) != proc_stat_thread_origin(ps2))
      /* Compare the threads' origins to keep them ordered after their
	 respective processes.  The exception is when they're both from the
	 same process, in which case we want to compare them directly so that
	 a process's threads are sorted among themselves (in most cases this
	 just fails because the thread doesn't have the proper fields; this
	 should just result in the threads remaining in their original
	 order).  */
      {
	if (is_th_1)
	  ps1 = proc_stat_thread_origin(ps1);
	if (is_th_2)
	  ps2 = proc_stat_thread_origin(ps2);
      }

    if (!proc_stat_has(ps1, needs) || !proc_stat_has(ps2, needs))
      /* If we can't call CMP_FN on either proc_stat_t due to lack of the
	 necessary preconditions, then compare their original positions, to
	 retain the same order.  */
      return p1 - p2;
    else if (reverse)
      return cmp_fn(ps2, ps1, getter);
    else
      return cmp_fn(ps1, ps2, getter);
  }

  if (err)
    return err;

  qsort((void *)procs, (size_t)pp->num_procs, sizeof(proc_stat_t), lessp);

  return 0;
}

/* Destructively sort proc_stats in PP by ascending value of the field KEY;
   if REVERSE is true, use the opposite order.  If KEY isn't a valid sort
   key, EINVAL is returned.  If a fatal error occurs the error code is
   returned.  Otherwise, 0 is returned.  */
error_t
proc_stat_list_sort(proc_stat_list_t pp, ps_fmt_spec_t key, bool reverse)
{
  int (*cmp_fn)() = ps_fmt_spec_compare_fn(key);
  if (cmp_fn == NULL)
    return EINVAL;
  else
    return
      proc_stat_list_sort1(pp, ps_fmt_spec_getter(key), cmp_fn, reverse);
}

/* ---------------------------------------------------------------- */

/* Format a description as instructed by FMT, of the processes in PP to
   STREAM, separated by newlines (and with a terminating newline).  If COUNT
   is non-NULL, it points to an integer which is incremented by the number of
   characters output.  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t
proc_stat_list_fmt(proc_stat_list_t pp, ps_fmt_t fmt, FILE * stream, int *count)
{
  int nprocs = pp->num_procs;
  proc_stat_t *procs = pp->proc_stats;
  error_t err = proc_stat_list_set_flags(pp, ps_fmt_needs(fmt));

  if (err)
    return err;

  while (nprocs-- > 0)
    {
      err = ps_fmt_write_proc_stat(fmt, *procs++, stream, count);
      if (err)
	return err;

      putc('\n', stream);
      if (count != NULL)
	(*count)++;
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Modifies FLAGS to be the subset which can't be set in any proc_stat_t in
   PP (and as a side-effect, adds as many bits from FLAGS to each proc_stat_t
   as possible).  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t
proc_stat_list_find_bogus_flags(proc_stat_list_t pp, int *flags)
{
  int nprocs = pp->num_procs;
  proc_stat_t *procs = pp->proc_stats;
  error_t err = proc_stat_list_set_flags(pp, *flags);

  if (err)
    return err;

  while (nprocs-- > 0 && *flags != 0)
    *flags &= ~proc_stat_flags(*procs++);

  return 0;
}

/* ---------------------------------------------------------------- */

/* Add thread entries for for every process in PP, located immediately after
   the containing process in sequence.  Subsequent sorting of PP will leave
   the thread entries located after the containing process, although the
   order of the thread entries themselves may change.  If a fatal error
   occurs, the error code is returned, otherwise 0.  */
error_t
proc_stat_list_add_threads(proc_stat_list_t pp)
{
  error_t err = proc_stat_list_set_flags(pp, PSTAT_NUM_THREADS);

  if (err)
    return err;
  else
    {
      int new_entries = 0;
      int nprocs = pp->num_procs;
      proc_stat_t *procs = pp->proc_stats;

      /* First, count the number of threads that will be added.  */
      while (nprocs-- > 0)
	{
	  proc_stat_t ps = *procs++;
	  if (proc_stat_has(ps, PSTAT_NUM_THREADS))
	    new_entries += proc_stat_num_threads(ps);
	}

      /* And make room for them...  */
      err = proc_stat_list_grow(pp, new_entries);
      if (err)
	return err;
      else
	/* Now add thread entries for every existing entry in PP; we go
	   through them backwards so we can do it in place.  */
	{
	  proc_stat_t *end = pp->proc_stats + pp->num_procs + new_entries;

	  nprocs = pp->num_procs;
	  procs = pp->proc_stats + nprocs;

	  while (nprocs-- > 0)
	    {
	      proc_stat_t ps = *--procs;
	      if (proc_stat_has(ps, PSTAT_NUM_THREADS))
		{
		  int nthreads = proc_stat_num_threads(ps);
		  while (nthreads-- > 0)
		    proc_stat_thread_create(ps, nthreads, --end);
		}
	      *--end = ps;
	    }

	  pp->num_procs += new_entries;
	}
    }

  return 0;
}
