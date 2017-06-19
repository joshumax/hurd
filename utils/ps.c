/* Show process information.

   Copyright (C) 1995,96,97,98,99,2002,2006 Free Software Foundation, Inc.

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
#include <assert-backtrace.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <argp.h>
#include <argz.h>
#include <idvec.h>
#include <ps.h>
#include <error.h>
#include <version.h>

#include "psout.h"
#include "parse.h"
#include "pids.h"

const char *argp_program_version = STANDARD_HURD_VERSION (ps);

#define OA OPTION_ARG_OPTIONAL

static const struct argp_option options[] =
{
  {0,0,0,0, "Output format selection:", 1},
  {"format",     'F',     "FMT",  0,  "Use the output-format FMT; FMT may be"
                                      " `default', `user', `vmem', `long',"
				      " `jobc', `full', `hurd', `hurd-long',"
				      " or a custom format-string"},
  {"posix-format",'o',    "FMT",  0,  "Use the posix-style output-format FMT"},
  {0,            'f',     0,      0,  "Use the `full' output-format"},
  {0,            'j',     0,      0,  "Use the `jobc' output-format"},
  {0,            'l',     0,      0,  "Use the `long' output-format"},
  {0,            'u',     0,      0,  "Use the `user' output-format"},
  {0,            'v',     0,      0,  "Use the `vmem' output-format"},

  {0,0,0,0, "Process filtering (by default, other users'"
   " processes, threads, and process-group leaders are not shown):", 2},
  {"all-users",  'a',     0,      0,  "List other users' processes"},
  {0,            'd',     0,      0,  "List all processes except process group"
                                      " leaders"},
  {"all",        'e',     0,      0,  "List all processes"},
  {0,		 'A',     0,      OPTION_ALIAS}, /* Posix option meaning -e */
  {0,            'g',     0,      0,  "Include session and login leaders"},
  {"owner",      'U',     "USER", 0,  "Show only processes owned by USER"},
  {"not-owner",  'O',     "USER", 0,  "Show only processes not owned by USER"},
  {"no-parent",  'P',     0,      0,  "Include processes without parents"},
  {"threads",    'T',     0,      0,  "Show the threads for each process"},
  {"tty",        't',     "TTY",  OA, "Only show processes with controlling"
                                      " terminal TTY"},
  {0,            'x',     0,      0,  "Include orphaned processes"},

  {0,0,0,0, "Elision of output fields:", 4},
  {"no-msg-port",'M',     0,      0,  "Don't show info that uses a process's"
                                      " msg port"},
  {"nominal-fields",'n',  0,      0,  "Don't elide fields containing"
                                      " `uninteresting' data"},
  {"all-fields", 'Q',     0,      0,  "Don't elide unusable fields (normally"
                                      " if there's some reason ps can't print"
                                      " a field for any process, it's removed"
                                      " from the output entirely)"},

  {0,0,0,0, "Output attributes:"},
  {"no-header",  'H',     0,      0,  "Don't print a descriptive header line"},
  {"reverse",    'r',     0,      0,  "Reverse the order of any sort"},
  {"sort",       's',	  "FIELD",0, "Sort the output with respect to FIELD,"
                                     " backwards if FIELD is prefixed by `-'"},
  {"top",	 'h',     "ENTRIES", OA, "Show the top ENTRIES processes"
                                      " (default 10), or if ENTRIES is"
                                      " negative, the bottom -ENTRIES"},
  {"head",	 0,       0,      OPTION_ALIAS},
  {"bottom",     'b',     "ENTRIES", OA, "Show the bottom ENTRIES processes"
                                      " (default 10)"},
  {"tail",	 0,       0,      OPTION_ALIAS},
  {"width",      'w',     "WIDTH",OA, "If WIDTH is given, try to format the"
                                      " output for WIDTH columns, otherwise,"
				      " remove the default limit"},
  {0, 0}
};

static const char doc[] =
"Show information about processes PID... (default all `interesting' processes)"
"\vThe USER, LID, PID, PGRP, and SID arguments may also be comma separated"
" lists.  The System V options -u and -g may be accessed with -O and -G.";

#define FILTER_OWNER		0x01
#define FILTER_NOT_LEADER	0x02
#define FILTER_CTTY    		0x04
#define FILTER_UNORPHANED	0x08
#define FILTER_PARENTED		0x10

/* A particular predefined output format.  */
struct output_fmt
{
  const char *name;
  const char *sort_key;		/* How this format should be sorted.  */
  const char *fmt;		/* The format string.  */
};

/* The predefined output formats.  */
struct output_fmt output_fmts[] =
{
  { "default", "pid",
    "%^%?user %pid %th %tt %sc %stat %time %command" },
  { "user", "-cpu",
    "%^%user %pid %th %cpu %mem %sz %rss %tt %sc %stat %start %time %command" },
  { "vmem", "-mem",
    "%^%pid %th %stat %sl %pgins %pgflts %cowflts %zfills %sz %rss %cpu %mem %command"
  },
  { "long", "pid",
    "%^%uid %pid %th %ppid %pri %ni %nth %msgi %msgo %sz %rss %sc %wait %stat %tt %time %command" },
  { "jobc", "pid",
    "%^%user %pid %th %ppid %pgrp %sess %lcoll %sc %stat %tt %time %command" },
  { "full", "pid",
    "%^%-user %pid %ppid %tty %time %command" },
  { "hurd", "pid",
    "%pid %th %uid %nth %{vsize:Vmem} %rss %{utime:User} %{stime:System} %args"
  },
  { "hurd-long", "pid",
    "%pid %th %uid %ppid %pgrp %sess %nth %{vsize:Vmem} %rss %cpu %{utime:User} %{stime:System} %args"
  }
};

/* Augment the standard specs with our own abbrevs.  */
static const struct ps_fmt_spec
spec_abbrevs[] = {
  {"TT=tty"}, {"SC=susp"}, {"Stat=state"}, {"Command=args"}, {"SL=sleep"},
  {"NTH=nth", "TH"}, {"NI=bpri"}, {"SZ=vsize"}, {"RSS=rsize"},
  {"MsgI=msgin"}, {"MsgO=msgout"},
  {0}
};
static struct ps_fmt_specs ps_specs =
  { spec_abbrevs, &ps_std_fmt_specs };

/* Returns the UID for the user called NAME.  */
static int
lookup_user (const char *name, struct argp_state *state)
{
  struct passwd *pw = getpwnam(name);
  if (pw == NULL)
    argp_failure (state, 2, 0, "%s: Unknown user", name);
  return pw->pw_uid;
}

int
main(int argc, char *argv[])
{
  error_t err;
  /* A buffer used for rewriting old-style ps command line arguments that
     need a dash prepended for the parser to understand them.  It gets
     realloced for each successive arg that needs it, on the assumption that
     args don't get parsed multiple times.  */
  char *arg_hack_buf = 0;
  struct idvec *only_uids = make_idvec (), *not_uids = make_idvec ();
  char *tty_names = 0;
  size_t num_tty_names = 0;
  struct proc_stat_list *procset;
  struct ps_context *context;
  const char *fmt_string = "default", *sort_key_name = NULL;
  unsigned filter_mask =
    FILTER_OWNER | FILTER_NOT_LEADER | FILTER_UNORPHANED | FILTER_PARENTED;
  int sort_reverse = FALSE, print_heading = TRUE;
  int squash_bogus_fields = TRUE, squash_nominal_fields = TRUE;
  int show_threads = FALSE, no_msg_port = FALSE;
  int output_width = -1;	/* Desired max output size.  */
  int show_non_hurd_procs = 1;	/* Show non-hurd processes.  */
  int posix_fmt = 0;		/* Use a posix_fmt-style format string.  */
  int top = 0;			/* Number of entries to output.  */
  pid_t *pids = 0;		/* User-specified pids.  */
  size_t num_pids = 0;
  struct pids_argp_params pids_argp_params = { &pids, &num_pids, 1 };

  /* Add a user who's processes should be printed out.  */
  error_t add_uid (uid_t uid, struct argp_state *state)
    {
      error_t err = idvec_add (only_uids, uid);
      if (err)
	argp_failure (state, 23, err, "Can't add uid");
      return err;
    }
  /* Add a user who's processes should not be printed out.  */
  error_t add_not_uid (uid_t uid, struct argp_state *state)
    {
      error_t err = idvec_add (not_uids, uid);
      if (err)
	argp_failure (state, 23, err, "Can't add uid");
      return err;
    }
  /* Returns TRUE if PS is owned by any of the users in ONLY_UIDS, and none
     in NOT_UIDS.  */
  int proc_stat_owner_ok(struct proc_stat *ps)
    {
      int uid = proc_stat_owner_uid (ps);
      if (only_uids->num > 0 && !idvec_contains (only_uids, uid))
	return 0;
      if (not_uids->num > 0 && idvec_contains (not_uids, uid))
	return 0;
      return 1;
    }

  /* Add TTY_NAME to the list for which processes with those controlling
     terminals will be printed.  */
  error_t add_tty_name (const char *tty_name, struct argp_state *state)
    {
      error_t err = argz_add (&tty_names, &num_tty_names, tty_name);
      if (err)
	argp_failure (state, 8, err, "%s: Can't add tty", tty_name);
      return err;
    }
  int proc_stat_has_ctty(struct proc_stat *ps)
    {
      if (proc_stat_has(ps, PSTAT_TTY))
	/* Only match processes whose tty we can figure out.  */
	{
	  struct ps_tty *tty = proc_stat_tty (ps);
	  if (tty)
	    {
	      char *try = 0;
	      const char *name = ps_tty_name (tty);
	      const char *short_name = ps_tty_short_name(tty);

	      while ((try = argz_next (tty_names, num_tty_names, try)))
		if ((name && strcmp (try, name) == 0)
		    || (short_name && strcmp (try, short_name) == 0))
		  return TRUE;
	    }
	}
      return FALSE;
    }

  /* Returns the name of the current controlling terminal.  */
  const char *current_tty_name()
    {
      error_t err;
      struct ps_tty *tty;
      mach_port_t cttyid = getcttyid();

      if (cttyid == MACH_PORT_NULL)
	error(2, 0, "No controlling terminal");

      err = ps_context_find_tty_by_cttyid (context, cttyid, &tty);
      if (err)
	error(2, err, "Can't get controlling terminal");

      return ps_tty_name (tty);
    }

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:			/* Non-option argument.  */
	  if (!isdigit (*arg) && !state->quoted)
	    /* Old-fashioned `ps' syntax takes options without the leading
	       dash.  Prepend a dash and feed back to getopt.  */
	    {
	      size_t len = strlen (arg) + 1;
	      arg_hack_buf = realloc (arg_hack_buf, 1 + len);
	      state->argv[--state->next] = arg_hack_buf;
	      state->argv[state->next][0] = '-';
	      memcpy (&state->argv[state->next][1], arg, len);
	      break;
	    }
	  else
	    /* Let PIDS_ARGP handle it.  */
	    return ARGP_ERR_UNKNOWN;

	case 'a': filter_mask &= ~FILTER_OWNER; break;
	case 'd': filter_mask &= ~(FILTER_OWNER | FILTER_UNORPHANED); break;
	case 'e': case 'A': filter_mask = 0; break;
	case 'g': filter_mask &= ~FILTER_NOT_LEADER; break;
	case 'x': filter_mask &= ~FILTER_UNORPHANED; break;
	case 'P': filter_mask &= ~FILTER_PARENTED; break;
	case 'f': fmt_string = "full"; break;
	case 'u': fmt_string = "user"; break;
	case 'v': fmt_string = "vmem"; break;
	case 'j': fmt_string = "jobc"; break;
	case 'l': fmt_string = "long"; break;
	case 'M': no_msg_port = TRUE; break;
	case 'H': print_heading = FALSE; break;
	case 'Q': squash_bogus_fields = squash_nominal_fields = FALSE; break;
	case 'n': squash_nominal_fields = FALSE; break;
	case 'T': show_threads = TRUE; break;
	case 's': sort_key_name = arg; break;
	case 'r': sort_reverse = TRUE; break;
	case 'h': top = arg ? atoi (arg) : 10; break;
	case 'b': top = -(arg ? atoi (arg) : 10); break;
	case 'F': fmt_string = arg; posix_fmt = 0; break;
	case 'o': fmt_string = arg; posix_fmt = 1; break;

	case 'w':
	  output_width = arg ? atoi (arg) : 0; /* 0 means `unlimited'.  */
	  break;

	case 't':
	  return parse_strlist (arg, add_tty_name, current_tty_name, "tty", state);
	case 'U':
	  return parse_numlist (arg, add_uid, NULL, lookup_user, "user", state);
	case 'O':
	  return parse_numlist (arg, add_not_uid, NULL, lookup_user, "user", state);

	case ARGP_KEY_INIT:
	  /* Initialize inputs for child parsers.  */
	  state->child_inputs[0] = &pids_argp_params;
	  break;

	case ARGP_KEY_SUCCESS:
	  /* Select an explicit format string if FMT_STRING is a format
	     name.  This is done here because parse_enum needs STATE.  */
	  {
	    if (posix_fmt)
	      break;
	    const char *fmt_name (unsigned n)
	      {
		return
		  n >= (sizeof output_fmts / sizeof *output_fmts)
		    ? 0
		    : output_fmts[n].name;
	      }
	    int fmt_index = parse_enum (fmt_string, fmt_name,
					"format type", 1, state);
	    if (fmt_index >= 0)
	      {
		fmt_string = output_fmts[fmt_index].fmt;
		if (sort_key_name == NULL)
		  sort_key_name = output_fmts[fmt_index].sort_key;
	      }
	  }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }

  struct argp_child argp_kids[] =
    { { &pids_argp, 0,
	"Process selection (before filtering; default is all processes):", 3},
      {0} };
  struct argp argp = { options, parse_opt, 0, doc, argp_kids };

  err = ps_context_create (getproc (), &context);
  if (err)
    error(1, err, "ps_context_create");

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  err = proc_stat_list_create(context, &procset);
  if (err)
    error(1, err, "proc_stat_list_create");

  if (num_pids == 0)
    /* No explicit processes specified.  */
    {
      err = proc_stat_list_add_all (procset, 0, 0);

      /* Try to avoid showing non-hurd processes if this isn't a native-booted
	 hurd system (because there would be lots of them).  Here we use a
	 simple heuristic: Is the 5th process a hurd process (1-4 are
	 typically: proc server, init, kernel, boot default pager (maybe); the
	 last two are know not to be hurd processes)?  */
      if (procset->num_procs > 4)
	{
	  struct proc_stat *ps = procset->proc_stats[4];
	  if (proc_stat_set_flags (ps, PSTAT_STATE) == 0
	      && (ps->flags & PSTAT_STATE))
	    show_non_hurd_procs = !(ps->state & PSTAT_STATE_P_NOPARENT);
	  else
	    /* Something is fucked, we can't get the state bits for PS.
	       Default to showing everything.  */
	    show_non_hurd_procs = 1;
	}
    }
  else
    /* User-specified processes. */
    {
      err = proc_stat_list_add_pids (procset, pids, num_pids, 0);
      filter_mask = 0;		/* Don't mess with them.  */
    }

  if (err)
    error(2, err, "Can't get process list");

  if (no_msg_port)
    proc_stat_list_set_flags(procset, PSTAT_NO_MSGPORT);

  if (only_uids->num == 0 && (filter_mask & FILTER_OWNER))
    /* Restrict the output to only our own processes.  */
    {
      int uid = getuid ();
      if (uid >= 0)
	add_uid (uid, 0);
      else
	filter_mask &= ~FILTER_OWNER; /* Must be an anonymous process.  */
    }

  /* Filter out any processes that we don't want to show.  */
  if (only_uids->num || not_uids->num)
    proc_stat_list_filter1 (procset, proc_stat_owner_ok,
			    PSTAT_OWNER_UID, FALSE);
  if (num_tty_names > 0)
    {
      /* We set the PSTAT_TTY flag separately so that our filter function
	 can look at any procs that fail to set it.  */
      proc_stat_list_set_flags(procset, PSTAT_TTY);
      proc_stat_list_filter1(procset, proc_stat_has_ctty, 0, FALSE);
    }
  if (filter_mask & FILTER_NOT_LEADER)
    proc_stat_list_filter (procset, &ps_not_leader_filter, FALSE);
  if (filter_mask & FILTER_UNORPHANED)
    proc_stat_list_filter (procset, &ps_unorphaned_filter, FALSE);
  if (!show_non_hurd_procs && (filter_mask & FILTER_PARENTED))
    proc_stat_list_filter (procset, &ps_parent_filter, FALSE);

  if (show_threads)
    proc_stat_list_add_threads(procset);

  proc_stat_list_filter (procset, &ps_alive_filter, FALSE);

  psout (procset, fmt_string, posix_fmt, &ps_specs,
	 sort_key_name, sort_reverse,
	 output_width, print_heading,
	 squash_bogus_fields, squash_nominal_fields, top);

  return 0;
}
