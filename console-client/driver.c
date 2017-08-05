/* driver.c - The console client driver code.
   Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dlfcn.h>

#include <pthread.h>

#include "driver.h"


/* The number of entries by which we grow a driver or component list
   if we need more space.  */
#define LIST_GROW 4

/* The path where we search for drivers, in addition to the default
   path.  The directories are separated by '\0' and terminated with an
   empty string.  XXX Should use argz or something.  XXX Should get a
   protective lock.  */
char *driver_path;

/* The driver list lock, the list itself, its current length and the
   total number of entries in the list.  */
pthread_mutex_t driver_list_lock;
driver_t driver_list;
size_t driver_list_len;
size_t driver_list_alloc;


/* Initialize the driver framework.  */
error_t
driver_init (void)
{
  pthread_mutex_init (&driver_list_lock, NULL);
  pthread_mutex_init (&display_list_lock, NULL);
  pthread_mutex_init (&input_list_lock, NULL);
  pthread_mutex_init (&bell_list_lock, NULL);
  return 0;
}


/* Deinitialize and unload all loaded drivers and deinitialize the
   driver framework.  */
error_t
driver_fini (void)
{
  unsigned int i;

  pthread_mutex_lock (&driver_list_lock);
  for (i = 0; i < driver_list_len; i++)
    {
      driver_list[i].ops->fini (driver_list[i].handle, 1);
      dlclose (driver_list[i].module);
      free (driver_list[i].name);
      free (driver_list[i].driver);
    }
  driver_list_len = 0;
  pthread_mutex_unlock (&driver_list_lock);
  return 0;
}


/* Load, intialize and (if START is non-zero) start the driver DRIVER
   under the given NAME (which must be unique among all loaded
   drivers) with arguments ARGZ with length ARGZ_LEN.  This function
   will grab the driver list lock.  The driver itself might try to
   grab the display, input source and bell list locks as well.  */
error_t driver_add (const char *const name, const char *const driver,
		    int argc, char *argv[], int *next, int start)
{
  error_t err;
  static char cons_defpath[] = CONSOLE_DEFPATH;
  driver_ops_t ops;
  char *filename = NULL;
  char *modname;
  void *shobj = NULL;
  driver_t drv;
  unsigned int i;
  char *dir = driver_path;
  int defpath = 0;
  char *opt_backup;

  pthread_mutex_lock (&driver_list_lock);
  for (i = 0; i < driver_list_len; i++)
    if (driver_list[i].name && !strcmp (driver_list[i].name, name))
      {
	pthread_mutex_unlock (&driver_list_lock);
	return EEXIST;
      }

  if (!dir || !*dir)
    {
      dir = cons_defpath;
      defpath = 1;
    }

  while (dir)
    {
      free (filename);
      if (asprintf (&filename,
		    "%s/%s%s", dir, driver, CONSOLE_SONAME_SUFFIX) < 0)
	{
	  pthread_mutex_unlock (&driver_list_lock);
	  return ENOMEM;
	}

      errno = 0;
      shobj = dlopen (filename, RTLD_LAZY);
      if (!shobj)
	{
	  (void) dlerror (); /* Must always call or it leaks! */
	  if (errno != ENOENT)
	    {
	      free (filename);
	      pthread_mutex_unlock (&driver_list_lock);
	      return errno ?: EGRATUITOUS;
	    }
	}
      else
	break;

      dir += strlen (dir) + 1;
      if (!*dir)
	{
	  if (defpath)
	    break;
	  else
	    {
	      dir = cons_defpath;
	      defpath = 1;
	    }
	}
    }

  if (!shobj)
    {
      free (filename);
      pthread_mutex_unlock (&driver_list_lock);
      return ENOENT;
    }

  if (asprintf (&modname, "driver_%s_ops", driver) < 0)
    {
      dlclose (shobj);
      free (filename);
      pthread_mutex_unlock (&driver_list_lock);
      return ENOMEM;
    }

  ops = dlsym (shobj, modname);
  free (modname);
  if (!ops || !ops->init)
    {
      dlclose (shobj);
      free (filename);
      pthread_mutex_unlock (&driver_list_lock);
      return EGRATUITOUS;
    }

  if (driver_list_len == driver_list_alloc)
    {
      size_t new_alloc = driver_list_alloc + LIST_GROW;
      driver_t new_list = realloc (driver_list,
				   new_alloc * sizeof (*driver_list));
      if (!new_list)
	{
	  dlclose (shobj);
	  free (filename);
	  pthread_mutex_unlock (&driver_list_lock);
	  return errno;
	}
      driver_list = new_list;
      driver_list_alloc = new_alloc;
    }
  drv = &driver_list[driver_list_len];

  drv->name = strdup (name);
  drv->driver = strdup (driver);
  drv->filename = filename;
  drv->ops = ops;
  drv->module = shobj;
  if (!drv->name || !drv->driver)
    {
      if (drv->name)
	free (drv->name);
      if (drv->driver)
	free (drv->driver);
      dlclose (shobj);
      free (filename);
      pthread_mutex_unlock (&driver_list_lock);
      return ENOMEM;
    }

  opt_backup = argv[*next - 1];
  argv[*next - 1] = (char *) name;
  /* If we will start the driver, the init function must not exit.  */
  err = (*drv->ops->init) (&drv->handle, start, argc - (*next - 1),
			   argv + *next - 1, next);
  argv[*next - 1] = opt_backup;

  if (!err && start && drv->ops->start)
    err = (*drv->ops->start) (drv->handle);
  if (err)
    {
      free (drv->name);
      free (drv->driver);
      dlclose (shobj);
      free (filename);
      pthread_mutex_unlock (&driver_list_lock);
      return err;
    }

  driver_list_len++;
  pthread_mutex_unlock (&driver_list_lock);
  return 0;
}


/* Start all drivers.  Only used once at start up, after all the
   option parsing and driver initialization.

   Returns 0 on success, and the name of a driver if it initializing
   that driver fails.  */
error_t
driver_start (char **name)
{
  error_t err = 0;
  int i;
  
  pthread_mutex_lock (&driver_list_lock);
  for (i = 0; i < driver_list_len; i++)
    {
      if (driver_list[i].ops->start)
	err = (*driver_list[i].ops->start) (driver_list[i].handle);
      if (err)
	{
	  *name = driver_list[i].name;
	  while (i > 0)
	    {
	      i--;
	      (*driver_list[i].ops->fini) (driver_list[i].handle, 1);
	    }
	  break;
	}
    }
  pthread_mutex_unlock (&driver_list_lock);
  return err;
}


/* Deinitialize and unload the driver with the name NAME.  This
   function will grab the driver list lock.  The driver might try to
   grab the display, input source and bell list locks as well.  */
error_t driver_remove (const char *const name)
{
  error_t err;
  unsigned int i;

  pthread_mutex_lock (&driver_list_lock);
  for (i = 0; i < driver_list_len; i++)
    if (driver_list[i].name && !strcmp (driver_list[i].name, name))
      {
	err = driver_list[i].ops->fini (driver_list[i].handle, 0);
	if (!err)
	  {
	    dlclose (driver_list[i].module);
	    free (driver_list[i].name);
	    free (driver_list[i].driver);
	    free (driver_list[i].filename);
	    while (i + 1 < driver_list_len)
	      {
		driver_list[i] = driver_list[i + 1];
		i++;
	      }
	    driver_list_len--;
	  }
	pthread_mutex_unlock (&driver_list_lock);
	return err;
      }
  pthread_mutex_unlock (&driver_list_lock);
  return ESRCH;
}

#define ADD_REMOVE_COMPONENT(component)					\
pthread_mutex_t component##_list_lock;					\
component##_t component##_list;						\
size_t component##_list_len;						\
size_t component##_list_alloc;						\
									\
error_t									\
driver_add_##component (component##_ops_t ops, void *handle)		\
{									\
  pthread_mutex_lock (&component##_list_lock);				\
  if (component##_list_len == component##_list_alloc)			\
    {									\
      size_t new_alloc = component##_list_alloc + LIST_GROW;		\
      component##_t new_list = realloc (component##_list,		\
					new_alloc			\
					* sizeof (*component##_list));	\
      if (!new_list)							\
	{								\
	  pthread_mutex_unlock (&component##_list_lock);		\
	  return errno;							\
	}								\
      component##_list = new_list;					\
      component##_list_alloc = new_alloc;				\
    }									\
  component##_list[component##_list_len].ops = ops;			\
  component##_list[component##_list_len].handle = handle;		\
  component##_list_len++;						\
  pthread_mutex_unlock (&component##_list_lock);			\
  return 0;								\
}									\
									\
error_t									\
driver_remove_##component (component##_ops_t ops, void *handle)		\
{									\
  unsigned int i;								\
									\
  pthread_mutex_lock (&component##_list_lock);				\
  for (i = 0; i < component##_list_len; i++)				\
    if (component##_list[i].ops == ops					\
	&& component##_list[i].handle == handle)			\
      {									\
	while (i + 1 < component##_list_len)				\
	  {								\
	    component##_list[i] = component##_list[i + 1];		\
	    i++;							\
	  }								\
	component##_list_len--;						\
      }									\
  pthread_mutex_unlock (&component##_list_lock);			\
  return 0;								\
}

ADD_REMOVE_COMPONENT (display)
ADD_REMOVE_COMPONENT (input)
ADD_REMOVE_COMPONENT (bell)
