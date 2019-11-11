/* Hurdish login

   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2002, 2010
   Free Software Foundation, Inc.

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
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <ctype.h>
#include <utmp.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <time.h>
#include <assert-backtrace.h>
#include <version.h>
#include <sys/mman.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/fcntl.h>

#include <argp.h>
#include <argz.h>
#include <envz.h>
#include <idvec.h>
#include <error.h>
#include <timefmt.h>
#include <hurd/lookup.h>
#include <ugids.h>

const char *argp_program_version = STANDARD_HURD_VERSION (login);

extern error_t
exec_reauth (auth_t auth, int secure, int must_reauth,
	     mach_port_t *ports, unsigned num_ports,
	     mach_port_t *fds, unsigned num_fds);
extern error_t
get_nonsugid_ids (struct idvec *uids, struct idvec *gids);

/* Defaults for various login parameters.  */
char *default_args[] = {
  "SHELL=/bin/bash",
  /* A ':' separated list of what to try if can't exec user's shell. */
  "BACKUP_SHELLS=/bin/bash:" _PATH_BSHELL,
  "HOME=/etc/login",		/* Initial WD.  */
  "USER=login",
  "UMASK=022",
  "NAME=Not logged in",
  "HUSHLOGIN=.hushlogin",	/* Looked up relative new home dir.  */
  "MOTD=/etc/motd",
  "PATH=/bin",
  "NOBODY=login",
  "NOAUTH_TIMEOUT=300",		/* seconds before unauthed sessions die. */
  0
};
/* Default values for the new environment.  */
char *default_env[] = {
  "PATH=/bin",
  0
};

/* Which things are copied from the login parameters into the environment. */
char *copied_args[] = {
  "USER", "SHELL", "HOME", "NAME", "VIA", "VIA_ADDR", "PATH", 0
};

static struct argp_option options[] =
{
  {"arg0",	'0', "ARG",   0, "Make ARG the shell's argv[0]"},
  {"envvar",	'e', "ENTRY", 0, "Add ENTRY to the environment"},
  {"envvar-default", 'E', "ENTRY", 0, "Use ENTRY as a default environment variable"},
  {"no-args",	'x', 0,	      0, "Don't put login args into the environment"},
  {"arg",	'a', "ARG",   0, "Add login parameter ARG"},
  {"arg-default", 'A', "ARG", 0, "Use ARG as a default login parameter"},
  {"no-environment-args", 'X', 0, 0, "Don't add the parent environment as default login params"},
  {"no-login",  'L', 0,       0, "Don't modify the shells argv[0] to look"
   " like a login shell"},
  {"preserve-environment", 'p', 0, 0, "Inherit the parent's environment"},
  {"via",	'h', "HOST",  0, "This login is from HOST"},
  {"no-passwd", 'f', 0,       0, "Don't ask for passwords"},
  {"paranoid",  'P', 0,       0, "Don't admit that a user doesn't exist"},
  {"save",      's', 0,       0, "Keep the old available ids, and save the old"
     " effective ids as available ids"},
  {"shell-from-args", 'S', 0, 0, "Use the first shell arg as the shell to invoke"},
  {"retry",     'R', "ARG",   OPTION_ARG_OPTIONAL,
   "Re-exec login with no users after non-fatal errors; if ARG is supplied,"
   "add it to the list of args passed to login when retrying"},
  {0, 0}
};
static struct argp_child child_argps[] =
{
  { &ugids_argp, 0, "Adding individual user/group ids:" },
  { 0 }
};
static char *args_doc = "[USER [ARG...]]";
static char *doc =
"Exec a program with uids and/or the environment changed appropriately.\v"
"To give args to the shell without specifying a user, use - for USER.\n"
"Current login parameters include HOME, SHELL, USER, NAME, and ROOT.";

/* Outputs whatever can be read from the io_t NODE to standard output, and
   then close it.  If NODE is MACH_PORT_NULL, assumes an error happened, and
   prints an error message using ERRNO and STR.  */
static void
cat (mach_port_t node, char *str)
{
  error_t err;
  if (node == MACH_PORT_NULL)
    err = errno;
  else
    for (;;)
      {
	char buf[1024], *data = buf;
	mach_msg_type_number_t data_len = sizeof (buf);

	err = io_read (node, &data, &data_len, -1, 16384);
	if (err || data_len == 0)
	  break;
	else
	  {
	    write (0, data, data_len);
	    if (data != buf)
	      munmap (data, data_len);
	  }
    }
  if (err)
    error (0, errno, "%s", str);
}

/* Add a utmp entry based on the parameters in ARGS & ARGS_LEN.  If
   INHERIT_HOST is true, the host parameters in ARGS aren't to be trusted, so
   try to get the host from the existing utmp entry (this only works if
   re-logging in during an existing session).  */
static void
add_utmp_entry (char *args, unsigned args_len, int inherit_host)
{
  struct utmp utmp;
  char const *host = 0;
  long addr = 0;

  memset (&utmp, 0, sizeof(utmp));

  gettimeofday (&utmp.ut_tv, 0);
  strncpy (utmp.ut_name, envz_get (args, args_len, "USER") ?: "",
	   sizeof (utmp.ut_name));

  if (! inherit_host)
    {
      char *via_addr = envz_get (args, args_len, "VIA_ADDR");
      host = envz_get (args, args_len, "VIA");
      if (host && strlen (host) > sizeof (utmp.ut_host))
	host = via_addr ?: host;
      if (via_addr)
	addr = inet_addr (via_addr);
    }

  if (!host || !addr)
    /* Get the host from the `existing utmp entry'.  This is a crock.  */
    {
      int tty_fd = 0;
      char *tty = 0;

      /* Search for a file descriptor naming a tty.  */
      while (!tty && tty_fd < 3)
	tty = ttyname (tty_fd++);
      if (tty)
	/* Find the old utmp entry for TTY, and grab its host parameters.  */
	{
	  struct utmp *old_utmp;
	  strncpy (utmp.ut_line, basename (tty), sizeof (utmp.ut_line));
	  setutent ();
	  old_utmp = getutline (&utmp);
	  endutent ();
	  if (old_utmp)
	    {
	      if (! host)
		host = old_utmp->ut_host;
	      if (! addr)
		addr = old_utmp->ut_addr;
	    }
	}
    }

  strncpy (utmp.ut_host, host ?: "", sizeof (utmp.ut_host));
  utmp.ut_addr = addr;

  login (&utmp);
}

/* Lookup the host HOST, and add entries for VIA (the host name), and
   VIA_ADDR (the dotted decimal address) to ARGS & ARGS_LEN.  */
static error_t
add_canonical_host (char **args, size_t *args_len, char *host)
{
  struct hostent *he = gethostbyname (host);

  if (he)
    {
      char *addr = 0;

      /* Try and get an ascii version of the numeric host address.  */
      switch (he->h_addrtype)
	{
	case AF_INET:
	  addr = strdup (inet_ntoa (*(struct in_addr *)he->h_addr));
	  break;
	}

      if (addr && strcmp (he->h_name, addr) == 0)
	/* gethostbyname() cheated!  Lookup the host name via the address
	   this time to get the actual host name.  */
	he = gethostbyaddr (he->h_addr, he->h_length, he->h_addrtype);

      if (he)
	host = he->h_name;

      if (addr)
	{
	  envz_add (args, args_len, "VIA_ADDR", addr);
	  free (addr);
	}
    }

  return envz_add (args, args_len, "VIA", host);
}

/* Add the `=' separated environment entry ENTRY to ENV & ENV_LEN, exiting
   with an error message if we can't.  */
static void
add_entry (char **env, size_t *env_len, char *entry)
{
  char *name = strsep (&entry, "=");
  error_t err = envz_add (env, env_len, name, entry);
  if (err)
    error (8, err, "Adding %s", entry);
}

/* Return in OWNED whether PID has an owner, or an error.  */
static error_t
check_owned (process_t proc_server, pid_t pid, int *owned)
{
  int flags = PI_FETCH_TASKINFO;
  char *waits = 0;
  mach_msg_type_number_t num_waits = 0;
  struct procinfo _pi, *pi = &_pi;
  mach_msg_type_number_t pi_size = sizeof _pi / sizeof (*(procinfo_t)0);
  error_t err =
    proc_getprocinfo (proc_server, pid, &flags, (procinfo_t *)&pi, &pi_size,
		      &waits, &num_waits);

  if (! err)
    {
      *owned = !(pi->state & PI_NOTOWNED);
      if (pi != &_pi)
	munmap (pi, pi_size * sizeof (*(procinfo_t)0));
    }

  return err;
}

/* Kills the login session PID with signal SIG.  */
static void
kill_login (process_t proc_server, pid_t pid, int sig)
{
  error_t err;
  size_t num_pids;
  pid_t self = getpid ();

  do
    {
      pid_t _pids[num_pids = 20], *pids = _pids;
      err = proc_getloginpids (proc_server, pid, &pids, &num_pids);
      if (! err)
	{
	  size_t i;
	  for (i = 0; i < num_pids; i++)
	    if (pids[i] != self)
	      kill (pids[i], sig);
	  if (pids != _pids)
	    munmap (pids, num_pids);
	}
    }
  while (!err && num_pids > 0);
}

/* Looks at the login collection LID.  If the root process (PID == LID) is
   owned by someone, then exit (0), otherwise, if it's exited, exit (42).  */
static void
check_login (process_t proc_server, int lid)
{
  int owned;
  error_t err = check_owned (proc_server, lid, &owned);

  if (err == ESRCH)
    exit (42);			/* Nothing left to watch. */
  else
    assert_perror_backtrace (err);

  if (owned)
    exit (0);			/* Our task is done.  */
}

/* Forks a process which will kill the login session headed by PID after
   TIMEOUT seconds if PID still has no owner.  */
static void
dog (time_t timeout, pid_t pid, char **argv)
{
  if (fork () == 0)
    {
      char buf[25];		/* Be gratuitously pretty.  */
      char *name = basename (argv[0]);
      time_t left = timeout;
      struct timeval tv = { 0, 0 };
      process_t proc_server = getproc ();

      while (left)
	{
	  time_t interval = left < 5 ? left : 5;

	  tv.tv_sec = left;

	  /* Frob ARGV so that ps show something nice.  */
	  fmt_named_interval (&tv, 0, buf, sizeof buf);
	  asprintf (&argv[0], "(watchdog for %s %d: %s remaining)",
		    name, pid, buf);
	  argv[1] = 0;

	  sleep (interval);
	  left -= interval;

	  check_login (proc_server, pid);
	}

      check_login (proc_server, pid);

      /* Give you-forgot-to-login message.  */
      tv.tv_sec = timeout;
      fmt_named_interval (&tv, 0, buf, sizeof buf);

      putc ('\n', stderr);	/* Make sure our message starts a line.  */
      error (0, 0, "Timed out after %s.", buf);

      /* Kill login session, trying to be nice about it.  */
      kill_login (proc_server, pid, SIGHUP);
      sleep (5);
      kill_login (proc_server, pid, SIGKILL);
      exit (0);
    }
}

int
main(int argc, char *argv[])
{
  int i;
  io_t node;
  char *arg;
  char *path;
  error_t err = 0;
  char *args = 0;		/* The login parameters */
  size_t args_len = 0;
  char *args_defs = 0;		/* Defaults for login parameters.  */
  size_t args_defs_len = 0;
  char *env = 0;		/* The new environment.  */
  size_t env_len = 0;
  char *env_defs = 0;		/* Defaults for the environment.  */
  size_t env_defs_len = 0;
  char *parent_env = 0;		/* The environment we got from our parent */
  size_t parent_env_len = 0;
  int no_environ = 0;		/* If false, use the env as default params. */
  int no_args = 0;		/* If false, put login params in the env. */
  int inherit_environ = 0;	/* True if we shouldn't clear our env.  */
  int no_passwd = 0;		/* Don't bother verifying what we're doing.  */
  int no_login = 0;		/* Don't prepend `-' to the shells argv[0].  */
  int paranoid = 0;		/* Admit no knowledge.  */
  int retry = 0;		/* For some failures, exec a login shell.  */
  char *retry_args = 0;		/* Args passed when retrying.  */
  size_t retry_args_len = 0;
  char *shell = 0;		/* The shell program to run.  */
  char *sh_arg0 = 0;		/* The shell's argv[0].  */
  char *sh_args = 0;		/* The args to the shell.  */
  size_t sh_args_len = 0;
  int shell_arg = 0;		/* If there are shell args, use the first as
				   the shell name. */
  struct ugids ugids = UGIDS_INIT; /* Authorization of the new shell.  */
  struct ugids_argp_params ugids_argp_params = { &ugids, 0, 0, 0, -1, 0 };
  struct idvec parent_uids = IDVEC_INIT; /* Parent uids, -SETUID. */
  struct idvec parent_gids = IDVEC_INIT; /* Parent gids, -SETGID. */
  mach_port_t exec;		/* The shell executable.  */
  mach_port_t root;		/* The child's root directory.  */
  mach_port_t ports[INIT_PORT_MAX]; /* Init ports for the new process.  */
  int ints[INIT_INT_MAX];	/* Init ints for it.  */
  mach_port_t fds[3];		/* File descriptors passed. */
  mach_port_t auth;		/* The new shell's authentication.  */
  mach_port_t proc_server = getproc ();
  pid_t pid = getpid (), sid;

  /* These three functions are to do child-authenticated lookups.  See
     <hurd/lookup.h> for an explanation.  */
  error_t use_child_init_port (int which, error_t (*operate)(mach_port_t))
    {
      return (*operate)(ports[which]);
    }
  mach_port_t get_child_fd_port (int fd)
    {
      return fd < 0 || fd > 2 ? __hurd_fail (EBADF) : fds[fd];
    }
  mach_port_t child_lookup (char *name, char *path, int flags)
    {
      mach_port_t port = MACH_PORT_NULL;
      errno =
	hurd_file_name_path_lookup (use_child_init_port, get_child_fd_port, 0,
				    name, path, flags, 0, &port, 0);
      return port;
    }

  /* Print an error message with FMT, STR and ERR.  Then, if RETRY is on,
     exec a default login shell, otherwise exit with CODE (must be non-0).  */
  void fail (int code, error_t err, char *fmt, const char *str)
    {
      int retry_argc;
      char **retry_argv;
      char *via = envz_get (args, args_len, "VIA");

      if (fmt)
	error (retry ? 0 : code, err, fmt, str); /* May exit... */
      else if (! retry)
	exit (code);

      if (via)
	envz_add (&retry_args, &retry_args_len, "--via", via);
      argz_insert (&retry_args, &retry_args_len, retry_args, argv[0]);

      retry_argc = argz_count (retry_args, retry_args_len);
      retry_argv = alloca ((retry_argc + 1) * sizeof (char *));
      argz_extract (retry_args, retry_args_len, retry_argv);

      /* Reinvoke ourselves with no userids or anything; shouldn't return.  */
      main (retry_argc, retry_argv);
      exit (code);		/* But if it does... */
    }

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'p': inherit_environ = 1; break;
	case 'x': no_args = 1; break;
	case 'X': no_environ = 1; break;
	case 'e': add_entry (&env, &env_len, arg); break;
	case 'E': add_entry (&env_defs, &env_defs_len, arg); break;
	case 'a': add_entry (&args, &args_len, arg); break;
	case 'A': add_entry (&args_defs, &args_defs_len, arg); break;
	case '0': sh_arg0 = arg; break;
	case 'L': no_login = 1; break;
	case 'f': no_passwd = 1; break;
	case 'P': paranoid = 1; break;
	case 'S': shell_arg = 1; break;

	case 'R':
	  retry = 1;
	  if (arg)
	    {
	      err = argz_add (&retry_args, &retry_args_len, arg);
	      if (err)
		error (10, err, "Adding retry arg %s", arg);
	    }
	  break;

	case 'h':
	  add_canonical_host (&args, &args_len, arg);
	  retry = 1;
	  break;

	case 's':
	  idvec_merge (&ugids.avail_uids, &parent_uids);
	  idvec_merge (&ugids.avail_gids, &parent_gids);
	  break;

	case ARGP_KEY_ARG:
	  if (state->arg_num > 0)
	    /* Program arguments.  */
	    {
	      err = argz_create (state->argv + state->next - 1,
				 &sh_args, &sh_args_len);
	      state->next = state->argc; /* Consume all args */
	      if (err)
		error (9, err, "Adding %s", arg);
	      break;
	    }

	  if (strcmp (arg, "-") == 0)
	    /* An explicit no-user-specified (so remaining args can be used
	       to set the program args).  */
	    break;

	  if (isdigit (*arg))
	    err = ugids_set_posix_user (&ugids, atoi (arg));
	  else
	    {
	      struct passwd *pw = getpwnam (arg);
	      if (pw)
		err = ugids_set_posix_user (&ugids, pw->pw_uid);
	      else if (paranoid)
		/* Add a bogus uid so that password verification will
		   fail.  */
		idvec_add (&ugids.eff_uids, -1);
	      else
		fail (10, 0, "%s: Unknown user", arg);
	    }

	  if (err)
	    fail (11, err, "%s: Can't set user!", arg);

	  break;

	case ARGP_KEY_INIT:
	  state->child_inputs[0] = &ugids_argp_params;
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = { options, parse_opt, args_doc, doc, child_argps };

  /* Don't allow logins if the nologin file exists.  */
  node = file_name_lookup (_PATH_NOLOGIN, O_RDONLY, 0);
  if (node != MACH_PORT_NULL)
    {
      cat (node, _PATH_NOLOGIN);
      exit (40);
    }

  /* Put in certain last-ditch defaults.  */
  err = argz_create (default_args, &args_defs, &args_defs_len);
  if (! err)
    err = argz_create (default_env, &env_defs, &env_defs_len);
  if (! err)
    /* Set the default path using confstr() if possible.  */
    {
      size_t path_len = confstr (_CS_PATH, 0, 0);
      if (path_len > 0)
	{
	  char path[path_len];
	  path_len = confstr (_CS_PATH, path, path_len);
	  if (path_len > 0)
	    err = envz_add (&env_defs, &env_defs_len, "PATH", path);
	}
    }
  if (err)
    error (23, err, "adding defaults");

  err = argz_create (environ, &parent_env, &parent_env_len);

  /* Get authentication of our parent, minus any setuid.  */
  get_nonsugid_ids (&parent_uids, &parent_gids);

  /* Parse our options.  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  /* Check passwords where necessary.  If no_passwd is set, then our parent
     guarantees identity itself (where it is allowed), but otherwise
     we want every UID fully checked.  */
  err = ugids_verify_make_auth (&ugids,
				no_passwd ? &parent_uids : 0,
				no_passwd ? &parent_gids : 0,
				0, 0, 0, 0, &auth);
  if (err == EACCES)
    fail (5, 0, "Invalid password", 0);
  else if (err)
    error (5, err, "Authentication failure");

  /* Now that we've parsed the command line, put together all these
     environments we've gotten from various places.  There are two targets:
     (1) the login parameters, and (2) the child environment.

     The login parameters come from these sources (in priority order):
      a) User specified (with the --arg option)
      b) From the passwd file entry for the user being logged in as
      c) From the parent environment, if --no-environ wasn't specified
      d) From the user-specified defaults (--arg-default)
      e) From last-ditch defaults given by the DEFAULT_* defines above

     The child environment (constructed later) is from:
      a) User specified (--environ)
      b) From the login parameters (if --no-args wasn't specified)
      c) From the parent environment, if --inherit-environ was specified
      d) From the user-specified default env values (--environ-default)
      e) From last-ditch defaults given by the DEFAULT_* defines above
   */
  {
    struct passwd *pw;
    char *passwd = 0;		/* Login parameters from /etc/passwd */
    size_t passwd_len = 0;

    /* Decide which password entry to get parameters from.  */
    if (ugids.eff_uids.num > 0)
      pw = getpwuid (ugids.eff_uids.ids[0]);	/* Effective uid */
    else if (ugids.avail_uids.num > 0)
      pw = getpwuid (ugids.avail_uids.ids[0]);	/* Auxiliary uid */
    else
      /* No user!  Try to used the `not-logged-in' user to set various
	 parameters.  */
      pw = getpwnam (envz_get (args, args_len, "NOBODY")
		     ?: envz_get (args_defs, args_defs_len, "NOBODY")
		     ?: "login");

    if (pw)
      {
	envz_add (&passwd, &passwd_len, "HOME", pw->pw_dir);
	envz_add (&passwd, &passwd_len, "SHELL", pw->pw_shell);
	envz_add (&passwd, &passwd_len, "NAME", pw->pw_gecos);
	envz_add (&passwd, &passwd_len, "USER", pw->pw_name);
      }

    /* Merge the login parameters.  */
    err = envz_merge (&args, &args_len, passwd, passwd_len, 0);
    if (! err && ! no_environ)
      err = envz_merge (&args, &args_len, parent_env, parent_env_len, 0);
    if (! err)
      err = envz_merge (&args, &args_len, args_defs, args_defs_len, 0);
    if (err)
      error (24, err, "merging parameters");

    free (passwd);
  }

  err = proc_getsid (proc_server, pid, &sid);
  assert_perror_backtrace (err);		/* This should never fail.  */

  if (!no_login
      && (parent_uids.num != 0
	  || ugids.eff_uids.num + ugids.avail_uids.num > 0))
    /* Make a new login collection (but only for real users).  */
    {
      char *user = envz_get (args, args_len, "USER");
      if (user && *user)
	setlogin (user);
      proc_make_login_coll (proc_server);

      if (ugids.eff_uids.num + ugids.avail_uids.num == 0)
	/* We're transiting from having some uids to having none, which means
	   this is probably a new login session.  Unless specified otherwise,
	   set a timer to kill this session if it hasn't acquired any ids by
	   then.  Note that we fork off the timer process before clearing the
	   process owner: because we're interested in killing unowned
	   processes, proc's in-same-login-session rule should apply to us
	   (allowing us to kill them), and this way they can't kill the
	   watchdog (because it *does* have an owner).  */
	{
	  char *to = envz_get (args, args_len, "NOAUTH_TIMEOUT");
	  time_t timeout = to ? atoi (to) : 0;
	  if (timeout)
	    dog (timeout, pid, argv);
	}
    }

  if (ugids.eff_uids.num > 0)
    proc_setowner (proc_server, ugids.eff_uids.ids[0], 0);
  else
    proc_setowner (proc_server, 0, 1); /* Clear the owner.  */

  /* Now start constructing the exec arguments.  */
  memset (ints, 0, sizeof (*ints) * INIT_INT_MAX);
  arg = envz_get (args, args_len, "UMASK");
  ints[INIT_UMASK] = arg && *arg ? strtoul (arg, 0, 8) : umask (0);

  for (i = 0; i < 3; i++)
    fds[i] = getdport (i);

  for (i = 0; i < INIT_PORT_MAX; i++)
    ports[i] = MACH_PORT_NULL;
  ports[INIT_PORT_PROC] = getproc ();
  ports[INIT_PORT_CTTYID] = getcttyid ();
  ports[INIT_PORT_CRDIR] = getcrdir ();	/* May be replaced below. */
  ports[INIT_PORT_CWDIR] = getcwdir ();	/*  "  */

  /* Now reauthenticate all of the ports we're passing to the child.  */
  err = exec_reauth (auth, 0, 1, ports, INIT_PORT_MAX, fds, 3);
  if (err)
    error (40, err, "Port reauth failure");

  /* These are the default values for the child's root.  We don't want to
     modify PORTS just yet, because we use it to do child-authenticated
     lookups.  */
  root = ports[INIT_PORT_CRDIR];

  /* Find the shell executable (we copy the name, as ARGS may be changed).  */
  if (shell_arg && sh_args && *sh_args)
    /* Special case for su mode: get the shell from the args if poss.  */
    {
      shell = strdup (sh_args);
      argz_delete (&sh_args, &sh_args_len, sh_args); /* Get rid of it. */
    }
  else
    {
      arg = envz_get (args, args_len, "SHELL");
      if (arg && *arg)
	shell = strdup (arg);
      else
	shell = 0;
    }

  path = envz_get (args, args_len, "PATH");
  exec = shell ? child_lookup (shell, path, O_EXEC) : MACH_PORT_NULL;
  if (exec == MACH_PORT_NULL)
    {
      char *backup = 0;
      char *backups = envz_get (args, args_len, "BACKUP_SHELLS");
      err = errno;		/* Save original lookup errno. */

      if (backups && *backups)
	{
	  backups = strdupa (backups); /* Copy so we can trash it. */
	  while (exec == MACH_PORT_NULL && backups)
	    {
	      backup = strsep (&backups, ":, ");
	      if (*backup && (!shell || strcmp (shell, backup) != 0))
		exec = child_lookup (backup, path, O_EXEC);
	    }
	}

      /* Give the error message, but only exit if we couldn't default. */
      if (exec == MACH_PORT_NULL)
	fail (1, err, "%s", shell);
      else
	error (0, err, "%s", shell);

      /* If we get here, we looked up the default shell ok.  */
      shell = strdup (backup);
      error (0, 0, "Using SHELL=%s", shell);
      envz_add (&args, &args_len, "SHELL", shell);
      err = 0;			/* Don't emit random err msgs later!  */
    }

  /* Now maybe change the cwd/root in the child.  */

  arg = envz_get (args, args_len, "HOME");
  if (arg && *arg)
    {
      mach_port_t cwd = child_lookup (arg, 0, O_RDONLY);
      if (cwd == MACH_PORT_NULL)
	{
	  error (0, errno, "%s", arg);
	  error (0, 0, "Using HOME=/");
	  envz_add (&args, &args_len, "HOME", "/");
	}
      else
        ports[INIT_PORT_CWDIR] = cwd;
    }

  arg = envz_get (args, args_len, "ROOT");
  if (arg && *arg)
    {
      root = child_lookup (arg, 0, O_RDONLY);
      if (root == MACH_PORT_NULL)
	fail (40, errno, "%s", arg);
    }

  /* Build the child environment.  */
  if (! no_args)
    /* We can't just merge ARGS, because it may contain the parent
       environment, which we don't always want in the child environment, so
       we pick out only those values of args which actually *are* args.  */
    {
      char **name;
      char *user = envz_get (args, args_len, "USER");

      for (name = copied_args; *name && !err; name++)
	if (! envz_get (env, env_len, *name))
	  {
	    char *val = envz_get (args, args_len, *name);
	    if (val && *val)
	      err = envz_add (&env, &env_len, *name, val);
	  }

      if (user)
	/* Copy the user arg into the environment as LOGNAME.  */
	err = envz_add (&env, &env_len, "LOGNAME", user);
    }
  if (! err && inherit_environ)
    err = envz_merge (&env, &env_len, parent_env, parent_env_len, 0);
  if (! err)
    err = envz_merge (&env, &env_len, env_defs, env_defs_len, 0);
  if (err)
    error (24, err, "Can't build environment");

  if (! sh_arg0)
    /* The shells argv[0] defaults to the basename of the shell.  */
    {
      char *shell_base = rindex (shell, '/');
      if (shell_base)
	shell_base++;
      else
	shell_base = shell;

      if (no_login)
	sh_arg0 = shell_base;
      else if (ugids.eff_uids.num + ugids.avail_uids.num == 0)
	/* Use a special format for the argv[0] of a login prompt shell,
	   so that `ps' shows something informative in the COMMAND field.
	   This string must begin with a `-', the convention to tell the
	   shell to be a login shell (i.e. run .profile and the like).  */
	err = (asprintf (&sh_arg0, "-login prompt (%s)", shell_base) == -1
	       ? ENOMEM : 0);
      else
	/* Prepend a `-' to the name, which is the ancient canonical
	   way to tell the shell that it's a login shell.  */
	err = asprintf (&sh_arg0, "-%s", shell_base) == -1 ? ENOMEM : 0;
    }
  if (! err)
    err = argz_insert (&sh_args, &sh_args_len, sh_args, sh_arg0);
  if (err)
    error (21, err, "Error building shell args");

  /* Maybe output the message of the day.  Note that we use the child's
     authentication to do it, so that this program can't be used to read
     arbitrary files!  */
  arg = envz_get (args, args_len, "MOTD");
  if (arg && *arg)
    {
      char *hush = envz_get (args, args_len, "HUSHLOGIN");
      mach_port_t hush_node =
	(hush && *hush) ? child_lookup (hush, 0, O_RDONLY) : MACH_PORT_NULL;
      if (hush_node == MACH_PORT_NULL)
	{
	  mach_port_t motd_node = child_lookup (arg, 0, O_RDONLY);
	  if (motd_node != MACH_PORT_NULL)
	    cat (motd_node, arg);
	}
      else
	mach_port_deallocate (mach_task_self (), hush_node);
    }

  /* Now that we don't need to use PORTS for lookups anymore, put the correct
     ROOT in.  */
  ports[INIT_PORT_CRDIR] = root;

  /* Get rid of any accumulated null entries in env.  */
  envz_strip (&env, &env_len);

  /* No more authentications to fail, so cross our fingers and add our utmp
     entry.  */

  if (pid == sid)
    /* Only add utmp entries for the session leader.  */
    add_utmp_entry (args, args_len, !idvec_contains (&parent_uids, 0));

  if ((ugids.eff_uids.num | ugids.eff_gids.num) && !no_login)
    {
      char *tty = ttyname (0);
      if (tty)
	{
	  /* Change the terminal to be owned by the user.  */
	  err = chown (tty,
		       ugids.eff_uids.num ? ugids.eff_uids.ids[0] : -1,
		       ugids.eff_gids.num ? ugids.eff_gids.ids[0] : -1);
	  if (err)
	    error (0, errno, "chown: %s", tty);
	}
    }

#ifdef HAVE_FILE_EXEC_PATHS
  err = file_exec_paths (exec, mach_task_self (), EXEC_DEFAULTS, shell, shell,
			 sh_args, sh_args_len, env, env_len,
			 fds, MACH_MSG_TYPE_COPY_SEND, 3,
			 ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
			 ints, INIT_INT_MAX,
			 0, 0, 0, 0);
  /* Fallback in case the file server hasn't been restarted.  */
  if (err == MIG_BAD_ID)
#endif
    err = file_exec (exec, mach_task_self (), EXEC_DEFAULTS,
		     sh_args, sh_args_len, env, env_len,
		     fds, MACH_MSG_TYPE_COPY_SEND, 3,
		     ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
		     ints, INIT_INT_MAX,
		     0, 0, 0, 0);
  if (err)
    error(5, err, "%s", shell);

  return 0;
}
