/* Show process information.

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
#include <assert.h>
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

char *argp_program_version = STANDARD_HURD_VERSION (ps);

#define OA OPTION_ARG_OPTIONAL

static const struct argp_option options[] =
{
  {"all-users",  'a',     0,      0,  "List other users' processes"},
  {"format",     'F',     "FMT",  0,  "Use the output-format FMT; FMT may be"
                                      " `default', `user', `vmem', `long',"
				      " `jobc', `full', `hurd', `hurd-long',"
				      " or a custom format-string"},
  {"posix-format",'o',    "FMT",  0,  "Use the posix-style output-format FMT"},
  {0,            'd',     0,      0,  "List all processes except process group"
                                      " leaders"},
  {"all",        'e',     0,      0,  "List all processes"},
  {0,		 'A',     0,      OPTION_ALIAS}, /* Posix option meaning -e */
  {0,            'f',     0,      0,  "Use the `full' output-format"},
  {0,            'g',     0,      0,  "Include session and login leaders"},
  {"no-header",  'H',     0,      0,  "Don't print a descriptive header line"},
  {0,            'j',     0,      0,  "Use the `jobc' output-format"},
  {0,            'l',     0,      0,  "Use the `long' output-format"},
  {"login",      'L',     "LID", OA, "Add the processes from the login"
                                      " collection LID (which defaults that of"
                                      " the current process)"},
  {"lid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {"no-msg-port",'M',     0,      0,  "Don't show info that uses a process's"
                                      " msg port"},
  {"nominal-fields",'n',  0,      0,  "Don't elide fields containing"
                                      " `uninteresting' data"},
  {"owner",      'U',     "USER", 0,  "Show only processes owned by USER"},
  {"not-owner",  'O',     "USER", 0,  "Show only processes not owned by USER"},
  {"pid",        'p',     "PID",  0,  "List the process PID"},
  {"pgrp",       'G',     "PGRP", 0,  "List processes in process group PGRP"},
  {"no-parent",  'P',     0,      0,  "Include processes without parents"},
  {"all-fields", 'Q',     0,      0,  "Don't elide unusable fields (normally"
                                      " if there's some reason ps can't print"
                                      " a field for any process, it's removed"
                                      " from the output entirely)"},
  {"reverse",    'r',     0,      0,  "Reverse the order of any sort"},
  {"session",    'S',     "SID",  OA, "Add the processes from the session SID"
                                      " (which defaults to the sid of the"
                                      " current process)"},
  {"sid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {"sort",       's',	  "FIELD",0, "Sort the output with respect to FIELD,"
                                     " backwards if FIELD is prefixed by `-'"},
  {"threads",    'T',     0,      0,  "Show the threads for each process"},
  {"tty",        't',     "TTY",  OA, "Only show processes with controlling"
                                      " terminal TTY"},
  {0,            'u',     0,      0,  "Use the `user' output-format"},
  {0,            'v',     0,      0,  "Use the `vmem' output-format"},
  {"width",      'w',     "WIDTH",OA, "If WIDTH is given, try to format the"
                                      " output for WIDTH columns, otherwise,"
				      " remove the default limit"}, 
  {0,            'x',     0,      0,  "Include orphaned processes"},
  {0, 0}
};

char *args_doc = "[PID...]";
char *doc =
"Show information about processes PID... (default all `interesting' processes)"
"\vThe USER, LID, PID, PGRP, and SID arguments may also be comma separated"
" lists.  The System V options -u and -g may be accessed with -O and -G."; 

int 
parse_enum(char *arg, char **choices, char *kind, int allow_mismatches)
{
  int arglen = strlen(arg);
  char **p = choices;
  int partial_match = -1;

  while (*p != NULL)
    if (strcmp(*p, arg) == 0)
      return p - choices;
    else
      {
	if (strncmp(*p, arg, arglen) == 0)
	  if (partial_match >= 0)
	    argp_error (0, "%s: Ambiguous %s", arg, kind);
	  else
	    partial_match = p - choices;
	p++;
      }

  if (partial_match < 0 && !allow_mismatches)
    argp_error (0, "%s: Invalid %s", arg, kind);

  return partial_match;
}

#define FILTER_OWNER		0x01
#define FILTER_NOT_LEADER	0x02
#define FILTER_CTTY    		0x04
#define FILTER_UNORPHANED	0x08
#define FILTER_PARENTED		0x10

enum procsets
{
  PROCSET_ALL, PROCSET_SESSION, PROCSET_LOGIN
};
char *procset_names[] =
{"all", "session", "login", 0};

/* The names of various predefined output formats.  */
char *fmt_names[] =
  {"default",	"user",	"vmem",	"long",	"jobc",	"full",	"hurd",	"hurd-long",0};
/* How each of those formats should be sorted; */
char *fmt_sortkeys[] =
  {"pid",	"-cpu","-mem",	"pid",	"pid",	"pid",	"pid",	"pid"};
/* and the actual format strings.  */
char *fmts[] =
{
  /* default */
  "%^%?user %pid %th %tt %sc %stat %time %command",
  /* user (-u) */
  "%^%user %pid %th %cpu %mem %sz %rss %tt %sc %stat %command",
  /* vmem (-v) */
  "%^%pid %th %stat %sl %pgins %pgflts %cowflts %zfills %sz %rss %cpu %mem %command",
  /* long (-l) */
  "%^%uid %pid %th %ppid %pri %ni %nth %msgi %msgo %sz %rss %sc %wait %stat %tt %time %command",
  /* jobc (-j) */
  "%^%user %pid %th %ppid %pgrp %sess %lcoll %sc %stat %tt %time %command",
  /* full (-f) (from sysv) */
  "%^%-user %pid %ppid %tty %time %command",
  /* hurd */
  "%pid %th %uid %nth %{vsize:Vmem} %rss %{utime:User} %{stime:System} %args",
  /* hurd-long */
  "%pid %th %uid %ppid %pgrp %sess %nth %{vsize:Vmem} %rss %cpu %{utime:User} %{stime:System} %args"
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

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_ADD_FN isn't NULL, then call DEFAULT_ADD_FN instead. */
static void
_parse_strlist (char *arg,
		void (*add_fn)(const char *str), void (*default_add_fn)(),
		const char *type_name)
{
  if (arg)
    while (isspace(*arg))
      arg++;

  if (arg == NULL || *arg == '\0')
    if (default_add_fn)
      (*default_add_fn)();
    else
      error(7, 0, "Empty %s list", type_name);
  else
    {
      char *end = arg;

      void mark_end()
	{
	  *end++ = '\0';
	  while (isspace(*end))
	    end++;
	}
      void parse_element()
	{
	  if (*arg == '\0')
	    error(7, 0, "Empty element in %s list", type_name);
	  (*add_fn)(arg);
	  arg = end;
	}

      while (*end != '\0')
	switch (*end)
	  {
	  case ' ': case '\t':
	    mark_end();
	    if (*end == ',')
	      mark_end();
	    parse_element();
	    break;
	  case ',':
	    mark_end();
	    parse_element();
	    break;
	  default:
	    end++;
	  }

      parse_element();
    }
}

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_FN isn't NULL, then call ADD_FN on the resutl of calling
   DEFAULT_FN instead, otherwise signal an error.  */
static void
parse_strlist (char *arg,
	       void (*add_fn)(const char *str),
	       const char *(*default_fn)(),
	       const char *type_name)
{
  void default_str_add() { (*add_fn)((*default_fn)()); }
  _parse_strlist(arg, add_fn, default_str_add, type_name);
}

/* For each numeric string in the comma-separated list in ARG, call ADD_FN;
   if ARG is empty and DEFAULT_FN isn't NULL, then call DEF_FN to get a number,
   and call ADD_FN on that, otherwise signal an error.  If any member of the
   list isn't a number, and LOOKUP_FN isn't NULL, then it is called to return
   an integer for the string.  LOOKUP_FN should signal an error itself it
   there's some problem parsing the string.  */
static void
parse_numlist (char *arg,
	       void (*add_fn)(unsigned num),
	       int (*default_fn)(),
	       int (*lookup_fn)(const char *str),
	       const char *type_name)
{
  void default_num_add() { (*add_fn)((*default_fn)()); }
  void add_num_str(const char *str)
    {
      const char *p;
      for (p = str; *p != '\0'; p++)
	if (!isdigit(*p))
	  {
	    if (lookup_fn)
	      (*add_fn)((*lookup_fn)(str));
	    else
	      error (7, 0, "%s: Invalid %s", p, type_name);
	    return;
	  }
      (*add_fn)(atoi(str));
    }
  _parse_strlist(arg, add_num_str, default_fn ? default_num_add : 0,
		 type_name);
}

static process_t proc_server;

/* Returns our session id.  */
static pid_t
current_sid()
{
  pid_t sid;
  error_t err = proc_getsid(proc_server, getpid(), &sid);
  if (err)
    error(2, err, "Couldn't get current session id");
  return sid;
}

/* Returns our login collection id.  */
static pid_t
current_lid()
{
  pid_t lid;
  error_t err = proc_getloginid(proc_server, getpid(), &lid);
  if (err)
    error(2, err, "Couldn't get current login collection") ;
  return lid;
}

/* Returns the UID for the user called NAME.  */
static int
lookup_user(const char *name)
{
  struct passwd *pw = getpwnam(name);
  if (pw == NULL)
    error(2, 0, "%s: Unknown user", name);
  return pw->pw_uid;
}

void 
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
  unsigned num_tty_names = 0;
  struct proc_stat_list *procset;
  struct ps_context *context;
  char *fmt_string = "default", *sort_key_name = NULL;
  unsigned filter_mask =
    FILTER_OWNER | FILTER_NOT_LEADER | FILTER_UNORPHANED | FILTER_PARENTED;
  int sort_reverse = FALSE, print_heading = TRUE;
  int squash_bogus_fields = TRUE, squash_nominal_fields = TRUE;
  int show_threads = FALSE, no_msg_port = FALSE;
  int output_width = -1;	/* Desired max output size.  */
  int show_non_hurd_procs = 1;	/* Show non-hurd processes.  */
  int posix_fmt = 0;		/* Use a posix_fmt-style format string.  */

  /* Add a specific process to be printed out.  */
  void add_pid (unsigned pid)
    {
      struct proc_stat *ps;

      err = proc_stat_list_add_pid (procset, pid, &ps);
      if (err)
	error (0, err, "%d: Can't add process", pid);

      /* See if this process actually exists.  */
      proc_stat_set_flags (ps, PSTAT_PROC_INFO);
      if (! proc_stat_has (ps, PSTAT_PROC_INFO))
	/* Give an error message; using ps_alive_filter below will delete the
	   entry so it doesn't get output. */
	error (0, 0, "%d: Unknown process", pid);

      /* If explicit processes are specified, we probably don't want to
	 filter them out later.  This implicit turning off of filtering might
	 be confusing in the case where a login-collection or session is
	 specified along with some pids, but it's probably not worth worrying
	 about.  */
      filter_mask = 0;
    }
  /* Print out all process from the given session.  */
  void add_sid(unsigned sid)
    {
      err = proc_stat_list_add_session (procset, sid, 0, 0);
      if (err)
	error(2, err, "%u: Can't add session", sid);
    }
  /* Print out all process from the given login collection.  */
  void add_lid(unsigned lid)
    {
      error_t err = proc_stat_list_add_login_coll (procset, lid, 0, 0);
      if (err)
	error(2, err, "%u: Can't add login collection", lid);
    }
  /* Print out all process from the given process group.  */
  void add_pgrp(unsigned pgrp)
    {
      error_t err = proc_stat_list_add_pgrp (procset, pgrp, 0, 0);
      if (err)
	error(2, err, "%u: Can't add process group", pgrp);
    }
    
  /* Add a user who's processes should be printed out.  */
  void add_uid (uid_t uid)
    {
      error_t err = idvec_add (only_uids, uid);
      if (err)
	error (23, err, "Can't add uid");
    }
  /* Add a user who's processes should not be printed out.  */
  void add_not_uid (uid_t uid)
    {
      error_t err = idvec_add (not_uids, uid);
      if (err)
	error (23, err, "Can't add uid");
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
  void add_tty_name (const char *tty_name)
    {
      error_t err = argz_add (&tty_names, &num_tty_names, tty_name);
      if (err)
	error (8, err, "%s: Can't add tty", tty_name);
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
  static const char *current_tty_name()
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
	  /* Otherwise, fall through and treat the arg as a process id.  */
	case 'p':
	  parse_numlist(arg, add_pid, NULL, NULL, "process id");
	  break;

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
	case 'F': fmt_string = arg; posix_fmt = 0; break;
	case 'o': fmt_string = arg; posix_fmt = 1; break;

	case 'w':
	  output_width = arg ? atoi (arg) : 0; /* 0 means `unlimited'.  */
	  break;

	case 't':
	  parse_strlist (arg, add_tty_name, current_tty_name, "tty");
	  break;
	case 'U':
	  parse_numlist (arg, add_uid, NULL, lookup_user, "user");
	  break;
	case 'O':
	  parse_numlist (arg, add_not_uid, NULL, lookup_user, "user");
	  break;
	case 'S':
	  parse_numlist(arg, add_sid, current_sid, NULL, "session id");
	  break;
	case 'L':
	  parse_numlist(arg, add_lid, current_lid, NULL, "login collection");
	  break;
	case 'G':
	  parse_numlist(arg, add_pgrp, NULL, NULL, "process group");
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }

  struct argp argp = { options, parse_opt, args_doc, doc};

  proc_server = getproc();

  err = ps_context_create (proc_server, &context);
  if (err)
    error(1, err, "ps_context_create");

  err = proc_stat_list_create(context, &procset);
  if (err)
    error(1, err, "proc_stat_list_create");

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (only_uids->num == 0 && (filter_mask & FILTER_OWNER))
    /* Restrict the output to only our own processes.  */
    {
      int uid = getuid ();
      if (uid >= 0)
	add_uid (uid);
      else
	filter_mask &= ~FILTER_OWNER; /* Must be an anonymous process.  */
    }

  {
    int fmt_index = parse_enum(fmt_string, fmt_names, "format type", 1);
    if (fmt_index >= 0)
      {
	fmt_string = fmts[fmt_index];
	if (sort_key_name == NULL)
	  sort_key_name = fmt_sortkeys[fmt_index];
      }
  }

  if (proc_stat_list_num_procs (procset) == 0)
    {
      err = proc_stat_list_add_all (procset, 0, 0);
      if (err)
	error(2, err, "Can't get process list");

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

  if (no_msg_port)
    proc_stat_list_set_flags(procset, PSTAT_NO_MSGPORT);

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
	 squash_bogus_fields, squash_nominal_fields);

  exit (0);
}
