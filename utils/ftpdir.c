/* Get a directory listing using the ftp protocol

   Copyright (C) 1997,2002 Free Software Foundation, Inc.
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

#include <unistd.h>
#include <string.h>
#include <error.h>
#include <argp.h>
#include <time.h>
#include <netdb.h>

#include <version.h>

#include <ftpconn.h>

#define COPY_SZ 65536

const char *argp_program_version = STANDARD_HURD_VERSION (ftpdir);

static struct argp_option options[] =
{
  {"user",     'u', "USER",0, "User to login as on ftp server"},
  {"password", 'p', "PWD", 0, "USER's password"},
  {"account",  'a', "ACCT",0, "Account to login as"},
  {"separator",'S', "SEP", 0, "String to separate multiple listings"},
  {"prefix",   'P', "PFX", 0, "String to proceed listings; the first and second"
                              " occurrences of %s are replace by HOST and DIR"},
  {"host",     'h', "HOST",0, "Use HOST as a default host"},
  {"debug",    'D', 0,     0, "Turn on debugging output for ftp connections"},
  {"intepret", 'i', 0,     0, "Parse the directory output"},
  {0, 0}
};
static char *args_doc = "[([HOST:]DIR | HOST:)...]";
static char *doc = "Get a directory listing over ftp from HOST:DIR."
"\vIf HOST is not supplied in an argument any default value set by --host is"
" used; if DIR is not supplied, the default directory of HOST is used."
"\nIf multiple DIRs are supplied on the command line, each listing is"
" prefixed by a newline (or SEP) and a line containing HOST:DIR: (or PFX).";

/* customization hooks.  */
static struct ftp_conn_hooks conn_hooks = { 0 };

static void
cntl_debug (struct ftp_conn *conn, int type, const char *txt)
{
  char *type_str;

  switch (type)
    {
    case FTP_CONN_CNTL_DEBUG_CMD:   type_str = "."; break;
    case FTP_CONN_CNTL_DEBUG_REPLY: type_str = "="; break;
    default: type_str = "?"; break;
    }

  fprintf (stderr, "%s%s\n", type_str, txt);
}

struct ftpdir_host
{
  char *name;
  struct ftp_conn_params params;
  struct ftp_conn *conn;
  struct ftpdir_host *next;
};

/* Return an ftp connection for the host NAME using PARAMS, and add an entry
   for it to *HOSTS.  If a connection already exists in HOSTS, it is returned
   instead of making a new one.  If an error occurrs, a message is printed and
   0 is returned.  */
static struct ftpdir_host *
get_host_conn (char *name, struct ftp_conn_params *params,
	       struct ftpdir_host **hosts)
{
  error_t err;
  struct ftpdir_host *h;
  struct hostent *he;

  for (h = *hosts; h; h = h->next)
    if (strcmp (h->name, name) == 0)
      return h;

  he = gethostbyname (name);
  if (! he)
    {
      error (0, 0, "%s: %s", name, hstrerror (h_errno));
      return 0;
    }

  for (h = *hosts; h; h = h->next)
    if (he->h_addrtype == h->params.addr_type
	&& he->h_length == h->params.addr_len
	&& bcmp (he->h_addr_list[0], h->params.addr, he->h_length) == 0)
      return h;

  h = malloc (sizeof (struct ftpdir_host));
  if (! h)
    {
      error (0, ENOMEM, "%s", name);
      return 0;
    }

  h->params = *params;
  h->params.addr = malloc (he->h_length);
  h->name = strdup (he->h_name);

  if (!h->name || !h->params.addr)
    err = ENOMEM;
  else
    {
      bcopy (he->h_addr_list[0], h->params.addr, he->h_length);
      h->params.addr_len = he->h_length;
      h->params.addr_type = he->h_addrtype;
      err = ftp_conn_create (&h->params, &conn_hooks, &h->conn);
    }

  if (err)
    {
      error (0, err, "%s", he->h_name);
      if (h->name)
	free (h->name);
      if (h->params.addr)
	free (h->params.addr);
      free (h);
      return 0;
    }

  h->next = *hosts;
  *hosts = h;

  return h;
}

static int
ftpdir (char *dir, struct ftpdir_host *host)
{
  int data;
  int rd;
  error_t err;
  static void *copy_buf = 0;
  struct ftp_conn *conn = host->conn;
  char *host_name = host->name;

  err = ftp_conn_start_dir (conn, dir, &data);
  if (err)
    {
      error (0, err, "%s:%s", host_name, dir);
      return err;
    }

  if (! copy_buf)
    {
      copy_buf = valloc (COPY_SZ);
      if (! copy_buf)
	error (12, ENOMEM, "Cannot allocate copy buffer");
    }

  while ((rd = read (data, copy_buf, COPY_SZ)) > 0)
    do
      {
	int wr = write (1, copy_buf, rd);
	if (wr < 0)
	  error (13, errno, "stdout");
	rd -= wr;
      }
    while (rd > 0);
  if (rd != 0)
    {
      error (0, errno, "%s:%s", host_name, dir);
      return errno;
    }

  close (data);

  err = ftp_conn_finish_transfer (conn);
  if (err)
    {
      error (0, err, "%s:%s", host_name, dir);
      return err;
    }

  return 0;
}

static error_t
pdirent (const char *name, const struct stat *st, const char *symlink_target,
	 void *hook)
{
  char timebuf[20];
  strftime (timebuf, sizeof timebuf, "%Y-%m-%d %H:%M", localtime (&st->st_mtime));
  printf ("%6o %2d %5d %5d %6lld  %s  %s\n",
	  st->st_mode, st->st_nlink, st->st_uid, st->st_gid, st->st_size,
	  timebuf, name);
  if (symlink_target)
    printf ("                                             -> %s\n",
	    symlink_target);
  return 0;
}

static error_t
ftpdir2 (char *dir, struct ftpdir_host *host)
{
  error_t err = ftp_conn_get_stats (host->conn, dir, 1, pdirent, 0);
  if (err == ENOTDIR)
    err = ftp_conn_get_stats (host->conn, dir, 0, pdirent, 0);
  if (err)
    error (0, err, "%s:%s", host->name, dir);
  return err;
}

int
main (int argc, char **argv)
{
  struct ftpdir_host *hosts = 0;
  char *default_host = 0;
  int interpret = 0;
  struct ftp_conn_params params = { 0 };
  char *sep = "\n";
  char *pfx = "%s:%s:\n";
  int use_pfx = 0;
  int errs = 0;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  {
	    char *host, *dir;

	    if (state->next < state->argc)
	      use_pfx = 1;	/* Multiple arguments. */

	    dir = index (arg, ':');
	    if (dir)
	      {
		host = arg;
		*dir++ = '\0';
		if (*host == '\0')
		  /* An argument of `:' */
		  host = default_host;
	      }
	    else
	      {
		host = default_host;
		dir = arg;
	      }

	    if (host)
	      {
		struct ftpdir_host *h = get_host_conn (host, &params, &hosts);
		if (h)
		  {
		    if (state->arg_num > 0)
		      fputs (sep, stdout);
		    if (use_pfx)
		      printf (pfx, h->name, dir);
		    if ((use_pfx && *pfx) || (state->arg_num > 0 && *sep))
		      fflush (stdout);
		    if (interpret)
		      errs |= ftpdir2 (dir, h);
		    else
		      errs |= ftpdir (dir, h);
		  }
		errs = 1;
	      }
	    else
	      {
		error (0, 0, "%s: No default host", arg);
		errs = 1;
	      }
	  }
	  break;

	case ARGP_KEY_NO_ARGS:
	  if (default_host)
	    {
	      struct ftpdir_host *h =
		get_host_conn (default_host, &params, &hosts);
	      if (h)
		errs |= ftpdir (0, h);
	    }
	  else
	    {
	      error (0, 0, "No default host");
	      errs = 1;
	    }
	  break;

	  return EINVAL;

	case 'u': params.user = arg; break;
	case 'p': params.pass = arg; break;
	case 'a': params.acct = arg; break;
	case 'h': default_host = arg; break;
	case 'D': conn_hooks.cntl_debug = cntl_debug; break;
	case 'P': pfx = arg; use_pfx = 1; break;
	case 'S': sep = arg; break;
	case 'i': interpret = 1; break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (errs)
    exit (10);
  else
    exit (0);
}
