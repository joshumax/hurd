/* Copy a file using the ftp protocol

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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <argp.h>
#include <netdb.h>
#include <fcntl.h>

#include <version.h>

#include <ftpconn.h>

#define COPY_SZ 65536

const char *argp_program_version = STANDARD_HURD_VERSION (ftpcp);

#define OPT_SRC_U -3
#define OPT_SRC_A -4
#define OPT_SRC_P -5
#define OPT_DST_U -6
#define OPT_DST_A -7
#define OPT_DST_P -8


static struct argp_option options[] =
{
  {"user",        'u',       "USER",0, "User to login as on both ftp servers"},
  {"password",    'p',       "PWD", 0, "USER's password"},
  {"account",     'a',       "ACCT",0, "Account to login as"}, 
  {"src-user",    OPT_SRC_U, "USER",0, "User to login as on the src ftp server"},
  {"src-password",OPT_SRC_P, "PWD", 0, "The src USER's password"},
  {"src-account", OPT_SRC_A, "ACCT",0, "Account to login as on the source server"}, 
  {"dst-user",    OPT_DST_U, "USER",0, "User to login as on the dst ftp server"},
  {"dst-password",OPT_DST_P, "PWD", 0, "The dst USER's password"},
  {"dst-account", OPT_DST_A, "ACCT",0, "Account to login as on the source server"}, 
  {"debug",    'D', 0,     0, "Turn on debugging output for ftp connections"},
  {0, 0}
};
static char *args_doc = "SRC [DST]";
static char *doc = "Copy file SRC over ftp to DST."
"\vBoth SRC and DST may have the form HOST:FILE, FILE, or -, where - is"
" standard input for SRC or standard output for DST, and FILE is a local"
" file.  DST may be a directory, in which case the basename of SRC is"
" appended to make the actual destination filename.";

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

/* Return an ftp connection for the host NAME using PARAMS.  If an error
   occurrs, a message is printed the program exits.  If CNAME is non-zero,
   the host's canonical name, in mallocated storage, is returned in it. */
struct ftp_conn *
get_host_conn (char *name, struct ftp_conn_params *params, char **cname)
{
  error_t err;
  struct hostent *he;
  struct ftp_conn *conn;

  he = gethostbyname (name);
  if (! he)
    error (10, 0, "%s: %s", name, hstrerror (h_errno));

  params->addr = malloc (he->h_length);
  if (! params->addr)
    error (11, ENOMEM, "%s", name);

  bcopy (he->h_addr_list[0], params->addr, he->h_length);
  params->addr_len = he->h_length;
  params->addr_type = he->h_addrtype;

  err = ftp_conn_create (params, &conn_hooks, &conn);
  if (err)
    error (12, err, "%s", he->h_name);

  if (cname)
    *cname = strdup (he->h_name);

  return conn;
}

static void
cp (int src, const char *src_name, int dst, const char *dst_name)
{
  ssize_t rd;
  static void *copy_buf = 0;

  if (! copy_buf)
    {
      copy_buf = valloc (COPY_SZ);
      if (! copy_buf)
	error (13, ENOMEM, "Cannot allocate copy buffer");
    }

  while ((rd = read (src, copy_buf, COPY_SZ)) > 0)
    do
      {
	int wr = write (dst, copy_buf, rd);
	if (wr < 0)
	  error (14, errno, "%s", dst_name);
	rd -= wr;
      }
    while (rd > 0);

  if (rd != 0)
    error (15, errno, "%s", src_name);
}

struct epoint
{
  char *name;			/* Name, of the form HOST:FILE, FILE, or -.  */
  char *file;			/* If remote, the FILE portion, or 0. */
  int fd;			/* A file descriptor to use.  */
  struct ftp_conn *conn;	/* An ftp connection to use.  */
  struct ftp_conn_params params;
};

static void
econnect (struct epoint *e, struct ftp_conn_params *def_params)
{
  char *rmt;

  if (! e->name)
    e->name = "-";

  rmt = strchr (e->name, ':');
  if (rmt)
    {
      error_t err;

      *rmt++ = 0;

      if (! e->params.user)
	e->params.user = def_params->user;
      if (! e->params.pass)
	e->params.pass = def_params->pass;
      if (! e->params.acct)
	e->params.acct = def_params->acct;

      e->conn = get_host_conn (e->name, &e->params, &e->name);
      e->name = realloc (e->name, strlen (e->name) + 1 + strlen (rmt) + 1);
      if (! e->name)
	error (22, ENOMEM, "Cannot allocate name storage");

      err = ftp_conn_set_type (e->conn, "I");
      if (err)
	error (23, err, "%s: Cannot set connection type to binary",
	       e->name);

      strcat (e->name, ":");
      strcat (e->name, rmt);

      e->file = rmt;
    }
  else if (e->params.user || e->params.pass || e->params.acct)
    error (20, 0,
	   "%s: Ftp login parameter specified for a local endpoint (%s,%s,%s)",
	   e->name, e->params.user, e->params.pass, e->params.acct);
  else
    e->file = strdup (e->name);
}

static error_t
eopen_wr (struct epoint *e, int *fd)
{
  if (e->conn)
    return ftp_conn_start_store (e->conn, e->file, fd);
  else if (strcmp (e->name, "-") == 0)
    *fd = 1;
  else
    {
      *fd = open (e->name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (*fd < 0)
	return errno;
    }
  return 0;
}

static error_t
eopen_rd (struct epoint *e, int *fd)
{
  if (e->conn)
    return ftp_conn_start_retrieve (e->conn, e->file, fd);
  else if (strcmp (e->name, "-") == 0)
    *fd = 0;
  else
    {
      *fd = open (e->name, O_RDONLY, 0666);
      if (*fd < 0)
	return errno;
    }
  return 0;
}

static void
efinish (struct epoint *e)
{
  if (e->conn)
    {
      error_t err = ftp_conn_finish_transfer (e->conn);
      if (err)
	error (31, err, "%s", e->name);
    }	      
}

int 
main (int argc, char **argv)
{
  error_t err;
  struct epoint rd = { 0 }, wr = { 0 };
  struct ftp_conn_params def_params = { 0 }; /* default params */

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  switch (state->arg_num)
	    {
	    case 0: rd.name = arg; break;
	    case 1: wr.name = arg; break;
	    default: return ARGP_ERR_UNKNOWN;
	    }
	  break;
	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);

	case 'u': def_params.user = arg; break;
	case 'p': def_params.pass = arg; break;
	case 'a': def_params.acct = arg; break;

	case OPT_SRC_U: rd.params.user = arg; break;
	case OPT_SRC_P: rd.params.pass = arg; break;
	case OPT_SRC_A: rd.params.acct = arg; break;

	case OPT_DST_U: wr.params.user = arg; break;
	case OPT_DST_P: wr.params.pass = arg; break;
	case OPT_DST_A: wr.params.acct = arg; break;

	case 'D': conn_hooks.cntl_debug = cntl_debug; break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  econnect (&rd, &def_params);
  econnect (&wr, &def_params);

  if (rd.conn && wr.conn)
    {
      err = ftp_conn_rmt_copy (rd.conn, rd.file, wr.conn, wr.file);
      if (err)
	error (30, err, "Remote copy");
    }
  else
    {
      int rd_fd, wr_fd;

      err = eopen_rd (&rd, &rd_fd);
      if (err)
	error (31, err, "%s", rd.name);

      err = eopen_wr (&wr, &wr_fd);
      if (err == EISDIR)
	/* The destination name is a directory; try again with the source
	   basename appended.  */
	{
	  char *bname = basename (rd.file);
	  size_t bname_len = strlen (bname);
	  char *dir = wr.file;
	  char *file = malloc (strlen (dir) + 1 + bname_len + 1);
	  char *name = malloc (strlen (wr.name) + 1 + bname_len + 1);

	  if (!file || !name)
	    error (99, ENOMEM, "%s", dir);

	  stpcpy (stpcpy (stpcpy (file, dir), "/"), bname);
	  wr.file = file;
	  stpcpy (stpcpy (stpcpy (name, wr.name), "/"), bname);
	  wr.name = name;

	  err = eopen_wr (&wr, &wr_fd);
	}
      if (err)
	error (32, err, "%s", wr.name);

      cp (rd_fd, rd.name, wr_fd, wr.name);

      close (rd_fd);
      close (wr_fd);

      efinish (&rd);
      efinish (&wr);
    }

  exit (0);
}
