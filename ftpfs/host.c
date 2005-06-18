/* Server lookup

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <ftpconn.h>

/* Split the server-specification string SERVER into its components: a
   hostname (returned in HOST), username (USER), and password (PASSWD).  */
static error_t
split_server_name (const char *server, char **host, char **user, char **passwd)
{
  size_t plim;
  const char *p = server, *sep;

  *host = 0;
  *user = 0;
  *passwd = 0;

  /* Extract the hostname; syntax is either `HOST:...', `...@HOST', or just
     HOST if there are no user parameters specified.  */
  sep = strrchr (p, '@');
  if (sep)
    /* ...@HOST */
    {
      *host = strdup (sep + 1);
      if (! *host)
	return ENOMEM;
      plim = sep - server;
    }
  else
    {
      sep = strchr (server, ':');
      if (sep)
	/* HOST:... */
	{
	  *host = strndup (server, sep - server);
	  if (! *host)
	    return ENOMEM;
	  p = sep + 1;
	  plim = strlen (p);
	}
      else
	/* Just HOST */
	{
	  *host = strdup (server);
	  if (! *host)
	    return ENOMEM;
	  return 0;
	}
    }

  /* Now P...P+PLIM contains any user parameters for HOST.  */
  sep = memchr (p, ':', plim);
  if (sep)
    /* USERNAME:PASSWD */
    {
      *user = strndup (p, sep - p);
      *passwd = strndup (sep + 1, plim - (sep + 1 - p));
      if (!*user || !*passwd)
	{
	  if (*user)
	    free (*user);
	  if (*passwd)
	    free (*passwd);
	  free (*host);
	  return ENOMEM;
	}
    }
  else
    /* Just USERNAME */
    {
      *user = strndup (p, plim);
      if (! *user)
	free (*user);
    }

  return 0;
}

/*  */
error_t
lookup_server (const char *server, struct ftp_conn_params **params, int *h_err)
{
  char hostent_data[2048];	/* XXX what size should this be???? */
  struct hostent _he, *he;
  char *host, *user, *passwd;
  error_t err = split_server_name (server, &host, &user, &passwd);

  if (err)
    return err;

  /* We didn't find a pre-existing host entry.  Make a new one.  Note that
     since we don't lock anything while making up our new structure, another
     thread could have inserted a duplicate entry for the same host name, but
     this isn't really a problem, just annoying.  */

  if (gethostbyname_r (host, &_he, hostent_data, sizeof hostent_data,
		       &he, h_err) == 0)
    {
      *params = malloc (sizeof (struct ftp_conn_params));
      if (! *params)
	err = ENOMEM;
      else
	{
	  (*params)->addr = malloc (he->h_length);
	  if (! (*params)->addr)
	    {
	      free (*params);
	      err = ENOMEM;
	    }
	  else
	    {
	      bcopy (he->h_addr_list[0], (*params)->addr, he->h_length);
	      (*params)->addr_len = he->h_length;
	      (*params)->addr_type = he->h_addrtype;
	      (*params)->user = user;
	      (*params)->pass = passwd;
	      (*params)->acct = 0;
	    }
	}
    }
  else
    err = EINVAL;

  free (host);

  if (err)
    {
      free (user);
      free (passwd);
    }

  return err;
}
