/* Simple reader/writer locks.

   Copyright (C) 1994, 1995 Free Software Foundation, Inc.

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

struct rwlock
{
  struct mutex master;
  struct condition wakeup;
  int readers;
  int writers_waiting;
  int readers_waiting;
};

/* Get a reader lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_reader_lock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  if (lock->readers == -1 || lock->writers_waiting)
    {
      lock->readers_waiting++;
      do
	condition_wait (&lock->wakeup, &lock->master);
      while (lock->readers == -1 || lock->writers_waiting);
      lock->readers_waiting--;
    }
  lock->readers++;
  mutex_unlock (&lock->master);
}

/* Get a writer lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_writer_lock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  if (lock->readers)
    {
      lock->writers_waiting++;
      do
	condition_wait (&lock->wakeup, &lock->master);
      while (lock->readers);
      lock->writers_waiting--;
    }
  lock->readers = -1;
  mutex_unlock (&lock->master);
}

/* Release a reader lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_reader_unlock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  assert (lock->readers);
  lock->readers--;
  if (lock->readers_waiting || lock->writers_waiting)
    condition_broadcast (&lock->wakeup);
  mutex_unlock (&lock->master);
}

/* Release a writer lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_writer_unlock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  assert (lock->readers == -1);
  lock->readers = 0;
  if (lock->readers_waiting || lock->writers_waiting)
    condition_broadcast (&lock->wakeup);
  mutex_unlock (&lock->master);
}

/* Initialize reader-writer lock LOCK */
extern inline void
rwlock_init (struct rwlock *lock)
{
  mutex_init (&lock->master);
  condition_init (&lock->wakeup);
  lock->readers = 0;
  lock->readers_waiting = 0;
  lock->writers_waiting = 0;
}
