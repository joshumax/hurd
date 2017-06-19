/* dir-changed.c - Handling dir changed notifications.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <errno.h>
#include <dirent.h>
#include <assert-backtrace.h>
#include <mach.h>
#include <pthread.h>

#include "cons.h"
#include "fs_notify_S.h"


static error_t
add_one (cons_t cons, char *name)
{
  unsigned long int nr;
  char *tail;

  errno = 0;
  nr = strtoul (name, &tail, 10);
  if (!errno && *tail == '\0' && nr > 0)
    {
      vcons_list_t vcons_entry;
      return cons_lookup (cons, nr, 1, &vcons_entry);
    }
  return 0;
}

static error_t
lookup_one (cons_t cons, char *name, vcons_list_t *vcons_entry)
{
  unsigned long int nr;
  char *tail;

  errno = 0;
  nr = strtoul (name, &tail, 10);
  if (!errno && *tail == '\0' && nr > 0)
    return cons_lookup (cons, nr, 0, vcons_entry);
  return 0;
}


kern_return_t
cons_S_dir_changed (cons_notify_t notify, natural_t tickno,
		    dir_changed_type_t change, string_t name)
{
  error_t err;
  cons_t cons;

  if (!notify || !notify->cons)
    return EOPNOTSUPP;
  cons = notify->cons;

  pthread_mutex_lock (&cons->lock);

  switch (change)
    {
    case DIR_CHANGED_NULL:
      {
	DIR *dir = cons->dir;
	struct dirent *dent;
	do
	  {
	    errno = 0;
	    dent = readdir (dir);
	    if (!dent && errno)
	      err = errno;
	    else if (dent)
	      err = add_one (cons, dent->d_name);
	  }
	while (dent && !err);
	if (err)
	  assert_backtrace ("Unexpected error");	/* XXX */
      }
      break;
    case DIR_CHANGED_NEW:
      {
	err = add_one (cons, name);
	if (err)
	  assert_backtrace ("Unexpected error");	/* XXX */
      }
      break;
    case DIR_CHANGED_UNLINK:
      {
	vcons_list_t vcons_entry;
	err = lookup_one (cons, name, &vcons_entry);
	if (!err)
	  {
	    cons_vcons_remove (cons, vcons_entry);
	    if (vcons_entry->prev)
	      vcons_entry->prev->next = vcons_entry->next;
	    else
	      cons->vcons_list = vcons_entry->next;
	    if (vcons_entry->next)
	      vcons_entry->next->prev = vcons_entry->prev;
	    else
	      cons->vcons_last = vcons_entry->prev;

	    free (vcons_entry);
	  }
      }
      break;
    case DIR_CHANGED_RENUMBER:
    default:
      assert_backtrace ("Unexpected dir-changed type.");
      pthread_mutex_unlock (&cons->lock);
      return EINVAL;
    }
  pthread_mutex_unlock (&cons->lock);
  return 0;
}
