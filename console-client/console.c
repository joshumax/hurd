/* console.c -- A pluggable console client.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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
#include <assert-backtrace.h>

#include <pthread.h>
#if HAVE_DAEMON
#include <libdaemon/daemon.h>
#endif

#include <hurd/console.h>
#include <hurd/cons.h>

#include <version.h>

#include "driver.h"
#include "timer.h"
#include "trans.h"

const char *cons_client_name = "console";
const char *cons_client_version = HURD_VERSION;

/* The default node on which the console node is started.  */
#define DEFAULT_CONSOLE_NODE	"/dev/cons"


/* The global lock protects the active_vcons variable, and thus all
   operations on the virtual console that is currently active.  */
static pthread_mutex_t global_lock;

/* The active virtual console.  This is the one currently
   displayed.  */
static vcons_t active_vcons = NULL;

/* Contains the VT id when switched away.  */
static int saved_id = 0;

/* The console, used to switch back.  */
static cons_t saved_cons;

/* The file name of the node on which the console translator is
   set.  */
static char *console_node;

/* If set, the client will daemonize.  */
static int daemonize;

/* Callbacks for input source drivers.  */

/* Returns current console ID.  */
error_t
console_current_id (int *cur)
{
  vcons_t vcons;

  pthread_mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      pthread_mutex_unlock (&global_lock);
      return ENODEV;
    }
  *cur = vcons->id;
  pthread_mutex_unlock (&global_lock);
  return 0;
}

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

  pthread_mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      pthread_mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  pthread_mutex_unlock (&global_lock);

  err = cons_switch (vcons, id, delta, &new_vcons);
  if (!err)
    {
      pthread_mutex_lock (&global_lock);
      if (active_vcons != new_vcons)
        {
          cons_vcons_close (active_vcons);
          active_vcons = new_vcons;
        }
      pthread_mutex_unlock (&new_vcons->lock);
      ports_port_deref (vcons);
      pthread_mutex_unlock (&global_lock);
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

  pthread_mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      pthread_mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  pthread_mutex_unlock (&global_lock);

  if (vcons)
    {
      err = cons_vcons_input (vcons, buf, size);
      ports_port_deref (vcons);
    }
  return err;
}


/* Report the mouse event EV to the currently active console.  This
   can be called by the input driver at any time.  */
error_t
console_move_mouse (mouse_event_t ev)
{
  error_t err;
  vcons_t vcons;

  pthread_mutex_lock (&global_lock);
  
  vcons = active_vcons;
  if (!vcons)
    {
      pthread_mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  pthread_mutex_unlock (&global_lock);

  if (vcons)
    {
      err = cons_vcons_move_mouse (vcons, ev);
      ports_port_deref (vcons);
      return err;
    }

  return 0;
}


/* Scroll the active console by TYPE and VALUE as specified by
   cons_vcons_scrollback.  */
int
console_scrollback (cons_scroll_t type, float value)
{
  int nr = 0;
  vcons_t vcons;

  pthread_mutex_lock (&global_lock);
  vcons = active_vcons;
  if (!vcons)
    {
      pthread_mutex_unlock (&global_lock);
      return EINVAL;
    }
  ports_port_ref (vcons);
  pthread_mutex_unlock (&global_lock);

  if (vcons)
    {
      nr = cons_vcons_scrollback (vcons, type, value);
      ports_port_deref (vcons);
    }
  return nr;
}


/* Switch away from the console an external use of the console like
   XFree.  */
void
console_switch_away (void)
{
  pthread_mutex_lock (&global_lock);

  driver_iterate
    if (driver->ops->save_status)
      driver->ops->save_status (driver->handle);

  if (active_vcons)
    {
      saved_id = active_vcons->id;
      saved_cons = active_vcons->cons;
      cons_vcons_close (active_vcons);
      active_vcons = NULL;
    }
  else
    {
      saved_cons = NULL;
    }
  pthread_mutex_unlock (&global_lock);
}

/* Switch back to the console client from an external user of the
   console like XFree.  */
void
console_switch_back (void)
{
  vcons_list_t conslist;
  pthread_mutex_lock (&global_lock);

  driver_iterate
    if (driver->ops->restore_status)
      driver->ops->restore_status (driver->handle);
  
  if (saved_cons)
    {
      error_t err;

      err = cons_lookup (saved_cons, saved_id, 1, &conslist);
      if (err)
	{
	  pthread_mutex_unlock (&global_lock);
	  return;
	}

      err = cons_vcons_open (saved_cons, conslist, &active_vcons);
      if (err)
	{
	  pthread_mutex_unlock (&global_lock);
	  return;
	}
	
      conslist->vcons = active_vcons;
      saved_cons = NULL;
      pthread_mutex_unlock (&active_vcons->lock);
    }
  pthread_mutex_unlock (&global_lock);
}


/* Exit the console client.  Does not return.  */
void
console_exit (void)
{
  driver_fini ();
#if HAVE_DAEMON
  if (daemonize)
    daemon_pid_file_remove ();
#endif /* HAVE_DAEMON */
  exit (0);
}

/* Signal an error to the user.  */
void console_error (const wchar_t *const err_msg)
{
  pthread_mutex_lock (&global_lock);
  bell_iterate
    if (bell->ops->beep)
      bell->ops->beep (bell->handle);
  pthread_mutex_unlock (&global_lock);
}

#if QUAERENDO_INVENIETIS
void
console_deprecated (int key)
{
  pthread_mutex_lock (&global_lock);
  input_iterate
    if (input->ops->deprecated)
      (*input->ops->deprecated) (input->handle, key);
  display_iterate
    if (display->ops->deprecated)
      (*display->ops->deprecated) (display->handle, key);
  bell_iterate
    if (bell->ops->deprecated)
      (*bell->ops->deprecated) (bell->handle, key);
  pthread_mutex_unlock (&global_lock);
}
#endif	/* QUAERENDO_INVENIETIS */


/* Callbacks for libcons.  */

/* The user may define this function.  It is called after a
   virtual console entry was added.  CONS is locked.  */
void
cons_vcons_add (cons_t cons, vcons_list_t vcons_entry)
{
  error_t err = 0;
  pthread_mutex_lock (&global_lock);
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
          pthread_mutex_unlock (&vcons->lock);
        }
    }
  pthread_mutex_unlock (&global_lock);
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
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->update)
	display->ops->update (display->handle);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Set the cursor on virtual
   console VCONS, which is locked, to position COL and ROW.  */
void
cons_vcons_set_cursor_pos (vcons_t vcons, uint32_t col, uint32_t row)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_cursor_pos)
	display->ops->set_cursor_pos (display->handle, col, row);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Set the cursor status of
   virtual console VCONS, which is locked, to STATUS.  */
void
cons_vcons_set_cursor_status (vcons_t vcons, uint32_t status)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_cursor_status)
	display->ops->set_cursor_status (display->handle, status);
  pthread_mutex_unlock (&global_lock);
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
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->scroll)
	display->ops->scroll (display->handle, delta);
  pthread_mutex_unlock (&global_lock);
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
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->clear)
	display->ops->clear (display->handle, length, col, row);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Write LENGTH characters
   starting from STR on the virtual console VCONS, which is locked,
   starting from position COL and ROW.  */
void
cons_vcons_write (vcons_t vcons, conchar_t *str, size_t length,
		  uint32_t col, uint32_t row)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->write)
	display->ops->write (display->handle, str, length, col, row);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Make the virtual console
   VCONS, which is locked, beep audibly.  */
void
cons_vcons_beep (vcons_t vcons)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    bell_iterate
      if (bell->ops->beep)
	bell->ops->beep (bell->handle);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Make the virtual console
   VCONS, which is locked, flash visibly.  */
void
cons_vcons_flash (vcons_t vcons)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->flash)
	display->ops->flash (display->handle);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Notice the current status of
   the scroll lock flag.  */
void
cons_vcons_set_scroll_lock (vcons_t vcons, int onoff)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    input_iterate
      if (input->ops->set_scroll_lock_status)
	input->ops->set_scroll_lock_status (input->handle, onoff);
  pthread_mutex_unlock (&global_lock);
}


/* The user must define this function.  Clear the existing screen
   matrix and set the size of the screen matrix to the dimension COL x
   ROW.  This call will be immediately followed by a call to
   cons_vcons_write that covers the whole new screen matrix.  */
error_t
cons_vcons_set_dimension (vcons_t vcons, uint32_t col, uint32_t row)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_dimension)
	display->ops->set_dimension (display->handle, col, row);
  pthread_mutex_unlock (&global_lock);
  return 0;
}


error_t
cons_vcons_set_mousecursor_pos (vcons_t vcons, float x, float y)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_mousecursor_pos)
	display->ops->set_mousecursor_pos (display->handle, x, y);
  pthread_mutex_unlock (&global_lock);
  return 0;
}


error_t
cons_vcons_set_mousecursor_status (vcons_t vcons, int status)
{
  pthread_mutex_lock (&global_lock);
  if (vcons == active_vcons)
    display_iterate
      if (display->ops->set_mousecursor_status)
	display->ops->set_mousecursor_status (display->handle, status);
  pthread_mutex_unlock (&global_lock);
  return 0;

}


#define DAEMONIZE_KEY 0x80 /* !isascii (DAEMONIZE_KEY), so no short option.  */

/* Console-specific options.  */
static const struct argp_option
options[] =
  {
    {"driver-path", 'D', "PATH", 0, "Specify search path for driver modules" },
    {"driver", 'd', "NAME", 0, "Add driver NAME to the console" },
    {"console-node", 'c', "FILE", OPTION_ARG_OPTIONAL,
     "Set a translator on the node FILE (default: " DEFAULT_CONSOLE_NODE ")" },
#if HAVE_DAEMON
    {"daemonize", DAEMONIZE_KEY, NULL, 0, "daemonize the console client"},
#endif
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

    case 'c':
      console_node = arg ? arg : DEFAULT_CONSOLE_NODE;
      if (!console_node)
	return ENOMEM;
      break;

    case DAEMONIZE_KEY:
      daemonize = 1;
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
#if HAVE_DAEMON
#define daemon_error(status, errnum, format, args...)			\
  do									\
    {									\
      if (daemonize)							\
	{								\
	  if (errnum)							\
	    daemon_log (LOG_ERR, format ": %s", ##args,			\
			strerror(errnum));				\
	  else								\
	    daemon_log (LOG_ERR, format, ##args);			\
	  if (status)							\
	    {								\
	      /* Signal parent.	 */					\
	      daemon_retval_send (status);				\
	      daemon_pid_file_remove ();				\
	      return 0;							\
	    }								\
	}								\
      else								\
	error (status, errnum, format, ##args);				\
    }									\
  while (0);
#else
#define daemon_error	error
#endif

int
main (int argc, char *argv[])
{
  error_t err;
  char *errname;

  driver_init ();

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&startup_argp, argc, argv, ARGP_IN_ORDER, 0, 0);

#if HAVE_DAEMON
  if (daemonize)
    {
      /* Reset signal handlers.	 */
      if (daemon_reset_sigs (-1) < 0)
	error (1, errno, "Failed to reset all signal handlers");

      /* Unblock signals.  */
      if (daemon_unblock_sigs (-1) < 0)
	error (1, errno, "Failed to unblock all signals");

      /* Set indetification string for the daemon for both syslog and
	 PID file.  */
      daemon_pid_file_ident = daemon_log_ident = \
	daemon_ident_from_argv0 (argv[0]);

      /* Check that the daemon is not run twice at the same time.  */
      pid_t pid;
      if ((pid = daemon_pid_file_is_running ()) >= 0)
	error (1, errno, "Daemon already running on PID file %u", pid);

      /* Prepare for return value passing from the initialization
	 procedure of the daemon process.  */
      if (daemon_retval_init () < 0)
	error (1, errno, "Failed to create pipe.");

      /* Do the fork.  */
      if ((pid = daemon_fork ()) < 0)
	{
	  /* Exit on error.  */
	  daemon_retval_done ();
	  error (1, errno, "Failed to fork");
	}
      else if (pid)
	{
	  /* The parent.  */
	  int ret;

	  /* Wait for 20 seconds for the return value passed from the
	     daemon process. .	*/
	  if ((ret = daemon_retval_wait (20)) < 0)
	    error (1, errno,
		   "Could not receive return value from daemon process");

	  return ret;
	}
      else
	{
	  /* The daemon.  */
	  /* Close FDs.	 */
	  if (daemon_close_all (-1) < 0)
	    daemon_error (1, errno, "Failed to close all file descriptors");

	  /* Create the PID file.  */
	  if (daemon_pid_file_create () < 0)
	    daemon_error (2, errno, "Could not create PID file");
	}
    }
#endif /* HAVE_DAEMON */

  err = driver_start (&errname);
  if (err)
    daemon_error (1, err, "Starting driver %s failed", errname);
    
  pthread_mutex_init (&global_lock, NULL);

  err = cons_init ();
  if (err)
    {
      driver_fini ();
      daemon_error (1, err, "Console library initialization failed");
    }

  err = timer_init ();
  if (err)
    {
      driver_fini ();
      daemon_error (1, err, "Timer thread initialization failed");
    }

  if (console_node)
    console_setup_node (console_node);

#if HAVE_DAEMON
  if (daemonize)
    /* Signal parent that all went well.  */
    daemon_retval_send (0);
#endif

  cons_server_loop ();

  /* Never reached.  */
  console_exit ();
}
