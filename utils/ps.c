/* Show process information.

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
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <argp.h>
#include <hurd/ps.h>
#include <unistd.h>

#include "error.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/* Long options without corresponding short ones.  -1 is EOF.  */
#define OPT_LOGIN	-2
#define OPT_SESS	-3
#define OPT_SORT	-4
#define OPT_FMT		-5
#define OPT_HELP	-6
#define OPT_PGRP	-7

#define OA OPTION_ARG_OPTIONAL

static struct argp_option options[] =
{
  {"all-users",  'a',     0,      0,  "List other users' processes"},
  {"fmt",        OPT_FMT, "FMT",  0,  "Use the output-format FMT FMT may be"
                                      " `default', `user', `vmem', `long',"
				      " `jobc', `full', `hurd', `hurd-long',"
				      " or a custom format-string"},
  {0,            'd',     0,      0,  "List all processes except process group"
                                      " leaders"},
  {"all",        'e',     0,      0,  "List all processes"},
  {0,            'f',     0,      0,  "Use the `full' output-format"},
  {0,            'g',     0,      0,  "Include session leaders"},
  {"no-header",  'H',     0,      0,  "Don't print a descriptive header line"},
  {0,            'j',     0,      0,  "Use the `jobc' output-format"},
  {0,            'l',     0,      0,  "Use the `long' output-format"},
  {"login",      OPT_LOGIN,"LID", OA, "Add the processes from the login"
                                      " collection LID (which defaults that of"
                                      " the current process)"},
  {"lid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {"no-msg-port",'M',     0,      0,  "Don't show info that uses a process's"
                                      " msg port"},
  {"nominal-fields",'n',  0,      0,  "Don't elide fields containing"
                                      " `uninteresting' data"},
  {"owner",      'o',     "USER", 0,  "Show only processes owned by USER"},
  {"not-owner",  'O',     "USER", 0,  "Show only processes not owned by USER"},
  {"pid",        'p',     "PID",  0,  "List the process PID"},
  {"pgrp",       OPT_PGRP,"PGRP", 0,  "List processes in process group PGRP"},
  {"no-parent",  'P',     0,      0,  "Include processes without parents"},
  {"all-fields", 'Q',     0,      0,  "Don't elide unusable fields (normally"
                                      " if there's some reason ps can't print"
                                      " a field for any process, it's removed"
                                      " from the output entirely)"},
  {"reverse",    'r',     0,      0,  "Reverse the order of any sort"},
  {"session",    OPT_SESS,"SID",  OA, "Add the processes from the session SID"
                                      " (which defaults to the sid of the"
                                      " current process)"},
  {"sid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {"sort",       OPT_SORT,"FIELD", 0, "Sort the output with respect to FIELD"},
  {"threads",    's',     0,      0,  "Show the threads for each process"},
  {"tty",        't',     "TTY",  OA, "Only show processes who's controlling"
                                      " terminal is TTY"},
  {0,            'u',     0,      0,  "Use the `user' output-format"},
  {0,            'v',     0,      0,  "Use the `vmem' output-format"},
  {"width",      'w',     "WIDTH",OA, "If WIDTH is given, try to format the"
                                      " output for WIDTH columns, otherwise,"
				      " remove the default limit"}, 
  {0,            'x',     0,      0,  "Include orphaned processes"},
  {0, 0}
};

char *args_doc = "[PID...]";

char *doc = "The USER, LID, PID, PGRP, and SID arguments may also be comma \
separated lists.  The System V options -u and -g may be accessed with -o and \
--pgrp.";

int 
parse_enum(char *arg, char **choices, char *kind, bool allow_mismatches)
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
#define FILTER_NSESSLDR		0x02
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
  {"pid",	"%cpu",	"%mem",	"pid",	"pid",	"pid",	"pid",	"pid"};
/* and whether the sort should be backwards or forwards; */ 
bool fmt_sortrev[] =
  {FALSE,	TRUE,	TRUE,	FALSE,	FALSE,	FALSE,	FALSE,	FALSE};
/* and the actual format strings.  */
char *fmts[] =
{
  /* default */
  "~PID ~TH# ~TT=tty ~SC=susp ~STAT=state ~TIME ~COMMAND=args",
  /* user (-u) */
  "~USER ~PID ~TH# ~%CPU ~%MEM ~SZ=vsize ~RSS=rsize ~TT=tty ~SC=susp ~STAT=state ~COMMAND=args",
  /* vmem (-v) */
  "~PID ~TH# ~STAT=state ~SL=sleep ~PAGEIN=pgins ~FAULTS=pgflts ~COWFLT=cowflts ~ZFILLS ~SIZE=vsize ~RSS=rsize ~%CPU ~%MEM ~COMMAND=args",
  /* long (-l) */
  "~UID ~PID ~TH# ~PPID ~PRI ~NI=bpri ~TH=nth ~MSGI=msgin ~MSGO=msgout ~SZ=vsize ~RSS=rsize ~SC=susp ~RPC ~STAT=state ~TT=tty ~TIME ~COMMAND=args",
  /* jobc (-j) */
  "~USER ~PID ~TH# ~PPID ~PGRP ~SESS ~LCOLL ~SC=susp ~STAT=state ~TT=tty ~TIME ~COMMAND=args",
  /* full (-f) (from sysv) */
  "~-USER ~PID ~PPID ~TTY ~TIME ~COMMAND=args",
  /* hurd */
  "~PID ~Th# ~UID ~NTh ~VMem=vsize ~RSS=rsize ~User=utime ~System=stime ~Args",
  /* hurd-long */
  "~PID ~Th# ~UID ~PPID ~PGRP ~Sess ~NTh ~VMem=vsize ~RSS=rsize ~%CPU ~User=utime ~System=stime ~Args"
};

/* ---------------------------------------------------------------- */

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_ADD_FN isn't NULL, then call DEFAULT_ADD_FN instead. */
static void
_parse_strlist (char *arg,
		void (*add_fn)(char *str), void (*default_add_fn)(),
			   char *type_name)
{
  if (arg)
    while (isspace(*arg))
      arg++;

  if (arg == NULL || *arg == '\0')
    if (default_add_fn)
      (*default_add_fn)();
    else
      error(7, 0, "No %s specified", type_name);
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

/* ---------------------------------------------------------------- */

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_FN isn't NULL, then call ADD_FN on the resutl of calling
   DEFAULT_FN instead, otherwise signal an error.  */
static void
parse_strlist (char *arg,
	       void (*add_fn)(char *str),
	       char *(*default_fn)(),
	       char *type_name)
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
	       int (*lookup_fn)(char *str),
	       char *type_name)
{
  void default_num_add() { (*add_fn)((*default_fn)()); }
  void add_num_str(char *str)
    {
      char *p;
      for (p = str; *p != '\0'; p++)
	if (!isdigit(*p))
	  {
	    (*add_fn)((*lookup_fn)(str));
	    return;
	  }
      (*add_fn)(atoi(str));
    }
  _parse_strlist(arg, add_num_str, default_num_add, type_name);
}

/* ---------------------------------------------------------------- */

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
lookup_user(char *name)
{
  struct passwd *pw = getpwnam(name);
  if (pw == NULL)
    error(2, 0, "%s: Unknown user", name);
  return pw->pw_uid;
}

/* ---------------------------------------------------------------- */

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

int
ids_contains (struct ids *ids, id_t id)
{
  unsigned i;
  for (i = 0; i < ids->num; i++)
    if (ids->ids[i] == id)
      return 1;
  return 0;
}

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  error_t err;
  /* A buffer used for rewriting old-style ps command line arguments that
     need a dash prepended for the parser to understand them.  It gets
     realloced for each successive arg that needs it, on the assumption that
     args don't get parsed multiple times.  */
  char *arg_hack_buf = 0;
  struct ids *only_uids = make_ids (), *not_uids = make_ids ();
  char **tty_names = 0;
  unsigned num_tty_names = 0, tty_names_alloced = 0;
  proc_stat_list_t procset;
  ps_context_t context;
  ps_stream_t output;
  ps_fmt_t fmt;
  char *fmt_string = "default", *sort_key_name = NULL;
  unsigned filter_mask =
    FILTER_OWNER | FILTER_NSESSLDR | FILTER_UNORPHANED | FILTER_PARENTED;
  bool sort_reverse = FALSE, print_heading = TRUE;
  bool squash_bogus_fields = TRUE, squash_nominal_fields = TRUE;
  bool show_threads = FALSE, no_msg_port = FALSE;
  int output_width = -1;	/* Desired max output size.  */

  /* Add a specific process to be printed out.  */
  void add_pid (unsigned pid)
    {
      err = proc_stat_list_add_pid(procset, pid);
      if (err)
	error(3, err, "%d: Can't add process id", pid);
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
      err = proc_stat_list_add_session(procset, sid);
      if (err)
	error(2, err, "Couldn't add session");
    }
  /* Print out all process from the given login collection.  */
  void add_lid(unsigned lid)
    {
      error_t err = proc_stat_list_add_login_coll(procset, lid);
      if (err)
	error(2, err, "Couldn't add login collection");
    }
  /* Print out all process from the given process group.  */
  void add_pgrp(unsigned pgrp)
    {
      error_t err = proc_stat_list_add_pgrp(procset, pgrp);
      if (err)
	error(2, err, "Couldn't add process group");
    }
    
  /* Add a user who's processes should be printed out.  */
  void add_uid (id_t uid)
    {
      ids_add (only_uids, uid);
    }
  /* Add a user who's processes should not be printed out.  */
  void add_not_uid (id_t uid)
    {
      ids_add (not_uids, uid);
    }
  /* Returns TRUE if PS is owned by any of the users in ONLY_UIDS, and none
     in NOT_UIDS.  */
  bool proc_stat_owner_ok(struct proc_stat *ps)
    {
      id_t uid = proc_stat_proc_info (ps)->owner;
      if (only_uids->num > 0 && !ids_contains (only_uids, uid))
	return 0;
      if (not_uids->num > 0 && ids_contains (not_uids, uid))
	return 0;
      return 1;
    }

  /* Add TTY_NAME to the list for which processes with those controlling
     terminals will be printed.  */
  void add_tty_name (char *tty_name)
    {
      if (tty_names_alloced == num_tty_names)
	{
	  tty_names_alloced += tty_names_alloced + 1;
	  tty_names = realloc(tty_names, tty_names_alloced * sizeof(int));
	  if (tty_names == NULL)
	    error(8, ENOMEM, "Can't allocate tty_name list");
	}
      tty_names[num_tty_names++] = tty_name;
    }
  bool proc_stat_has_ctty(struct proc_stat *ps)
    {
      if (proc_stat_has(ps, PSTAT_TTY))
	/* Only match processes whose tty we can figure out.  */
	{
	  ps_tty_t tty = proc_stat_tty(ps);
	  if (tty)
	    {
	      unsigned i;
	      char *name = ps_tty_name(tty);
	      char *short_name = ps_tty_short_name(tty);

	      for (i = 0; i < num_tty_names; i++)
		if ((name && strcmp (tty_names[i], name) == 0)
		    || (short_name && strcmp (tty_names[i], short_name) == 0))
		  return TRUE;
	    }
	}
      return FALSE;
    }

  /* Returns the name of the current controlling terminal.  */
  static char *current_tty_name()
    {
      error_t err;
      ps_tty_t tty;
      mach_port_t cttyid = getcttyid();

      if (cttyid == MACH_PORT_NULL)
	error(2, 0, "No controlling terminal");

      err = ps_context_find_tty_by_cttyid(context, cttyid, &tty);
      if (err)
	error(2, err, "Can't get controlling terminal");

      return ps_tty_name(tty);
    }

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:			/* Non-option argument.  */
	  if (!isdigit (*arg))
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
	  parse_numlist(arg, add_pid, NULL, NULL, "PID");
	  break;

	case 'a':
	  filter_mask &= ~(FILTER_OWNER | FILTER_NSESSLDR); break;
	case 'd':
	  filter_mask &= ~(FILTER_OWNER | FILTER_UNORPHANED); break;
	case 'e':
	  filter_mask = 0; break;
	case 'g':
	  filter_mask &= ~FILTER_NSESSLDR; break;
	case 'x':
	  filter_mask &= ~FILTER_UNORPHANED; break;
	case 'P':
	  filter_mask &= ~FILTER_PARENTED; break;
	case 'f':
	  fmt_string = "full"; break;
	case 'u':
	  fmt_string = "user"; break;
	case 'v':
	  fmt_string = "vmem"; break;
	case 'j':
	  fmt_string = "jobc"; break;
	case 'l':
	  fmt_string = "long"; break;
	case 'M':
	  no_msg_port = TRUE; break;
	case 'H':
	  print_heading = FALSE; break;
	case 'Q':
	  squash_bogus_fields = squash_nominal_fields = FALSE; break;
	case 'n':
	  squash_nominal_fields = FALSE; break;
	case 's':
	  show_threads = TRUE; break;
	case OPT_FMT:
	  fmt_string = arg; break;
	case OPT_SORT:
	  sort_key_name = arg; break;
	case 'r':
	  sort_reverse = TRUE; break;

	case 't':
	  parse_strlist(arg, add_tty_name, current_tty_name, "tty");
	  break;
	case 'o':
	  parse_numlist(arg, add_uid, NULL, lookup_user, "user");
	  break;
	case 'O':
	  parse_numlist (arg, add_not_uid, NULL, lookup_user, "user");
	  break;
	case OPT_SESS:
	  parse_numlist(arg, add_sid, current_sid, NULL, "session id");
	  break;
	case OPT_LOGIN:
	  parse_numlist(arg, add_lid, current_lid, NULL, "login collection");
	  break;
	case OPT_PGRP:
	  parse_numlist(arg, add_pgrp, NULL, NULL, "process group");
	  break;

	case 'w':
	  output_width = arg ? atoi (arg) : 0; /* 0 means `unlimited'.  */
	  break;

	default:
	  return EINVAL;
	}
      return 0;
    }

  struct argp argp = { options, parse_opt, args_doc, doc};

  proc_server = getproc();

  err = ps_context_create(proc_server, &context);
  if (err)
    error(1, err, "ps_context_create");

  err = proc_stat_list_create(context, &procset);
  if (err)
    error(1, err, "proc_stat_list_create");

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0);

  if (only_uids->num == 0 && (filter_mask & FILTER_OWNER))
    add_uid (getuid ());

  {
    int fmt_index = parse_enum(fmt_string, fmt_names, "format type", 1);
    if (fmt_index >= 0)
      {
	fmt_string = fmts[fmt_index];
	if (sort_key_name == NULL)
	  {
	    sort_key_name = fmt_sortkeys[fmt_index];
	    sort_reverse = fmt_sortrev[fmt_index];
	  }
      }
  }

  if (proc_stat_list_num_procs(procset) == 0)
    {
      err = proc_stat_list_add_all(procset);
      if (err)
	error(2, err, "Couldn't get process list");
    }

  if (no_msg_port)
    proc_stat_list_set_flags(procset, PSTAT_NO_MSGPORT);

  /* Filter out any processes that we don't want to show.  */
  if (only_uids->num || not_uids->num)
    proc_stat_list_filter1 (procset, proc_stat_owner_ok,
			    PSTAT_PROC_INFO, FALSE);
  if (num_tty_names > 0)
    {
      /* We set the PSTAT_TTY flag separately so that our filter function
	 can look at any procs that fail to set it.  */
      proc_stat_list_set_flags(procset, PSTAT_TTY);
      proc_stat_list_filter1(procset, proc_stat_has_ctty, 0, FALSE);
    }
  if (filter_mask & FILTER_NSESSLDR)
    proc_stat_list_filter(procset, &ps_not_sess_leader_filter, FALSE);
  if (filter_mask & FILTER_UNORPHANED)
    proc_stat_list_filter(procset, &ps_unorphaned_filter, FALSE);
  if (filter_mask & FILTER_PARENTED)
    proc_stat_list_filter(procset, &ps_parent_filter, FALSE);

  err = ps_fmt_create(fmt_string, ps_std_fmt_specs, &fmt);
  if (err)
    error(4, err, "Can't create output format");

  if (show_threads)
    proc_stat_list_add_threads(procset);

  if (sort_key_name != NULL)
    /* Sort on the given field; we look in both the user-named fields and
       the system named fields for the name given, as the user may only know
       the printed title and not the official name. */
    {
      ps_fmt_spec_t sort_key = NULL;
      unsigned nfields = ps_fmt_num_fields(fmt);
      ps_fmt_field_t field = ps_fmt_fields(fmt);

      /* first, look at the actual printed titles in the current format */
      while (nfields-- > 0)
	if (strcasecmp(sort_key_name, ps_fmt_field_title(field)) == 0)
	  {
	    sort_key = ps_fmt_field_fmt_spec(field);
	    break;
	  }
	else
	  field++;

      /* Then, if not there, look at the actual ps_fmt_spec names (this way
	 the user can sort on a key that's not actually displayed). */
      if (sort_key == NULL)
	{
	  sort_key = find_ps_fmt_spec(sort_key_name, ps_std_fmt_specs);
	  if (sort_key == NULL)
	    error(3, 0, "%s: bad sort key", sort_key_name);
	}

      err = proc_stat_list_sort(procset, sort_key, sort_reverse);
      if (err)
	/* Give an error message, but don't exit.  */
	error(0, err, "Couldn't sort processes");
    }

  if (squash_bogus_fields)
    /* Remove any fields that we can't print anyway (because of system
       bugs/protection violations?).  */
    {
      ps_flags_t bogus_flags = ps_fmt_needs(fmt);

      err = proc_stat_list_find_bogus_flags(procset, &bogus_flags);
      if (err)
	error(0, err, "Couldn't remove bogus fields");
      else
	ps_fmt_squash_flags (fmt, bogus_flags);
    }

  if (squash_nominal_fields)
    /* Remove any fields that contain only `uninteresting' information.  */
    {
      bool nominal (ps_fmt_spec_t spec)
	{
	  return proc_stat_list_spec_nominal (procset, spec);
	}
      ps_fmt_squash (fmt, nominal);
    }

  err = ps_stream_create (stdout, &output);
  if (err)
    error (5, err, "Can't make output stream");

  if (print_heading)
    if (proc_stat_list_num_procs(procset) > 0)
      {
	err = ps_fmt_write_titles (fmt, output);
	if (err)
	  error(0, err, "Can't print titles");
	ps_stream_newline (output);
      }
    else
      error(0, 0, "No applicable processes");

  if (output_width)
    {
      int deduce_term_size (int fd, char *type, int *width, int *height);
      struct ps_fmt_field *field = ps_fmt_fields (fmt);
      int nfields = ps_fmt_num_fields (fmt);

      if (output_width < 0)
	/* Have to figure it out!  */
	if (! deduce_term_size (1, getenv ("TERM"), &output_width, 0))
	  output_width = 80;	/* common default */

      /* We're not very clever about this -- just see if the last field is
	 `varying' (as it usually is), then set it to the proper max width.  */
      while (--nfields > 0)
	{
	  int fw = field->width;
	  output_width -= field->pfx_len + (fw < 0 ? -fw : fw);
	  field++;
	}
      if (nfields == 0 && field->width == 0 && output_width > 0)
	field->width = output_width - field->pfx_len - 1; /* 1 for the CR. */
    }

  err = proc_stat_list_fmt (procset, fmt, output);
  if (err)
    error (5, err, "Couldn't output process status");

  exit(0);
}
