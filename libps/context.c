/* The ps_context type, for per-procserver and somewhat global state.

   Copyright (C) 1995,96,99,2000,02 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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
#include <hurd/term.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/* Returns in PC a new ps_context for the proc server SERVER.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t
ps_context_create (process_t server, struct ps_context **pc)
{
  *pc = NEW (struct ps_context);
  if (*pc == NULL)
    return ENOMEM;

  (*pc)->server = server;
  (*pc)->user_hooks = 0;
  hurd_ihash_init (&(*pc)->procs, HURD_IHASH_NO_LOCP);
  hurd_ihash_init (&(*pc)->ttys, HURD_IHASH_NO_LOCP);
  hurd_ihash_init (&(*pc)->ttys_by_cttyid, HURD_IHASH_NO_LOCP);
  hurd_ihash_init (&(*pc)->users, HURD_IHASH_NO_LOCP);

  hurd_ihash_set_cleanup (&(*pc)->procs,
			  (hurd_ihash_cleanup_t) _proc_stat_free, NULL);
  hurd_ihash_set_cleanup (&(*pc)->ttys,
			  (hurd_ihash_cleanup_t) ps_tty_free, NULL);
  hurd_ihash_set_cleanup (&(*pc)->users,
			  (hurd_ihash_cleanup_t) ps_user_free, NULL);

  return 0;
}

/* Frees PC and any resources it consumes.  */
void
ps_context_free (struct ps_context *pc)
{
  hurd_ihash_destroy (&pc->procs);
  hurd_ihash_destroy (&pc->ttys);
  hurd_ihash_destroy (&pc->ttys_by_cttyid);
  hurd_ihash_destroy (&pc->users);
  free (pc);
}


/* ---------------------------------------------------------------- */

/* Return the value in HT indexed by the key ID.  If it doesn't exist create
   it by calling CREATE with ID and a return location pointer as arguments
   (CREATE should return either an error-code or 0 if no error occurs), and
   cache it in HT.  */
static error_t
lookup (int id, hurd_ihash_t ht, error_t (*create)(int id, void **),
	void **value)
{
  *value = hurd_ihash_find (ht, id);
  if (*value == NULL)
    {
      error_t err = create (id, value);
      if (err)
	return err;
      hurd_ihash_add (ht, id, *value);
    }
  return 0;
}

/* Find a proc_stat for the process referred to by PID, and return it in
   PS.  If an error occurs, it is returned, otherwise 0.  */
error_t
ps_context_find_proc_stat (struct ps_context *pc, pid_t pid, struct proc_stat **ps)
{
  error_t create (int pid, void **value)
    {
      return _proc_stat_create (pid, pc, (struct proc_stat **)value);
    }
  return lookup (pid, &pc->procs, create, (void **)ps);
}

/* Find a ps_tty for the terminal referred to by the port TTY_PORT, and
   return it in TTY.  If an error occurs, it is returned, otherwise 0.  */
error_t
ps_context_find_tty (struct ps_context *pc, mach_port_t tty_port,
		     struct ps_tty **tty)
{
  return lookup (tty_port,
		 &pc->ttys,
		 (error_t (*)(int id, void **result))ps_tty_create,
		 (void **)tty);
}

/* Find a ps_tty for the terminal referred to by the ctty id port
   CTTYID_PORT, and return it in TTY.  If an error occurs, it is returned,
   otherwise 0.  */
error_t
ps_context_find_tty_by_cttyid (struct ps_context *pc, mach_port_t cttyid_port,
			       struct ps_tty **tty)
{
  error_t create (int cttyid_port, void **value)
    {
      if (cttyid_port == MACH_PORT_NULL)
	{
	  *value = 0;
	  return 0;
	}
      else
	{
	  mach_port_t tty_port;
	  error_t err = termctty_open_terminal (cttyid_port, 0, &tty_port);
	  if (err)
	    return err;
	  else
	    return ps_context_find_tty (pc, tty_port, (struct ps_tty **)value);
	}
    }

  return lookup (cttyid_port, &pc->ttys_by_cttyid, create, (void **)tty);
}

/* Find a ps_user for the user referred to by UID, and return it in U.  */
error_t
ps_context_find_user (struct ps_context *pc, uid_t uid, struct ps_user **u)
{
  return lookup (uid,
		 &pc->users,
		 (error_t (*)(int id, void **result))ps_user_create,
		 (void **) u);
}
