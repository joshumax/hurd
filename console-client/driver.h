/* driver.h - The interface to and for a console client driver.
   Copyright (C) 2002, 2005 Free Software Foundation, Inc.
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

#ifndef _CONSOLE_DRIVER_H_
#define _CONSOLE_DRIVER_H_ 1

#include <errno.h>
#include <stddef.h>
#include <pthread.h>

#include "display.h"
#include "input.h"
#include "bell.h"


/* The path where we search for drivers, in addition to the default
   path.  The directories are separated by '\0' and terminated with an
   empty string.  XXX Should use argz or something.  XXX Should get a
   protective lock.  */
extern char *driver_path;


/* The driver framework allows loading and unloading of new drivers.
   It also provides some operations on the loaded drivers.  The device
   framework module does its own locking, so all operations can be run
   at any time by any thread.  */

/* Initialize the driver framework.  */
error_t driver_init (void);

/* Deinitialize and unload all loaded drivers and deinitialize the
   driver framework.  */
error_t driver_fini (void);

/* Forward declaration.  */
struct driver_ops;
typedef struct driver_ops *driver_ops_t;

/* Load, initialize and (if START is non-zero) start the driver DRIVER
   under the given NAME (which must be unique among all loaded
   drivers) with arguments ARGC, ARGV and NEXT (see
   parse_startup_args).  This function will grab the driver list lock.
   The driver itself might try to grab the display, input source and
   bell list locks as well.  */
error_t driver_add (const char *const name, const char *const driver,
		    int argc, char *argv[], int *next, int start);

/* Start all drivers.  Only used once at start up, after all the
   option parsing and driver initialization.

   Returns 0 on success, and the error if it initializing that driver
   fails (NAME points to the driver name then).  */
error_t driver_start (char **name);

/* Deinitialize and unload the driver with the name NAME.  This
   function will grab the driver list lock.  The driver might try to
   grab the display, input source and bell list locks as well.  */
error_t driver_remove (const char *const name);

/* Iterate over all loaded drivers.  This macro will grab the driver
   list lock.  You use it with a block:

   driver_iterate
     {
       printf ("%s\n", driver->ops->name);
     }

   Or even just:

   driver_iterate printf ("%s\n", driver->ops->name);

   The variable DRIVER is provided by the macro.  */
#define driver_iterate							\
  for (driver_t driver = (pthread_mutex_lock (&driver_list_lock),	\
			  &driver_list[0]);				\
       driver < &driver_list[driver_list_len]				\
	 || (pthread_mutex_unlock (&driver_list_lock), 0);		\
       driver++)


struct driver_ops
{
  /* Initialize an instance of the driver and return a handle for it
     in HANDLE.  The options in ARGC, ARGV and NEXT should be
     processed and validated.

     If NO_EXIT is zero, the function might exit on fatal errors or
     invalid arguments.  The drawback is that it must NOT allocate any
     resources that need to be freed or deallocated explicitely before
     exiting the program either, because other driver instances are
     also allowed to exit without prior notice at some later time.
     Allocation and initialization of such resources (like the video
     card) must be delayed until the start() function is called (see
     below).

     If NO_EXIT is non-zero, the function must not exit, but report
     all errors back to the caller.  In this case, it is guaranteed
     that the START function is called immediately after this function
     returns, and that the driver is properly unloaded with fini() at
     some later time.

     The above behaviour, and the split into an init() and a start()
     function, was carefully designed to allow the init() function the
     optimal use of argp at startup and at run time to parse options.
     
     ARGV[*NEXT] is the next argument to be parsed.  ARGC is the
     number of total arguments in ARGV.  The function should increment
     *NEXT for each argument parsed.  The function should not reorder
     arguments, nor should it parse non-option arguments.  It should
     also not parse over any single "--" argument.

     Every driver must implement this function.

     If NO_EXIT is zero, the function should return zero on success
     and otherwise either terminate or return an appropriate error
     value.  If it returns, either the program terminates because of
     other errors, or the function start() is called.

     If NO_EXIT is non-zero, the function should return zero on
     success and an appropriate error value otherwise.  If it returns
     success, the function start() will be called next, otherwise
     nothing happens.  */
  error_t (*init) (void **handle, int no_exit,
		   int argc, char *argv[], int *next);

  /* Activate the driver instance.  This function should load all the
     display, input and bell river components for this driver
     instance.

     If successful, the function should return zero.  In this case it
     is guaranteed that fini() will be called before the program
     terminates.  If not successful, the function should free all
     resources associated with HANDLE and return non-zero.  */ 
  error_t (*start) (void *handle);

  /* Deinitialize the driver.  This should remove all the individual
     drivers installed by init() and release all resources.  It should
     also reset all hardware devices into the state they had before
     calling init(), as far as applicable.  HANDLE is provided as
     returned by init().

     The function is allowed to fail if FORCE is 0.  If FORCE is not
     0, the driver should remove itself no matter what.  */
  error_t (*fini) (void *handle, int force);


  /* Save the status of the hardware.  */
  void (*save_status) (void *handle);
  
  /* Restore the status of the hardware.  */
  void (*restore_status) (void *handle);
};


/* The driver structure.  */
struct driver
{
  /* The unique name of the driver.  */
  char *name;

  /* The plugin name.  */
  char *driver;

  /* The filename that was identified as providing the plugin.  */
  char *filename;

  driver_ops_t ops;
  void *handle;

  /* The following members are private to the driver support code.  Do
     not use.  */

  /* The shared object handle as returned by dlopen().  */
  void *module;
};
typedef struct driver *driver_t;


/* Forward declarations needed by the macro above.  Don't use these
   variables directly.  */
extern pthread_mutex_t driver_list_lock;
extern driver_t driver_list;
extern size_t driver_list_len;


/* Iterate over all loaded displays.  This macro will grab the display
   list lock.  You use it with a block, just like driver_iterate.

   display_iterate display->ops->flash (display->handle);

   The variable DISPLAY is provided by the macro.  */
#define display_iterate							\
  for (display_t display = (pthread_mutex_lock (&display_list_lock),	\
			    &display_list[0]);				\
       display < &display_list[display_list_len]			\
	 || (pthread_mutex_unlock (&display_list_lock), 0);		\
       display++)


/* The display structure.  */
struct display
{
  display_ops_t ops;
  void *handle;
};
typedef struct display *display_t;


/* Forward declarations needed by the macro above.  Don't use these
   variables directly.  */
extern pthread_mutex_t display_list_lock;
extern display_t display_list;
extern size_t display_list_len;


/* Iterate over all loaded inputs.  This macro will grab the input
   list lock.  You use it with a block, just like driver_iterate.

   input_iterate input->ops->set_scroll_lock_status (input->handle, 0);

   The variable INPUT is provided by the macro.  */
#define input_iterate								\
  for (input_t input = (pthread_mutex_lock (&input_list_lock), &input_list[0]);	\
       input < &input_list[input_list_len]					\
	 || (pthread_mutex_unlock (&input_list_lock), 0);			\
       input++)


/* The input structure.  */
struct input
{
  input_ops_t ops;
  void *handle;
};
typedef struct input *input_t;


/* Forward declarations needed by the macro above.  Don't use these
   variables directly.  */
extern pthread_mutex_t input_list_lock;
extern input_t input_list;
extern size_t input_list_len;


/* Iterate over all loaded bells.  This macro will grab the bell list
   lock.  You use it with a block, just like driver_iterate.

   bell_iterate bell->ops->beep (bell->handle);

   The variable BELL is provided by the macro.  */
#define bell_iterate								\
  for (bell_t bell = (pthread_mutex_lock (&bell_list_lock), &bell_list[0]);	\
       bell < &bell_list[bell_list_len]						\
	 || (pthread_mutex_unlock (&bell_list_lock), 0);			\
       bell++)


/* The bell structure, needed by the macro above.  Don't use it
   directly.  */
struct bell
{
  bell_ops_t ops;
  void *handle;
};
typedef struct bell *bell_t;

/* Forward declarations needed by the macro above.  Don't use these
   variables directly.  */
extern pthread_mutex_t bell_list_lock;
extern bell_t bell_list;
extern size_t bell_list_len;

#endif	/* _CONSOLE_DRIVER_H_ */
