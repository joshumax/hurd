/* The ps_tty type, for per-tty info.

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
#include <string.h>
#include <assert.h>
#include <hurd/term.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/* Create a ps_tty for the tty referred to by PORT, returning it in TTY.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t
ps_tty_create (file_t port, struct ps_tty **tty)
{
  *tty = NEW (struct ps_tty);
  if (*tty == NULL)
    return ENOMEM;

  (*tty)->port = port;
  (*tty)->name_state = PS_TTY_NAME_PENDING;
  (*tty)->short_name = NULL;
  (*tty)->short_name_alloced = FALSE;

  return 0;
}

/* Frees TTY and any resources it consumes.  */
void
ps_tty_free (struct ps_tty *tty)
{
  mach_port_deallocate(mach_task_self (), tty->port);
  if (tty->name_state == PS_TTY_NAME_OK && tty->name != NULL)
    free ((char *)tty->name);
  if (tty->short_name_alloced)
    free ((char *)tty->short_name);
  free (tty);
}

/* ---------------------------------------------------------------- */

/* Returns the name of the tty, or NULL if it can't be figured out.  */
const char *
ps_tty_name (struct ps_tty *tty)
{
  if (tty->name_state == PS_TTY_NAME_PENDING)
    {
      string_t buf;

      if (term_get_nodename (tty->port, buf) != 0)
	/* There is a terminal there, but we can't figure out its name.  */
	tty->name_state = PS_TTY_NAME_ERROR;
      else
	{
	  tty->name = strdup (buf);
	  tty->name_state = (tty->name ? PS_TTY_NAME_OK : PS_TTY_NAME_ERROR);
	}
    }

  if (tty->name_state == PS_TTY_NAME_OK)
    return tty->name;
  else
    return NULL;
}

/* ---------------------------------------------------------------- */

struct ps_tty_abbrev
{
  char *pfx;
  char *subst;
};

struct ps_tty_abbrev ps_tty_abbrevs[] =
{
  { "/tmp/console", "oc" },	/* temp hack */
  { "/dev/console", "co"},
  { "/dev/tty",     ""},
  { "/dev/pty",     ""},
  { "/dev/",	    ""},
  { 0 }
};

/* Returns the standard abbreviated name of the tty, the whole name if there
   is no standard abbreviation, or NULL if it can't be figured out.  */
const char *
ps_tty_short_name (struct ps_tty *tty)
{
  if (tty->short_name != NULL)
    return tty->short_name;
  else
    {
      struct ps_tty_abbrev *abbrev;
      const char *name = ps_tty_name (tty);

      if (name)
	for (abbrev = ps_tty_abbrevs; abbrev->pfx != NULL; abbrev++)
	  {
	    char *subst = abbrev->subst;
	    unsigned pfx_len = strlen (abbrev->pfx);

	    if (strncmp (name, abbrev->pfx, pfx_len) == 0)
	      {
		if (name[pfx_len] == '\0')
		  tty->short_name = abbrev->subst;
		else if (!subst || subst[0] == '\0')
		  tty->short_name = name + pfx_len;
		else
		  {
		    char *n = malloc (strlen(subst) + strlen (name + pfx_len));
		    if (n)
		      {
			strcpy (n, subst);
			strcat (n, name + pfx_len);
			tty->short_name = n;
			tty->short_name_alloced = TRUE;
		      }
		  }
		break;
	      }
	  }

      if (tty->short_name == NULL)
	tty->short_name = name;

      return tty->short_name;
    }
}
