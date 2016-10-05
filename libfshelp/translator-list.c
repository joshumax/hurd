/* A list of active translators.

   Copyright (C) 2013,14,15 Free Software Foundation, Inc.

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

#include <argz.h>
#include <hurd/fsys.h>
#include <hurd/ihash.h>
#include <hurd/ports.h>
#include <mach.h>
#include <mach/notify.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "fshelp.h"

struct translator
{
  struct port_info *pi;
  char *name;
  mach_port_t active;
};

/* The hash table requires some callback functions.  */
static void
cleanup (void *value, void *arg)
{
  struct translator *translator = value;

  if (translator->pi)
    ports_port_deref (translator->pi);
  mach_port_deallocate (mach_task_self (), translator->active);
  free (translator->name);
  free (translator);
}

static hurd_ihash_key_t
hash (const void *key)
{
  return (hurd_ihash_key_t) hurd_ihash_hash32 (key, strlen (key), 0);
}

static int
compare (const void *a, const void *b)
{
  return strcmp ((const char *) a, (const char *) b) == 0;
}

/* The list of active translators.  */
static struct hurd_ihash translator_ihash
  = HURD_IHASH_INITIALIZER_GKI (HURD_IHASH_NO_LOCP, cleanup, NULL,
				hash, compare);

/* The lock protecting the translator_ihash.  */
static pthread_mutex_t translator_ihash_lock = PTHREAD_MUTEX_INITIALIZER;

/* Record an active translator being bound to the given file name
   NAME.  ACTIVE is the control port of the translator.  */
error_t
fshelp_set_active_translator (struct port_info *pi,
			      const char *name,
			      mach_port_t active)
{
  error_t err = 0;
  struct translator *t;
  hurd_ihash_locp_t slot;

  pthread_mutex_lock (&translator_ihash_lock);
  t = hurd_ihash_locp_find (&translator_ihash, (hurd_ihash_key_t) name,
			    &slot);
  if (t)
    goto update; /* Entry exists.  */

  if (! MACH_PORT_VALID (active))
    /* Avoid allocating an entry just to delete it.  */
    goto out;

  t = malloc (sizeof *t);
  if (! t)
    {
      err = errno;
      goto out;
    }

  t->active = MACH_PORT_NULL;
  t->pi = NULL;
  t->name = strdup (name);
  if (! t->name)
    {
      err = errno;
      free (t);
      goto out;
    }

  err = hurd_ihash_locp_add (&translator_ihash, slot,
			     (hurd_ihash_key_t) t->name, t);
  if (err)
    goto out;

 update:
  if (active)
    {
      if (t->pi != pi)
	{
	  mach_port_t old;
	  err = mach_port_request_notification (mach_task_self (), active,
						MACH_NOTIFY_DEAD_NAME, 0,
						pi->port_right,
						MACH_MSG_TYPE_MAKE_SEND_ONCE,
						&old);
	  if (err)
	    goto out;
	  if (old != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), old);

	  if (t->pi)
	    ports_port_deref (t->pi);

	  ports_port_ref (pi);
	  t->pi = pi;
	}

      if (MACH_PORT_VALID (t->active))
	mach_port_deallocate (mach_task_self (), t->active);
      mach_port_mod_refs (mach_task_self (), active,
			  MACH_PORT_RIGHT_SEND, +1);
      t->active = active;
    }
  else
    hurd_ihash_remove (&translator_ihash, (hurd_ihash_key_t) t);

 out:
  pthread_mutex_unlock (&translator_ihash_lock);
  return err;
}

/* Remove the active translator specified by its control port ACTIVE.
   If there is no active translator with the given control port, this
   does nothing.  */
error_t
fshelp_remove_active_translator (mach_port_t active)
{
  error_t err = 0;
  pthread_mutex_lock (&translator_ihash_lock);

  struct translator *t = NULL;
  HURD_IHASH_ITERATE (&translator_ihash, value)
    {
      struct translator *v = value;
      if (active == v->active)
	{
	  t = v;
	  break;
	}
    }

  if (t)
    hurd_ihash_remove (&translator_ihash, (hurd_ihash_key_t) t->name);

  pthread_mutex_unlock (&translator_ihash_lock);
  return err;
}

/* Records the list of active translators below PREFIX into the argz
   vector specified by TRANSLATORS filtered by FILTER.  If PREFIX is
   NULL, entries with any prefix are considered.  If FILTER is NULL,
   no filter is applied.  */
error_t
fshelp_get_active_translators (char **translators,
			       size_t *translators_len,
			       fshelp_filter filter,
			       const char *prefix)
{
  error_t err = 0;
  pthread_mutex_lock (&translator_ihash_lock);

  if (prefix && strlen (prefix) == 0)
    prefix = NULL;

  HURD_IHASH_ITERATE (&translator_ihash, value)
    {
      struct translator *t = value;

      if (prefix != NULL
	  && (strncmp (t->name, prefix, strlen (prefix)) != 0
	      || t->name[strlen (prefix)] != '/'))
	/* Skip this entry, as it is not below PREFIX.  */
	continue;

      if (filter)
	{
	  char *dir = strdup (t->name);
	  if (! dir)
	    {
	      err = ENOMEM;
	      break;
	    }

	  err = filter (dirname (dir));
	  free (dir);
	  if (err)
	    {
	      err = 0;
	      continue; /* Skip this entry.  */
	    }
	}

      err = argz_add (translators, translators_len,
		      &t->name[prefix? strlen (prefix) + 1: 0]);
      if (err)
	break;
    }

  pthread_mutex_unlock (&translator_ihash_lock);
  return err;
}
