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
"\vBoth SRC and DST may have the form HOST:FILE, FILE, or -, where - is standard"
" input for SRC or standard output for DST, and FILE is a local file.";

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
  char *rmt_file;		/* If NAME is remote, the FILE portion, or 0. */
  char *rmt_host;		/* If NAME is remote, the HOST portion, or 0. */
  int fd;			/* A file descriptor to use.  */
  struct ftp_conn *conn;	/* An ftp connection to use.  */
  struct ftp_conn_params params;
};

int 
main (int argc, char **argv)
{
  int i;
  error_t err;
  struct epoint epoints[2] = { {0}, {0} };
  struct ftp_conn_params def_params = { 0 }; /* default params */

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  if (state->arg_num < 2)
	    epoints[state->arg_num].name = arg;
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);

	case 'u': def_params.user = arg; break;
	case 'p': def_params.pass = arg; break;
	case 'a': def_params.acct = arg; break;

	case OPT_SRC_U: epoints[0].params.user = arg; break;
	case OPT_SRC_P: epoints[0].params.pass = arg; break;
	case OPT_SRC_A: epoints[0].params.acct = arg; break;

	case OPT_DST_U: epoints[1].params.user = arg; break;
	case OPT_DST_P: epoints[1].params.pass = arg; break;
	case OPT_DST_A: epoints[1].params.acct = arg; break;

	case 'D': conn_hooks.cntl_debug = cntl_debug; break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  for (i = 0; i < 2; i++)
    {
      char *rmt;

      if (! epoints[i].name)
	epoints[i].name = "-";

      rmt = strchr (epoints[i].name, ':');
      if (rmt)
	{
	  *rmt++ = 0;

	  if (! epoints[i].params.user)
	    epoints[i].params.user = def_params.user;
	  if (! epoints[i].params.pass)
	    epoints[i].params.pass = def_params.pass;
	  if (! epoints[i].params.acct)
	    epoints[i].params.acct = def_params.acct;

	  epoints[i].conn =
	    get_host_conn (epoints[i].name, &epoints[i].params,
			   &epoints[i].name);
	  epoints[i].name =
	    realloc (epoints[i].name,
		     strlen (epoints[i].name) + 1 + strlen (rmt) + 1);
	  if (! epoints[i].name)
	    error (22, ENOMEM, "Cannot allocate name storage");

	  err = ftp_conn_set_type (epoints[i].conn, "I");
	  if (err)
	    error (23, err, "%s: Cannot set connection type to binary",
		   epoints[i].name);

	  strcat (epoints[i].name, ":");
	  strcat (epoints[i].name, rmt);

	  epoints[i].rmt_file = rmt;
	}
      else if (epoints[i].params.user
	       || epoints[i].params.pass
	       || epoints[i].params.acct)
	error (20, 0,
	       "%s: Ftp login parameter specified for a local endpoint (%s,%s,%s)",
	       epoints[i].name,
	       epoints[i].params.user,
	       epoints[i].params.pass,
	       epoints[i].params.acct);
    }

  if (epoints[0].conn && epoints[1].conn)
    {
      err = ftp_conn_rmt_copy (epoints[0].conn, epoints[0].rmt_file,
			       epoints[1].conn, epoints[1].rmt_file);
      if (err)
	error (30, err, "Remote copy");
    }
  else
    {
      for (i = 0; i < 2; i++)
	if (epoints[i].conn)
	  {
	    if (i == 0)
	      err = ftp_conn_start_retrieve (epoints[i].conn,
					     epoints[i].rmt_file,
					     &epoints[i].fd);
	    else
	      err = ftp_conn_start_store (epoints[i].conn,
					  epoints[i].rmt_file,
					  &epoints[i].fd);
	    if (err)
	      error (31, err, "%s", epoints[i].name);
	  }
	else if (strcmp (epoints[i].name, "-") == 0)
	  epoints[i].fd = i;
	else
	  {
	    int flags = (i == 0) ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
	    epoints[i].fd = open (epoints[i].name, flags, 0666);
	    if (epoints[i].fd < 0)
	      error (32, errno, "%s", epoints[i].name);
	  }

      cp (epoints[0].fd, epoints[0].name, epoints[1].fd, epoints[1].name);

      for (i = 0; i < 2; i++)
	{
	  close (epoints[i].fd);
	  if (epoints[i].conn)
	    {
	      err = ftp_conn_finish_transfer (epoints[i].conn);
	      if (err)
		error (31, err, "%s", epoints[i].name);
	    }
	}	      
    }

  exit (0);
}
