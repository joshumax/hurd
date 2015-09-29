/* idvec string representation

   Copyright (C) 1996, 1997, 1998 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <idvec.h>
#include <grp.h>
#include <pwd.h>

/* Return a string representation of the ids in IDVEC, each id separated by
   the string SEP (default ",").  SHOW_VALUES and SHOW_NAMES reflect how each
   id is printed (if SHOW_NAMES is true values are used where names aren't
   available); if both are true, the `VALUE(NAME)' format is used.
   ID_NAME_FN is used to map each id to a name; it should return a malloced
   string, which will be freed here.  The empty string is returned for an
   empty list, and 0 for an allocation error.  */
char *
idvec_rep (const struct idvec *idvec, int show_values, int show_names,
	   char *(*id_name_fn) (uid_t id), const char *sep)
{
  size_t sep_len;
  char *rep = 0;
  size_t rep_len = 0, rep_sz = 0;

  int ensure_room (size_t amount)
    {
      size_t end = rep_len + amount;
      if (end > rep_sz)
	{
	  size_t new_sz = rep_sz + end;
	  char *new_rep = realloc (rep, new_sz);
	  if (new_rep)
	    {
	      rep = new_rep;
	      rep_sz = new_sz;
	    }
	  else
	    return 0;
	}
      return 1;
    }
  int add_id (uid_t val, char *name)
    {
      if (!name || show_values)
	{
	  if (! ensure_room (10))
	    return 0;
	  rep_len += snprintf (rep + rep_len, 10, "%d", val);
	}
      if (name)
	{
	  size_t nlen = strlen (name) + 3;
	  if (! ensure_room (nlen))
	    {
	      free (name);
	      return 0;
	    }
	  rep_len +=
	    snprintf (rep + rep_len, nlen, show_values ? "(%s)" : "%s", name);
	  free (name);
	}
      return 1;
    }

  if (! sep)
    sep = ",";
  sep_len = strlen (sep);

  if (idvec->num > 0)
    {
      unsigned int i;

      for (i = 0; i < idvec->num; i++)
	{
	  char *name = 0;
	  uid_t val = idvec->ids[i];

	  if (i > 0)
	    {
	      if (ensure_room (sep_len))
		{
		  strcpy (rep + rep_len, sep);
		  rep_len += sep_len;
		}
	      else
		break;
	    }

	  if (show_names || !show_values)
	    name = (*id_name_fn) (val);
	  if (! add_id (val, name))
	    break;
	}

      if (i < idvec->num)
	{
	  free (rep);
	  return 0;
	}

      return rep;
    }

  return strdup ("");
}

/* Return a malloced string with the name of the user UID.  */
static char *
lookup_uid (uid_t uid)
{
  char buf[1024];
  struct passwd _pw, *pw;
  if (getpwuid_r (uid, &_pw, buf, sizeof buf, &pw) == 0 && pw)
    return strdup (pw->pw_name);
  else
    return 0;
}

/* Return a malloced string with the name of the group GID.  */
static char *
lookup_gid (gid_t gid)
{
  char buf[1024];
  struct group _gr, *gr;
  if (getgrgid_r (gid, &_gr, buf, sizeof buf, &gr) == 0 && gr)
    return strdup (gr->gr_name);
  else
    return 0;
}

/* Like idvec_rep, mapping ids to user names.  */
char *
idvec_uids_rep (const struct idvec *idvec, int show_values, int show_names,
		const char *sep)
{
  return idvec_rep (idvec, show_values, show_names, lookup_uid, sep);
}

/* Like idvec_rep, mapping ids to group names.  */
char *
idvec_gids_rep (const struct idvec *idvec, int show_values, int show_names,
		const char *sep)
{
  return idvec_rep (idvec, show_values, show_names, lookup_gid, sep);
}
