/* String representation of ugids

   Copyright (C) 1997 Free Software Foundation, Inc.

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

#include <stdlib.h>
#include <string.h>

#include "ugids.h"

/* Return a string representation of the ids in UGIDS.  SHOW_VALUES and
   SHOW_NAMES reflect how each id is printed (if SHOW_NAMES is true values
   are used where names aren't available); if both are true, the
   `VALUE(NAME)' format is used.  ID_SEP, TYPE_SEP, and HDR_SEP contain the
   strings that separate, respectively, multiple ids of a particular type
   (default ","), the various types of ids (default ", "), and the name of
   each type from its ids (default ": ").  The empty string is returned for
   an empty list, and 0 for an allocation error.  */
char *
ugids_rep (const struct ugids *ugids, int show_values, int show_names,
	   const char *id_sep, const char *type_sep, const char *hdr_sep)
{
  size_t type_sep_len, hdr_sep_len;
  int first = 1;
  char *rep = 0;		/* Result */
  size_t len = 0;		/* Total length of result.  */
  char *euid_rep = 0, *egid_rep = 0, *auid_rep = 0, *agid_rep = 0;

  /* Calculate the rep for NAME, with ids IDS, returning the rep for the ids
     in REP, and updates LEN to include everything needed by this type (the
     length of *REP *plus* the length of NAME and any separators).  True is
     returned unless an allocation error occurs.  */
  int type_rep (const char *name, const struct idvec *ids, int is_group,
		char **rep)
    {
      if (ids->num > 0)
	{
	  if (first)
	    first = 0;
	  else
	    len += type_sep_len;
	  len += strlen (name);
	  len += hdr_sep_len;
	  *rep =
	    (is_group ? idvec_gids_rep : idvec_uids_rep)
	      (ids, show_values, show_names, id_sep);
	  if (*rep)
	    len += strlen (*rep);
	  else
	    return 0;
	}
      return 1;
    }
  void add_type_rep (char **to, const char *name, const char *rep)
    {
      if (rep)
	{
	  if (first)
	    first = 0;
	  else
	    *to = stpcpy (*to, type_sep);
	  *to = stpcpy (*to, name);
	  *to = stpcpy (*to, hdr_sep);
	  *to = stpcpy (*to, rep);
	}
    }

  if (! type_sep)
    type_sep = ", ";
  if (! hdr_sep)
    hdr_sep = ": ";

  type_sep_len = strlen (type_sep);
  hdr_sep_len = strlen (hdr_sep);

  if (type_rep ("euids", &ugids->eff_uids, 0, &euid_rep)
      && type_rep ("egids", &ugids->eff_gids, 1, &egid_rep)
      && type_rep ("auids", &ugids->avail_uids, 0, &auid_rep)
      && type_rep ("agids", &ugids->avail_gids, 1, &agid_rep))
    {
      char *p = malloc (len + 1);
      if (p)
	{
	  rep = p;
	  first = 1;
	  add_type_rep (&p, "euids", euid_rep);
	  add_type_rep (&p, "egids", egid_rep);
	  add_type_rep (&p, "auids", auid_rep);
	  add_type_rep (&p, "agids", agid_rep);
	}
    }

  if (euid_rep)
    free (euid_rep);
  if (egid_rep)
    free (egid_rep);
  if (auid_rep)
    free (auid_rep);
  if (agid_rep)
    free (agid_rep);

  return rep;
}
