/* Hurdish login

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
#include <unistd.h>
#include <paths.h>
#include <ctype.h>
#include <utmp.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <time.h>

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

extern error_t
exec_reauth (auth_t auth, int secure, int must_reauth,
	     mach_port_t *ports, unsigned num_ports,
	     mach_port_t *fds, unsigned num_fds);

extern error_t
hurd_file_name_path_lookup (error_t (*use_init_port)
			    (int which,
			     error_t (*operate) (mach_port_t)),
			    file_t (*get_dtable_port) (int fd),
			    const char *file_name, const char *path,
			    int flags, mode_t mode,
			    file_t *result, char **prefixed_name);

/* Defaults for various login parameters.  */
char *default_args[] = {
  "SHELL=/bin/bash",
  /* A ':' separated list of what to try if can't exec user's shell. */
  "BACKUP_SHELLS=/bin/bash:" _PATH_BSHELL,
  "HOME=/etc/login",		/* Initial WD.  */
  "USER=login",
  "UMASK=0",
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
  {"environ",	'e', "ENTRY", 0, "Add ENTRY to the environment"},
  {"environ-default", 'E', "ENTRY", 0, "Use ENTRY as a default environment variable"},
  {"no-args",	'x', 0,	      0, "Don't put login args into the environment"},
  {"arg",	'a', "ARG",   0, "Add login parameter ARG"},
  {"arg-default", 'A', "ARG", 0, "Use ARG as a default login parameter"},
  {"no-environ", 'X', 0,      0, "Don't add the parent environment as default login params"},
  {"user",	'u', "USER",  0, "Add USER to the effective uids"},
  {"avail-user",'U', "USER",  0, "Add USER to the available uids"},
  {"group",     'g', "GROUP", 0, "Add GROUP to the effective groups"},
  {"avail-group",'G',"GROUP", 0, "Add GROUP to the available groups"},
  {"no-login",  'L', 0,       0, "Don't modify the shells argv[0] to look"
     " like a login shell"},
  {"inherit-environ", 'p', 0, 0, "Inherit the parent's environment"},
  {"via",	'h', "HOST",  0, "This login is from HOST"},
  {"no-passwd", 'f', 0,       0, "Don't ask for passwords"},
  {"paranoid",  'P', 0,       0, "Don't admit that a user doesn't exist"},
  {"keep",      'k', 0,       0, "Keep the old available ids, and save the old"
     "effective ids as available ids"},
  {"shell-from-args", 'S', 0, 0, "Use the first shell arg as the shell to invoke"},
  {"retry",     'R', "ARG",   OPTION_ARG_OPTIONAL,
     "Re-exec login with no users after non-fatal errors; if ARG is supplied,"
     "add it to the list of args passed to login when retrying"},
  {0, 0}
};
static char *args_doc = "[USER [ARG...]]";
static char *doc =
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
	      vm_deallocate (mach_task_self (), (vm_address_t)data, data_len);
	  }
    }
  if (err)
    error (0, errno, "%s", str);
}

/* Returns the host from the umtp entry for the current tty, or 0.  The
   return value is in a static buffer.  */
static char *
get_utmp_host ()
{
  static struct utmp utmp;
  int tty = ttyslot ();
  char *host = 0;

  if (tty > 0)
    {
      int fd = open (_PATH_UTMP, O_RDONLY);
      if (fd >= 0)
	{
	  lseek (fd, (off_t)(tty * sizeof (struct utmp)), L_SET);
	  if (read (fd, &utmp, sizeof utmp) == sizeof utmp
	      && *utmp.ut_name && *utmp.ut_line && *utmp.ut_host)
	    host = utmp.ut_host;
	  close (fd);
	}
    }

  return host;
}

/* Add a utmp entry based on the parameters in ARGS & ARGS_LEN, from tty
   TTY_FD.  If INHERIT_HOST is true, the host parameters in ARGS aren't to be
   trusted, so try to get the host from the existing utmp entry (this only
   works if re-logging in during an existing session).  */
static void
add_utmp_entry (char *args, unsigned args_len, int tty_fd, int inherit_host)
{
  struct utmp utmp;
  char bogus_tty[sizeof (_PATH_TTY) + 2];
  char *tty = ttyname (0);
  char const *host;

  if (! tty)
    {
      sprintf (bogus_tty, "%s??", _PATH_TTY);
      tty = bogus_tty;
    }

  if (strncmp (tty, _PATH_DEV, sizeof (_PATH_DEV) - 1) == 0)
    /* Remove a prefix of `/dev/'.  */
    tty += sizeof (_PATH_DEV) - 1;

  bzero (&utmp, sizeof (utmp));

  time (&utmp.ut_time);
  strncpy (utmp.ut_name, envz_get (args, args_len, "USER") ?: "",
	   sizeof (utmp.ut_name));
  strncpy (utmp.ut_line, tty, sizeof (utmp.ut_line));

  if (inherit_host)
    host = get_utmp_host ();
  else
    {
      host = envz_get (args, args_len, "VIA");
      if (host && strlen (host) > sizeof (utmp.ut_host))
	host = envz_get (args, args_len, "VIA_ADDR") ?: host;
    }
  strncpy (utmp.ut_host, host ?: "", sizeof (utmp.ut_host));

  login (&utmp);
}

/* Lookup the host HOST, and add entries for VIA (the host name), and
   VIA_ADDR (the dotted decimal address) to ARGS & ARGS_LEN.  */
static error_t
add_canonical_host (char **args, unsigned *args_len, char *host)
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
add_entry (char **env, unsigned *env_len, char *entry)
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
  mach_msg_type_number_t pi_size = sizeof pi;
  error_t err =
    proc_getprocinfo (proc_server, pid, &flags, (procinfo_t *)&pi, &pi_size,
		      &waits, &num_waits);

  if (! err)
    {
      *owned = pi->state & PI_NOTOWNED;
      if (pi != &_pi)
	vm_deallocate (mach_task_self (), (vm_address_t)pi, pi_size);
    }

  return err;
}

/* Kills the login session PID with signal SIG.  */
static void
kill_login (process_t proc_server, pid_t pid, int sig)
{
  error_t err;
  size_t num_pids;
  do
    {
      pid_t _pids[num_pids = 20], *pids = _pids;
      err = proc_getloginpids (proc_server, pid, &pids, &num_pids);
      if (! err)
	{
	  size_t i;
	  for (i = 0; i < num_pids; i++)
	    kill (pids[i], sig);
	  if (pids != _pids)
	    vm_deallocate (mach_task_self (), (vm_address_t)pids, num_pids);
	}
    }
  while (!err && num_pids > 0);
}

/* Forks a process which will kill the login session headed by PID after
   TIMEOUT seconds if PID still has no owner.  */
static void
dog (time_t timeout, pid_t pid)
{
  if (fork () == 0)
    {
      int owned;
      error_t err;
      process_t proc_server = getproc ();

      sleep (timeout);

      err = check_owned (proc_server, pid, &owned);
      if (err == ESRCH)
	/* The process has gone away.  Maybe someone is trying to play games;
	   just see if *any* of the remaing processes in the login session
	   are owned, and give up if so (this can be foiled by setuid
	   processes, &c, but oh well; they can be set non-executable by
	   nobody).  */
	{
	  size_t num_pids = 20, i;
	  pid_t _pids[num_pids], *pids = _pids;
	  err = proc_getloginpids (proc_server, pid, &pids, &num_pids);
	  if (! err)
	    for (i = 0; i < num_pids; i++)
	      if (check_owned (proc_server, pids[i], &owned) == 0 && owned)
		exit (0);	/* Give up, luser wins. */
	  /* None are owned.  Kill session after emitting cryptic, yet
	     stupid, message. */
	  putc ('\n', stderr);
	  error (0, 0, "Beware of dog.");
	}
      else if (err)
	exit (1);		/* Impossible error.... XXX  */
      else if (owned)
	exit (0);		/* Use logged in.  */
      else
	/* Give normal you-forgot-to-login message.  */
	{
	  char interval[10];	/* Be gratuitously pretty.  */
	  struct timeval tv = { timeout, 0 };

	  fmt_named_interval (&tv, 0, interval, sizeof interval);

	  putc ('\n', stderr);
	  error (0, 0, "Timed out after %s.", interval);
	}

      /* Kill login session, trying to be nice about it.  */
      kill_login (proc_server, pid, SIGHUP);
      sleep (5);
      kill_login (proc_server, pid, SIGKILL);
      exit (0);
    }
}

void
main(int argc, char *argv[])
{
  int i;
  io_t node;
  char *arg;
  char *path;
  error_t err = 0;
  char *args = 0;		/* The login parameters */
  unsigned args_len = 0;
  char *passwd = 0;		/* Login parameters from /etc/passwd */
  unsigned passwd_len = 0;
  char *args_defs = 0;		/* Defaults for login parameters.  */
  unsigned args_defs_len = 0;
  char *env = 0;		/* The new environment.  */
  unsigned env_len = 0;
  char *env_defs = 0;		/* Defaults for the environment.  */
  unsigned env_defs_len = 0;
  char *parent_env = 0;		/* The environment we got from our parent */
  unsigned parent_env_len = 0;
  int no_environ = 0;		/* If false, use the env as default params. */
  int no_args = 0;		/* If false, put login params in the env. */
  int inherit_environ = 0;	/* True if we shouldn't clear our env.  */
  int no_passwd = 0;		/* Don't bother verifying what we're doing.  */
  int no_login = 0;		/* Don't prepend `-' to the shells argv[0].  */
  int paranoid = 0;		/* Admit no knowledge.  */
  int retry = 0;		/* For some failures, exec a login shell.  */
  char *retry_args = 0;		/* Args passed when retrying.  */
  unsigned retry_args_len = 0;
  char *shell = 0;		/* The shell program to run.  */
  char *sh_arg0 = 0;		/* The shell's argv[0].  */
  char *sh_args = 0;		/* The args to the shell.  */
  unsigned sh_args_len = 0;
  int shell_arg = 0;		/* If there are shell args, use the first as
				   the shell name. */
  struct idvec *eff_uids = make_idvec (); /* The UIDs of the new shell.  */
  struct idvec *eff_gids = make_idvec (); /* The EFF_GIDs.  */
  struct idvec *avail_uids = make_idvec (); /* The aux UIDs of the new shell.  */
  struct idvec *avail_gids = make_idvec (); /* The aux EFF_GIDs.  */
  struct idvec *parent_uids = make_idvec (); /* Parent uids, -SETUID. */
  struct idvec *parent_gids = make_idvec (); /* Parent gids, -SETGID. */
  mach_port_t exec;		/* The shell executable.  */
  mach_port_t cwd;		/* The child's CWD.  */
  mach_port_t root;		/* The child's root directory.  */
  mach_port_t ports[INIT_PORT_MAX]; /* Init ports for the new process.  */
  int ints[INIT_INT_MAX];	/* Init ints for it.  */
  mach_port_t fds[3];		/* File descriptors passed. */
  mach_port_t auth;		/* The new shell's authentication.  */
  mach_port_t proc_server = getproc ();
  mach_port_t parent_auth = getauth ();
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
	hurd_file_name_path_lookup (use_child_init_port, get_child_fd_port,
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
      extern void _argp_unlock_xxx (); /* Secret unknown function.  */

      error (retry ? 0 : code, err, fmt, str); /* May exit... */

      if (via)
	envz_add (&retry_args, &retry_args_len, "--via", via);
      argz_insert (&retry_args, &retry_args_len, retry_args, argv[0]);

      retry_argc = argz_count (retry_args, retry_args_len);
      retry_argv = alloca ((retry_argc + 1) * sizeof (char *));
      argz_extract (retry_args, retry_args_len, retry_argv);

      /* Reinvoke ourselves with no userids or anything; shouldn't return.  */
      _argp_unlock_xxx ();	/* Hack to get around problems with getopt. */
      main (retry_argc, retry_argv);
      exit (code);		/* But if it does... */
    }

  /* Make sure that the parent_[ug]ids are filled in.  To make them useful
     for su'ing, each is the avail ids with all effective ids but the first
     appended; this gets rid of the effect of login being suid, and is useful
     as the new process's avail id list (e.g., the real id is right).   */
  void need_parent_ids ()
    {
      if (parent_uids->num == 0 && parent_gids->num == 0)
	{
	  struct idvec *p_eff_uids = make_idvec ();
	  struct idvec *p_eff_gids = make_idvec ();
	  if (!p_eff_uids || !p_eff_gids)
	    err = ENOMEM;
	  if (! err)
	    err = idvec_merge_auth (p_eff_uids, parent_uids,
				    p_eff_gids, parent_gids,
				    parent_auth);
	  if (! err)
	    {
	      idvec_delete (p_eff_uids, 0); /* Counteract setuid. */
	      idvec_delete (p_eff_gids, 0);
	      err = idvec_merge (parent_uids, p_eff_uids);
	      if (! err)
		err = idvec_merge (parent_gids, p_eff_gids);
	    }
	  if (err)
	    error (39, err, "Can't get uids");
	}
    }

  /* Returns true if the *caller* of this login program has UID.  */
  int parent_has_uid (uid_t uid)
    {
      need_parent_ids ();
      return idvec_contains (parent_uids, uid);
    }
  /* Returns true if the *caller* of this login program has GID.  */
  int parent_has_gid (gid_t gid)
    {
      need_parent_ids ();
      return idvec_contains (parent_gids, gid);
    }
  /* Returns the number of parent uids.  */
  int count_parent_uids ()
    {
      need_parent_ids ();
      return parent_uids->num;
    }
  /* Returns the number of parent gids.  */
  int count_parent_gids ()
    {
      need_parent_ids ();
      return parent_gids->num;
    }

  /* Make sure the user should be allowed to do this.  */
  void verify_passwd (const char *name, const char *password,
		      uid_t id, int is_group)
    {
      extern char *crypt (const char salt[2], const char *string);
      char *prompt, *unencrypted, *encrypted;

      if (!password || !*password
	  || idvec_contains (is_group ? eff_gids : eff_uids, id)
	  || idvec_contains (is_group ? avail_gids : avail_uids, id)
	  || (no_passwd
	      && (parent_has_uid (0)
		  || (is_group ? parent_has_uid (id) : parent_has_gid (id)))))
	return;			/* Already got this one.  */

      if (name)
	asprintf (&prompt, "Password for %s%s:",
		  is_group ? "group " : "", name);
      else
	prompt = "Password:";

      unencrypted = getpass (prompt);
      encrypted = crypt (unencrypted, password);
      /* Paranoia may destroya.  */
      memset (unencrypted, 0, strlen (unencrypted));

      if (name)
	free (prompt);

      if (strcmp (encrypted, password) != 0)
	fail (50, 0, "Incorrect password", 0);
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

	case 'k':
	  need_parent_ids ();
	  idvec_merge (avail_uids, parent_uids);
	  idvec_merge (avail_gids, parent_gids);
	  break;

	case ARGP_KEY_ARG:
	  if (state->arg_num > 0)
	    {
	      err = argz_create (state->argv + state->next - 1,
				 &sh_args, &sh_args_len);
	      state->next = state->argc; /* Consume all args */
	      if (err)
		error (9, err, "Adding %s", arg);
	      break;
	    }

	  if (strcmp (arg, "-") == 0)
	    arg = 0;		/* Just like there weren't any args at all.  */
	  /* Fall through to deal with adding the user.  */

	case 'u':
	case 'U':
	case ARGP_KEY_NO_ARGS:
	  {
	    /* USER is whom to look up.  If it's 0, then we hit the end of
	       the sh_args without seeing a user, so we want to add defaults
	       values for `nobody'.  */
	    char *user = arg ?: envz_get (args, args_len, "NOBODY");
	    struct passwd *pw =
	      isdigit (*user) ? getpwuid (atoi (user)) : getpwnam (user);
	    /* True if this is the user arg and there were no user options. */
	    int only_user =
	      (key == ARGP_KEY_ARG
	       && eff_uids->num == 0 && avail_uids->num <= count_parent_uids ()
	       && eff_gids->num == 0 && avail_gids->num <= count_parent_gids ());

	    if (! pw)
	      if (! arg)
		/* It was nobody anyway.  Just use the defaults.  */
		break;
	      else if (paranoid)
		/* In paranoid mode, we don't admit we don't know about a
		   user, so we just ask for a password we we know the user
		   can't supply.  */
		verify_passwd (only_user ? 0 : user, "*", -1, 0);
	      else
		fail (10, 0, "%s: Unknown user", user);

	    if (arg)
	      /* If it's not nobody, make sure we're authorized.  */
	      verify_passwd (only_user ? 0 : pw->pw_name, pw->pw_passwd,
			     pw->pw_uid, 0);

	    if (key == 'U')
	      /* Add available ids instead of effective ones.  */
	      {
		idvec_add_new (avail_uids, pw->pw_uid);
		idvec_add_new (avail_gids, pw->pw_gid);
	      }
	    else
	      {
		if (key == ARGP_KEY_ARG || eff_uids->num == 0)
		  /* If it's the argument (as opposed to option) specifying a
		     user, or the first option user, then we get defaults for
		     various things from the password entry.  */
		  {
		    envz_add (&passwd, &passwd_len, "HOME", pw->pw_dir);
		    envz_add (&passwd, &passwd_len, "SHELL", pw->pw_shell);
		    envz_add (&passwd, &passwd_len, "NAME", pw->pw_gecos);
		    envz_add (&passwd, &passwd_len, "USER", pw->pw_name);
		  }
		if (arg)	/* A real user.  */
		  if (key == ARGP_KEY_ARG)
		    /* The main user arg; add both effective and available
		       ids (the available ids twice, for posix compatibility
		       -- once for the real id, and again for the saved).  */
		    {
		      /* Updates the real id in IDS to be ID.  */
		      void update_real (struct idvec *ids, uid_t id)
			{
			  if (ids->num == 0
			      || !idvec_tail_contains (ids, 1, ids->ids[0]))
			    idvec_insert (ids, 0, id);
			  else
			    ids->ids[0] = id;
			}
			
		      /* Effective */
		      idvec_insert_only (eff_uids, 0, pw->pw_uid);
		      idvec_insert_only (eff_gids, 0, pw->pw_gid);
		      /* Real */
		      update_real (avail_uids, pw->pw_uid);
		      update_real (avail_gids, pw->pw_gid);
		      /* Saved */
		      idvec_insert_only (avail_uids, 1, pw->pw_uid);
		      idvec_insert_only (avail_gids, 1, pw->pw_gid);
		    }
		  else
		    {
		      idvec_add_new (eff_uids, pw->pw_uid);
		      idvec_add_new (eff_gids, pw->pw_gid);
		    }
	      }
	  }
	  break;

	case 'g':
	case 'G':
	  {
	    struct group *gr =
	      isdigit (*arg) ? getgrgid (atoi (arg)) : getgrnam (arg);
	    if (! gr)
	      fail (11, 0, "%s: Unknown group", arg);
	    verify_passwd (gr->gr_name, gr->gr_passwd, gr->gr_gid, 1);
	    idvec_add_new (key == 'g' ? eff_gids : avail_gids, gr->gr_gid);
	  }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

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

  /* Parse our options.  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  /* Now that we've parsed the command line, put together all these
     environments we've gotten from various places.  There are two targets:
     (1) the login parameters, and (2) the child environment.

     The login parameters come from these sources (in priority order):
      a) User specified (with the --arg option)
      b) From the passwd file entry for the user being logged in as
      c) From the parent environment, if --no-environ wasn't specified
      d) From the user-specified defaults (--arg-default)
      e) From last-ditch defaults given by the DEFAULT_* defines above

     The child environment is from:
      a) User specified (--environ)
      b) From the login parameters (if --no-args wasn't specified)
      c) From the parent environment, if --inherit-environ was specified
      d) From the user-specified default env values (--environ-default)
      e) From last-ditch defaults given by the DEFAULT_* defines above
   */

  /* Merge the login parameters.  */
  err = envz_merge (&args, &args_len, passwd, passwd_len, 0);
  if (! err && ! no_environ)
    err = envz_merge (&args, &args_len, parent_env, parent_env_len, 0);
  if (! err)
    err = envz_merge (&args, &args_len, args_defs, args_defs_len, 0);
  if (err)
    error (24, err, "merging parameters");

  err =
    auth_makeauth (getauth (), 0, MACH_MSG_TYPE_COPY_SEND, 0,
		   eff_uids->ids, eff_uids->num,
		   avail_uids->ids, avail_uids->num,
		   eff_gids->ids, eff_gids->num,
		   avail_gids->ids, avail_gids->num,
		   &auth);
  if (err)
    fail (3, err, "Authentication failure", 0);

  if (!no_login && count_parent_uids () != 0)
    /* Make a new login collection (but only for real users).  */
    {
      char *user = envz_get (args, args_len, "USER");
      if (user && *user)
	setlogin (user);
      proc_make_login_coll (proc_server);
    }

  if (eff_uids->num + avail_uids->num == 0 && count_parent_uids () != 0)
    /* We're transiting from having some uids to having none, which means
       this is probably a new login session.  Unless specified otherwise, set
       a timer to kill this session if it hasn't aquired any ids by then.
       Note that we fork off the timer process before clearing the process
       owner: because we're interested in killing unowned processes, proc's
       in-same-login-session rule should apply to us (allowing us to kill
       them), and this way they can't kill the watchdog (because it *does*
       have an owner).  */
    {
      char *to = envz_get (args, args_len, "NOAUTH_TIMEOUT");
      time_t timeout = to ? atoi (to) : 0;
      if (timeout)
	dog (timeout, pid);
    }

  if (eff_uids->num > 0)
    proc_setowner (proc_server, eff_uids->ids[0], 0);
  else
    proc_setowner (proc_server, 0, 1); /* Clear the owner.  */

  /* Now start constructing the exec arguments.  */
  bzero (ints, sizeof (*ints) * INIT_INT_MAX);
  arg = envz_get (args, args_len, "UMASK");
  ints[INIT_UMASK] = arg && *arg ? strtoul (arg, 0, 8) : umask (0);

  for (i = 0; i < 3; i++)
    fds[i] = getdport (i);

  for (i = 0; i < INIT_PORT_MAX; i++)
    ports[i] = MACH_PORT_NULL;
  ports[INIT_PORT_PROC] = getproc ();
  ports[INIT_PORT_CRDIR] = getcrdir ();	/* May be replaced below. */
  ports[INIT_PORT_CWDIR] = getcwdir ();	/*  "  */

  /* Now reauthenticate all of the ports we're passing to the child.  */
  err = exec_reauth (auth, 0, 1, ports, INIT_PORT_MAX, fds, 3);
  if (err)
    error (40, err, "Port reauth failure");

  /* These are the default values for the child's root/cwd.  We don't want to
     modify PORTS just yet, because we use it to do child-authenticated
     lookups.  */
  root = ports[INIT_PORT_CRDIR];
  cwd = ports[INIT_PORT_CWDIR];

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
      cwd = child_lookup (arg, 0, O_RDONLY);
      if (cwd == MACH_PORT_NULL)
	{
	  error (0, errno, "%s", arg);
	  error (0, 0, "Using HOME=/");
	  envz_add (&args, &args_len, "HOME", "/");
	}
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
      else
	{
	  sh_arg0 = malloc (strlen (shell_base) + 2);
	  if (! sh_arg0)
	    err = ENOMEM;
	  else
	    /* Prepend the name with a `-', as is the odd custom.  */
	    {
	      sh_arg0[0] = '-';
	      strcpy (sh_arg0 + 1, shell_base);
	    }
	}
    }
  if (! err)
    err = argz_insert (&sh_args, &sh_args_len, sh_args, sh_arg0);
  if (err)
    error (21, err, "Error building shell args");

  /* Maybe output the message of the day.  Note that we we the child's
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
     ROOT and CWD in.  */
  ports[INIT_PORT_CRDIR] = root;
  ports[INIT_PORT_CWDIR] = cwd;

  /* Get rid of any accumulated null entries in env.  */
  envz_strip (&env, &env_len);

  /* No more authentications to fail, so cross our fingers and add our utmp
     entry.  */
  
  err = proc_getsid (proc_server, pid, &sid);
  if (!err && pid == sid)
    /* Only add utmp entries for the session leader.  */
    add_utmp_entry (args, args_len, 0, !parent_has_uid (0));

  if ((eff_uids->num | eff_gids->num) && !no_login)
    {
      char *tty = ttyname (0);
      if (tty)
	{
	  /* Change the terminal to be owned by the user.  */
	  err = chown (tty,
		       eff_uids->num ? eff_uids->ids[0] : -1,
		       eff_gids->num ? eff_gids->ids[0] : -1);
	  if (err)
	    error (0, err, "chown: %s", tty);
	}
    }

  err = file_exec (exec, mach_task_self (),
		   EXEC_NEWTASK | EXEC_DEFAULTS | EXEC_SECURE,
		   sh_args, sh_args_len, env, env_len,
		   fds, MACH_MSG_TYPE_COPY_SEND, 3,
		   ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
		   ints, INIT_INT_MAX,
		   0, 0, 0, 0);
  if (err)
    error(5, err, "%s", shell);

  exit(0);
}
