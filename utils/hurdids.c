/* Show all hurd ids

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.

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
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <version.h>

#include <error.h>
#include <idvec.h>

const char *argp_program_version = STANDARD_HURD_VERSION (hurdids);

static struct argp_option options[] =
{
  {"effective", 'e', 0, 0, "Show effective ids"},
  {"available", 'a', 0, 0, "Show available ids"},
  {"uids",      'u', 0, 0, "Show user ids"},
  {"gids",      'g', 0, 0, "Show group ids"},
  {"names",     'n', 0, 0, "Show names of uids/gids"},
  {"ids",       'i', 0, 0, "Show numeric uids/gids"},
  {0}
};
static char *args_doc = "[PID]";
static char *doc = "Show hurd uids/gids."
"\vIf PID is suppplied, show ids in that process.";

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  error_t err;
  task_t task;
  mach_port_t msgport;
  int pid = -1;
  auth_t auth = getauth ();
  process_t proc = getproc ();
  int show_eff = 0, show_avail = 0, show_uids = 0, show_gids = 0;
  int show_names = 0, show_ids = 0;
  struct idvec euids = { 0 }, egids = { 0 };
  struct idvec auids = { 0 }, agids = { 0 };

  /* Print a given id vector, using NAME for the prompt.  */
  void print_ids (struct idvec *uids, struct idvec *gids, char *name)
    {
      int i;

      if (show_uids)
	{
	  if (name && show_gids)
	    printf ("%s uids: ", name);
	  else if (show_gids)
	    printf ("uids: ");
	  else if (name)
	    printf ("%s: ", name);

	  for (i = 0; i < uids->num; i++)
	    {
	      uid_t uid = uids->ids[i];
	      struct passwd *pw = show_names ? getpwuid (uid) : 0;
	      if (i > 0)
		putchar (' ');
	      if (pw)
		if (show_ids)
		  printf ("%d(%s)", uid, pw->pw_name);
		else
		  printf ("%s", pw->pw_name);
	      else
		printf ("%d", uid);
	    }
	  putchar ('\n');
	}
      if (show_gids)
	{
	  if (name && show_uids)
	    printf ("%s gids: ", name);
	  else if (show_uids)
	    printf ("gids: ");
	  else if (name)
	    printf ("%s: ", name);

	  for (i = 0; i < gids->num; i++)
	    {
	      gid_t gid = gids->ids[i];
	      struct group *gr = show_names ? getgrgid (gid) : 0;
	      if (i > 0)
		putchar (' ');
	      if (gr)
		if (show_ids)
		  printf ("%d(%s)", gid, gr->gr_name);
		else
		  printf ("%s", gr->gr_name);
	      else
		printf ("%d", gid);
	    }
	  putchar ('\n');
	}
    }

  /* Parse a command line option.  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'e': show_eff = 1; break;
	case 'a': show_avail = 1; break;
	case 'u': show_uids = 1; break;
	case 'g': show_gids = 1; break;
	case 'n': show_names = 1; break;
	case 'i': show_ids = 1; break;
	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    {
	      pid = atoi (arg);
	      break;
	    }
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }

  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (! show_eff && ! show_avail)
    show_eff = show_avail = 1;
  if (! show_uids && ! show_gids)
    show_uids = show_gids = 1;
  if (! show_names && ! show_ids)
    show_names = show_ids = 1;

  if (pid < 0)
    /* We get our parent's authentication instead of our own because this
       program is usually installed setuid.  This should work even if it's
       not installed setuid, using the auth port as authentication to the
       msg_get_init_port rpc.  */
    pid = getppid ();

  /* Get a msgport for PID, to which we can send requests.  */
  err = proc_getmsgport (proc, pid, &msgport);
  if (err)
    error (5, err, "%d: Cannot get process msgport", pid);

  /* Try to get the task port to use as authentication.  */
  err = proc_pid2task (proc, pid, &task);

  /* Now fetch the auth port; if we couldn't get the task port to use for
     authentication, we try the (old) auth port instead.  */
  if (err)
    err = msg_get_init_port (msgport, auth, INIT_PORT_AUTH, &auth);
  else
    err = msg_get_init_port (msgport, task, INIT_PORT_AUTH, &auth);
  if (err)
    error (6, err, "%d: Cannot get process authentication", pid);

  mach_port_deallocate (mach_task_self (), msgport);
  mach_port_deallocate (mach_task_self (), task);

  /* Get the ids that AUTH represents.  */
  err = idvec_merge_auth (&euids, &auids, &egids, &agids, auth);
  if (err)
    error (10, err, "Cannot get authentication ids");

  /* Print them.  */
  if (show_eff)
    print_ids (&euids, &egids, show_avail ? "effective" : 0);
  if (show_avail)
    print_ids (&auids, &agids, show_eff ? "available" : 0);

  exit (0);
}
