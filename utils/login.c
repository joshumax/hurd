/* Hurdish login

   Copyright (C) 1995 Free Software Foundation, Inc.

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
#include <argp.h>
#include <argz.h>
#include <unistd.h>
#include <paths.h>
#include <error.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <sys/fcntl.h>

#define DEFAULT_SHELL _PATH_BSHELL
#define DEFAULT_PATH  _PATH_DEFPATH
#define DEFAULT_HOME "/u/nobody"
#define DEFAULT_UMASK 0

static struct argp_option options[] =
{
  {"shell", 's', "SHELL", 0, "Use SHELL as the new shell"},
  {"arg",   'a', "ARG",   0, "Add ARG to the shell's arguments"},
  {"arg0",  '0', "ARG",   0, "Make ARG the shell's argv[0]"},
  {"environ", 'e', "ENTRY", 0, "Add ENTRY to the environment"},
  {"user",  'u', "USER",  0, "Add USER to the uids in the new shell"},
  {"aux-user", 'U', "USER",  0, "Add USER to the aux uids in the new shell"},
  {"group", 'g', "GROUP", 0, "Add GROUP to the gids in the new shell"},
  {"aux-group", 'G', "GROUP", 0, "Add GROUP to the aux gids in the new shell"},
  {"home",  'h', "DIR",   0, "Make DIR the CWD of the new shell"},
  {"umask", 'm', "MASK",  0, "Make UMASK the new shell's umask"},
  {"path",  'p', "PATH",  0, "Path put in environment by default"},
  {"no-environ", 'E', 0, 0, "Don't put default entries into the new environment"},
  {0, 0}
};
static char *args_doc = "[USER...]";
static char *doc = 0;

/* These functions should be moved into argz.c in libshouldbelibc.  */

/* Add BUF, of length BUF_)LEN to the argz vector in ARGZ & ARGZ_LEN.  */
static error_t
argz_append (char **argz, unsigned *argz_len, char *buf, unsigned buf_len)
{
  unsigned new_argz_len = *argz_len + buf_len;
  char *new_argz = realloc (*argz, new_argz_len);
  if (new_argz)
    {
      bcopy (buf, new_argz + *argz_len, buf_len);
      *argz = new_argz;
      *argz_len = new_argz_len;
      return 0;
    }
  else
    return ENOMEM;
}

/* Add STR to the argz vector in ARGZ & ARGZ_LEN.  This should be moved into
   argz.c in libshouldbelibc.  */
static error_t
argz_add (char **argz, unsigned *argz_len, char *str)
{
  return argz_append (argz, argz_len, str, strlen (str) + 1);
}

typedef unsigned id_t;

struct ids 
{
  id_t *ids;
  unsigned num, alloced;
};

struct
ids *make_ids ()
{
  struct ids *ids = malloc (sizeof (struct ids));
  if (!ids)
    error(8, ENOMEM, "Can't allocate id list");
  ids->ids = 0;
  ids->num = ids->alloced = 0;
  return ids;
}

void
ids_add (struct ids *ids, id_t id)
{
  if (ids->alloced == ids->num)
    {
      ids->alloced = ids->alloced * 2 + 1;
      ids->ids = realloc (ids->ids, ids->alloced * sizeof (id_t));
      if (ids->ids == NULL)
	error(8, ENOMEM, "Can't allocate id list");
    }

  ids->ids[ids->num++] = id;
}

void 
main(int argc, char *argv[])
{
  error_t err;
  int umask = DEFAULT_UMASK;
  char *shell = 0;		/* The shell program to run.  */
  char *home = 0;		/* The new home directory.  */
  char *path = DEFAULT_PATH;	/* The path put in environment.  */
  char *arg0 = 0;		/* The shell's argv[0].  */
  char *args = 0;		/* The args to the shell.  */
  unsigned args_len = 0;
  char *env = 0;		/* The new environment.  */
  unsigned env_len = 0;
  int no_environ = 0;		/* True if we shouldn't add default entries. */
  struct ids *uids = make_ids (); /* The UIDs of the new shell.  */
  struct ids *gids = make_ids (); /* The GIDs.  */
  struct ids *aux_uids = make_ids (); /* The aux UIDs of the new shell.  */
  struct ids *aux_gids = make_ids (); /* The aux GIDs.  */
  mach_port_t exec_node;	/* The shell executable.  */
  mach_port_t home_node;	/* The home directory node.  */
  mach_port_t ports[INIT_PORT_MAX];
  int ints[INIT_INT_MAX];
  mach_port_t dtable[3];	/* File descriptors passed. */
  char *argz = 0;		/* The shell's arg vector.  */
  unsigned argz_len = 0;
  mach_port_t auth;		/* The auth port for the new shell.  */
  mach_port_t auth_server = getauth ();
  mach_port_t proc_server = getproc ();

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 's':  shell = arg; break;
	case 'm':  umask = strtoul (arg, 0, 8); break;
	case 'h':  home = arg; break;
	case 'p':  path = arg; break;

	case 'e':
	  err = argz_add (&env, &env_len, arg);
	  if (err)
	    error (8, err, "Adding %s", arg);
	  break;
	case 'a':
	  err = argz_add (&args, &args_len, arg);
	  if (err)
	    error (9, err, "Adding %s", arg);
	  break;
	case '0':
	  arg0 = arg;
	  break;

	case ARGP_KEY_ARG:
	case 'u':
	case 'U':
	  {
	    struct passwd *pw =
	      isdigit (*arg) ? getpwuid (atoi (arg)) : getpwnam (arg);
	    if (! pw)
	      error (10, 0, "%s: Unknown user", arg);

	    /* Should check password here. */

	    if (key != 'U')
	      {
		ids_add (uids, pw->pw_uid);
		ids_add (gids, pw->pw_gid);

		/* Add reasonable defaults.  */
		if (! home && pw->pw_dir)
		  home = strdup (pw->pw_dir);
		if (! shell && pw->pw_shell)
		  shell = strdup (pw->pw_shell);
	      }
	    else
	      {
		ids_add (aux_uids, pw->pw_uid);
		ids_add (aux_uids, pw->pw_gid);
	      }
	  }
	  break;

	case 'g':
	case 'G':
	  {
	    struct group *gr =
	      isdigit (*arg) ? getgrgid (atoi (arg)) : getgrnam (arg);
	    if (! gr)
	      error (11, 0, "%s: Unknown group", arg);

	    /* Should check password here. */

	    ids_add (key == 'g' ? gids : aux_gids, gr->gr_gid);
	  }
	  break;

	default: return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0);

  if (! shell)
    shell = DEFAULT_SHELL;
  if (! arg0)
    arg0 = shell;
  if (! home)
    home = DEFAULT_HOME;

  argz_add (&argz, &argz_len, arg0);
  argz_append (&argz, &argz_len, args, args_len);

  if (! no_environ)
    {
      char *entry;
      asprintf (&entry, "HOME=%s", home);
      argz_add (&env, &env_len, entry);
      free (entry);
      asprintf (&entry, "SHELL=%s", shell);
      argz_add (&env, &env_len, entry);
      free (entry);
      if (path)
	{
	  asprintf (&entry, "PATH=%s", path);
	  argz_add (&env, &env_len, entry);
	  free (entry);
	}
    }

  exec_node = file_name_lookup (shell, O_EXEC, 0);
  if (exec_node == MACH_PORT_NULL)
    error (1, errno, "%s", shell);

  home_node = file_name_lookup (home, O_RDONLY, 0);
  if (home_node == MACH_PORT_NULL)
    error (2, errno, "%s", home);

  err =
    auth_makeauth (auth_server, 0, MACH_MSG_TYPE_COPY_SEND, 0,
		   uids->ids, uids->num, gids->ids, gids->num,
		   aux_uids->ids, aux_uids->num, aux_gids->ids, aux_gids->num,
		   &auth);
  if (err)
    error (3, err, "authenticating");

  bzero (ports, sizeof (*ports) * INIT_PORT_MAX);
  ports[INIT_PORT_CRDIR] = getcrdir ();
  ports[INIT_PORT_CWDIR] = home_node;
  ports[INIT_PORT_AUTH] = auth;
  ports[INIT_PORT_PROC] = proc_server;

  bzero (ints, sizeof (*ints) * INIT_INT_MAX);
  ints[INIT_UMASK] = umask;

  dtable[0] = getdport (0);
  dtable[1] = getdport (1);
  dtable[2] = getdport (2);

  err = file_exec (exec_node, mach_task_self (),
#if 0
		   EXEC_NEWTASK | EXEC_DEFAULTS | EXEC_SECURE,
#else
		   0,
#endif
		   argz, argz_len, env, env_len,
		   dtable, MACH_MSG_TYPE_COPY_SEND, 3,
		   ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
		   ints, INIT_INT_MAX,
		   0, 0, 0, 0);
  if (err)
    error(5, err, "%s", shell);

  exit(0);
}
