/* Delayed deallocation of port_info objects.

   Copyright (C) 2015 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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

#ifndef	_HURD_PORTS_DEREF_DEFERRED_
#define	_HURD_PORTS_DEREF_DEFERRED_

#include <pthread.h>

/* A list of port_info objects.  */
struct pi_list;

/* We use protected payloads to look up objects without taking a lock.
   A complication arises if we destroy an object using
   ports_destroy_right.  To avoid payloads from becoming stale (and
   resulting in invalid memory accesses when being interpreted as
   pointer), we delay the deallocation of those object until all
   threads running at the time of the objects destruction are done
   with whatever they were doing and entered a quiescent period.  */
struct ports_threadpool
{
  /* Access to the threadpool object is serialized by this lock.  */
  pthread_spinlock_t lock;

  /* We maintain two sets of objects and threads.  Each thread and the
     threadpool itself has a color.  If a thread has the same color as
     the thread pool, it belongs to the old generation.  */
  unsigned int color;

  /* The number of old threads.  When an old thread enters its
     quiescent period, it decrements OLD_THREADS and flips its color
     (hence becoming a young thread).  */
  size_t old_threads;

  /* A list of old objects.  Once OLD_THREADS drops to zero, they are
     deallocated, and all young threads and objects become old threads
     and objects.  */
  struct pi_list *old_objects;

  /* The number of young threads.  Any thread joining or leaving the
     thread group must be a young thread.  */
  size_t young_threads;

  /* The list of young objects.  Any object being marked for delayed
     deallocation is added to this list.  */
  struct pi_list *young_objects;
};

/* Per-thread state.  */
struct ports_thread
{
  unsigned int color;
};

/* Initialize the thread pool.  */
void _ports_threadpool_init (struct ports_threadpool *);

/* Called by a thread to join a thread pool.  */
void _ports_thread_online (struct ports_threadpool *, struct ports_thread *);

/* Called by a thread that enters its quiescent period.  */
void _ports_thread_quiescent (struct ports_threadpool *, struct ports_thread *);

/* Called by a thread to leave a thread pool.  */
void _ports_thread_offline (struct ports_threadpool *, struct ports_thread *);

struct port_info;

/* Schedule an object for deallocation.  */
void _ports_port_deref_deferred (struct port_info *);

#endif	/* _HURD_PORTS_DEREF_DEFERRED_ */
