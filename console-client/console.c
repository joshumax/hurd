/* console.c -- A pluggable console client.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wchar.h>
#include <error.h>
#include <assert.h>

#include <cthreads.h>

#include <hurd/console.h>
#include <hurd/cons.h>

#include <version.h>

#include "driver.h"
#include "timer.h"

const char *cons_client_name = "console";
const char *cons_client_version = HURD_VERSION;


/* The global lock protects the active_vcons variable, and thus all
   operations on the virtual console that is currently active.  */
static struct mutex global_lock;

/* The active virtual console.  This is the one currently
   displayed.  */
static vcons_t active_vcons = NULL;


/* Callbacks for input source drivers.  */

/* Switch the active console to console ID or DELTA (relative to the
   active console).  */
error_t
console_switch (int id, int delta)
{
  error_t err = 0;
  vcons_t vcons;
  vcons_t new_vcons;

  /* We must give up our global lock before we can call back into
     libcons.  This is because cons_switch will lock CONS, and as
     other functions in libcons lock CONS while calling back into our
     functions which take the global lock (like cons_vcons_add), we
     would deadlock.  So we acquire a reference for VCONS to make sure
     it isn't deallocated while we are outside of the global lock.  We
     also know that */

  mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  mutex_unlock (&global_lock);

  err = cons_switch (vcons, id, delta, &new_vcons);
  if (!err)
    {
      mutex_lock (&global_lock);
      if (active_vcons != new_vcons)
        {
          cons_vcons_close (active_vcons);
          active_vcons = new_vcons;
        }
      mutex_unlock (&new_vcons->lock);
      ports_port_deref (vcons);
      mutex_unlock (&global_lock);
    }
  return err;
}


/* Enter SIZE bytes from the buffer BUF into the currently active
   console.  This can be called by the input driver at any time.  */
error_t
console_input (char *buf, size_t size)
{
  error_t err = 0;
  vcons_t vcons;

  mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  mutex_unlock (&global_lock);

  if (vcons)
    {
      err = cons_vcons_input (vcons, buf, size);
      ports_port_deref (vcons);
    }
  return err;
}


/* Scroll the active console by TYPE and VALUE as specified by
   cons_vcons_scrollback.  */
int
console_scrollback (cons_scroll_t type, float value)
{
  int nr = 0;
  vcons_t vcons;

  mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  mutex_unlock (&global_lock);

  if (vcons)
    {
      nr = cons_vcons_scrollback (vcons, type, value);
      ports_port_deref (vcons);
    }
  return nr;
}


/* Exit the console client.  Does not return.  */
void
console_exit (void)
{
  driver_fini ();
  exit (0);
}

/* Signal an error to the user.  */
void console_error (const wchar_t *const err_msg)
{
  mutex_lock (&global_lock);
  bell_iterate
    if (bell->ops->beep)
      bell->ops->beep (bell->handle);
  mutex_unlock (&global_lock);
}

#if QUAERENDO_INVENIETIS
void
console_deprecated (int key)
{
  mutex_lock (&global_lock);
  input_iterate
    if (input->ops->deprecated)
      (*input->ops->deprecated) (input->handle, key);
  display_iterate
    if (display->ops->deprecated)
      (*display->ops->deprecated) (display->handle, key);
  bell_iterate
    if (bell->ops->deprecated)
      (*bell->ops->deprecated) (bell->handle, key);
  mutex_unlock (&global_lock);
}
#endif	/* QUAERENDO_INVENIETIS */


/* Callbacks for libcons.  */

/* The user may define this function.  It is called after a
   virtual console entry was added.  CONS is locked.  */
void
cons_vcons_add (cons_t cons, vcons_list_t vcons_entry)
{
  error_t err = 0;
  mutex_lock (&global_lock);
  if (!active_vcons)
    {
      vcons_t vcons;

      /* The first virtual console added to the list is automatically
	 opened after startup.  */
      err = cons_vcons_open (cons, vcons_entry, &vcons);
      if (!err)
        {
          vcons_entry->vcons = vcons;
          active_vcons = vcons;
          mutex_unlock (&vcons->lock);
        }
    }
  mutex_unlock (&global_lock);
}


/* The user may define this function.  Make the changes from
   cons_vcons_write, cons_vcons_set_cursor_pos,
   cons_vcons_set_cursor_status and cons_vcons_scroll active.  VCONS
   is locked and will have been continuously locked from the first
   change since the last update on.  This is the latest possible point
   the user must make the changes visible from.  The user can always
   make the changes visible at a more convenient, earlier time.  */
void
cons_vcons_update (vcons_t vcons)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->update)
	display->ops->update (display->handle);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Set the cursor on virtual
   console VCONS, which is locked, to position COL and ROW.  */
void
cons_vcons_set_cursor_pos (vcons_t vcons, uint32_t col, uint32_t row)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_cursor_pos)
	display->ops->set_cursor_pos (display->handle, col, row);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Set the cursor status of
   virtual console VCONS, which is locked, to STATUS.  */
void
cons_vcons_set_cursor_status (vcons_t vcons, uint32_t status)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_cursor_status)
	display->ops->set_cursor_status (display->handle, status);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Scroll the content of virtual
   console VCONS, which is locked, up by DELTA if DELTA is positive or
   down by -DELTA if DELTA is negative.  DELTA will never be zero, and
   the absolute value if DELTA will be smaller than or equal to the
   height of the screen matrix.

   See <hurd/cons.h> for more information about this function.  */
void
cons_vcons_scroll (vcons_t vcons, int delta)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->scroll)
	display->ops->scroll (display->handle, delta);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Deallocate the scarce
   resources (like font glyph slots, colors etc) in the LENGTH entries
   of the screen matrix starting from position COL and ROW.  This call
   is immediately followed by a subsequent cons_vcons_write call with
   the same LENGTH, COL and ROW arguments, and should help to make the
   write successful.  If there are no scarce resources, the caller
   might do nothing.  */
void cons_vcons_clear (vcons_t vcons, size_t length,
		       uint32_t col, uint32_t row)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->clear)
	display->ops->clear (display->handle, length, col, row);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Write LENGTH characters
   starting from STR on the virtual console VCONS, which is locked,
   starting from position COL and ROW.  */
void
cons_vcons_write (vcons_t vcons, conchar_t *str, size_t length,
		  uint32_t col, uint32_t row)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->write)
	display->ops->write (display->handle, str, length, col, row);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Make the virtual console
   VCONS, which is locked, beep audibly.  */
void
cons_vcons_beep (vcons_t vcons)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    bell_iterate
      if (bell->ops->beep)
	bell->ops->beep (bell->handle);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Make the virtual console
   VCONS, which is locked, flash visibly.  */
void
cons_vcons_flash (vcons_t vcons)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->flash)
	display->ops->flash (display->handle);
  mutex_unlock (&global_lock);
}


/* The user must define this function.  Notice the current status of
   the scroll lock flag.  */
void
cons_vcons_set_scroll_lock (vcons_t vcons, int onoff)
{
  mutex_lock (&global_lock);
  if (vcons == active_vcons)
    input_iterate
      if (input->ops->set_scroll_lock_status)
	input->ops->set_scroll_lock_status (input->handle, onoff);
  mutex_unlock (&global_lock);
}


/* Console-specific options.  */
static const struct argp_option
options[] =
  {
    {"driver-path", 'D', "PATH", 0, "Specify search path for driver modules" },
    {"driver", 'd', "NAME", 0, "Add driver NAME to the console" },
    {0}
  };

/* Parse a command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  static int devcount = 0;
  error_t err;

  switch (key)
    {
    case 'D':
      {
	char *s;
	char *d;

	if (driver_path)
	  free (driver_path);
	driver_path = malloc (strlen (arg) + 2);
	if (!driver_path)
	  {
	    argp_failure (state, 1, ENOMEM, "adding driver path failed");
	    return EINVAL;
	  }
	s = arg;
	d = driver_path;
	while (*s)
	  {
	    *(d++) = (*s == ':') ? '\0' : *s;
	    s++;
	  }
	*(d++) = '\0';
	*d = '\0';
      }
      break;

    case 'd':
      err = driver_add (arg /* XXX */, arg,
			state->argc, state->argv, &state->next, 0);
      if (err)
	{
	  argp_failure (state, 1, err, "loading driver `%s' failed", arg);
	  return EINVAL;
	}
      devcount++;
      break;

    case ARGP_KEY_SUCCESS:
      if (!devcount)
	{
	  argp_error (state, "at least one --driver argument required");
	  return EINVAL;
	}
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Add our startup arguments to the standard cons set.  */
static const struct argp_child startup_children[] =
  { { &cons_startup_argp }, { 0 } };
static struct argp startup_argp = {options, parse_opt, 0,
				   0, startup_children};

int
main (int argc, char *argv[])
{
  error_t err;
  char *errname;

  driver_init ();

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&startup_argp, argc, argv, 0, 0, 0);

  err = driver_start (&errname);
  if (err)
    error (1, err, "Starting driver %s failed", errname);
    
  mutex_init (&global_lock);

  err = cons_init ();
  if (err)
    {
      driver_fini ();
      error (1, err, "Console library initialization failed");
    }

  err = timer_init ();
  if (err)
    {
      driver_fini ();
      error (1, err, "Timer thread initialization failed");
    }

  cons_server_loop ();

  /* Never reached.  */
  driver_fini ();
  return 0;
}
