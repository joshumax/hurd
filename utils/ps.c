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
#include <getopt.h>
#include <hurd/ps.h>
#include <unistd.h>

#include "error.h"
#include "common.h"

/* ---------------------------------------------------------------- */

static void
usage(status)
     int status;
{
  if (status != 0)
    fprintf(stderr, "Try `%s --help' for more information.\n",
	    program_invocation_short_name);
  else
    {
      printf("Usage: %s [OPTION...] [PID...]\n",
	     program_invocation_short_name);
      printf("\
\n\
  -a, --all-users            List other users' processes\n\
      --fmt=FMT              Use the output-format FMT\n\
                             FMT may be `default', `user', `vmem', `long',\n\
                             `jobc', `full', `hurd', `hurd-long',\n\
                             or a custom format-string\n\
  -d                         List all processes except process group leaders\n\
  -e, --all                  List all processes\n\
  -f                         Use the `full' output-format\n\
  -g                         Include session leaders\n\
  -H, --no-header            Don't print a descriptive header line\n\
  -j                         Use the `jobc' output-format\n\
  -l                         Use the `long' output-format\n\
      --login[=LID]          Add the processes from the login collection LID\n\
                             (which defaults that of the current process)\n\
  -M, --no-msg-port          Don't show info that uses a process's msg port\n\
  -oUSER, --owner=USER       Show processes owned by USER\n\
  -pPID, --pid=PID           List the process PID\n\
      --pgrp=PGRP            List processes in the process group PGRP\n\
  -P, --no-parent            Include processes without parents\n\
  -Q, --all-fields           Don't elide unusable fields (normally if there's\n\
                             some reason ps can't print a field for any\n\
                             process, it's removed from the output entirely)\n\
  -r, --reverse              Reverse the order of any sort\n\
      --session[=SID]        Add the processes from the session SID (which\n\
                             defaults to the sid of the current process)\n\
      --sort=FIELD           Sort the output with respect to FIELD\n\
  -s, --threads              Show the threads for each process\n\
  -tTTY, --tty=TTY           Only show processes who's controlling tty is TTY\n\
  -u                         Use the `user' output-format\n\
  -v                         Use the `vmem' output-format\n\
  -x                         Include orphaned processes\n\
\n\
The USER, LID, PID, PGRP, and SID arguments may also be comma separated lists.\n\
The System V options -u and -g may be accessed with -o and --pgrp.\n\
");
    }

  exit(status);
}

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
	    {
	      fprintf(stderr, "%s: ambiguous %s", arg, kind);
	      usage(1);
	    }
	  else
	    partial_match = p - choices;
	p++;
      }

  if (partial_match >= 0 || allow_mismatches)
    return partial_match;
  else
    {
      fprintf(stderr, "%s: Invalid %s", arg, kind);
      usage(1);
      return 0;
    }
}

/* ---------------------------------------------------------------- */

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

/* Long options without corresponding short ones.  -1 is EOF.  */
#define OPT_LOGIN	-2
#define OPT_SESSION	-3
#define OPT_SORT	-4
#define OPT_FMT		-5
#define OPT_HELP	-6
#define OPT_PGRP	-7

#define SHORT_OPTIONS "adefgHjlMo:p:PQrt::suvx"

static struct option options[] =
{
  {"all", no_argument, 0, 'e'},
  {"all-users", no_argument, 0, 'a'},
  {"all-fields", no_argument, 0, 'Q'},
  {"fmt", required_argument, 0, OPT_FMT},
  {"help", no_argument, 0, OPT_HELP},
  {"login", optional_argument, 0, OPT_LOGIN},
  {"lid", optional_argument, 0, OPT_LOGIN},
  {"no-header", no_argument, 0, 'H'},
  {"no-msg-port", no_argument, 0, 'M'},
  {"no-squash", no_argument, 0, 'Q'},
  {"no-parent", no_argument, 0, 'P'},
  {"owner", required_argument, 0, 'o'},
  {"pgrp", required_argument, 0, OPT_PGRP},
  {"session", optional_argument, 0, OPT_SESSION},
  {"sid", optional_argument, 0, OPT_SESSION},
  {"threads", no_argument, 0, 's'},
  {"tty", optional_argument, 0, 't'},
  {"reverse", no_argument, 0, 'r'},
  {"sort", required_argument, 0, OPT_SORT},
  {0, 0, 0, 0}
};

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
  "~UID ~PID ~TH# ~PPID ~PRI ~NI=bpri ~TH=nth ~MSGI=msgin ~MSGO=msgout ~SZ=vsize ~RSS=rsize ~SC=susp ~STAT=state ~TT=tty ~TIME ~COMMAND=args",
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
static uid_t
lookup_user(char *name)
{
  struct passwd *pw = getpwnam(name);
  if (pw == NULL)
    error(2, 0, "%s: Unknown user", name);
  return pw->pw_uid;
}

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  int opt;
  error_t err;
  unsigned num_uids = 0, uids_alloced = 1;
  uid_t *uids = malloc(sizeof(uid_t) * num_uids);
  unsigned num_tty_names = 0, tty_names_alloced = 1;
  char **tty_names = malloc(sizeof(char *) * num_tty_names);
  proc_stat_list_t procset;
  ps_context_t context;
  ps_fmt_t fmt;
  char *fmt_string = "default", *sort_key_name = NULL;
  unsigned filter_mask =
    FILTER_OWNER | FILTER_NSESSLDR | FILTER_UNORPHANED | FILTER_PARENTED;
  bool sort_reverse = FALSE, print_heading = TRUE, squash_bogus_fields = TRUE;
  bool show_threads = FALSE, no_msg_port = FALSE;

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
  void add_uid (unsigned uid)
    {
      if (uids_alloced == num_uids)
	{
	  uids_alloced *= 2;
	  uids = realloc(uids, uids_alloced * sizeof(int));
	  if (uids == NULL)
	    error(8, ENOMEM, "Can't allocate uid list");
	}
      uids[num_uids++] = uid;
    }
  /* Returns TRUE if PS is owned by any of the users in UIDS.  */
  bool proc_stat_has_owner(struct proc_stat *ps)
    {
      unsigned i;
      uid_t uid = proc_stat_info(ps)->owner;
      for (i = 0; i < num_uids; i++)
	if (uids[i] == uid)
	  return TRUE;
      return FALSE;
    }

  /* Add TTY_NAME to the list for which processes with those controlling
     terminals will be printed.  */
  void add_tty_name (char *tty_name)
    {
      if (tty_names_alloced == num_tty_names)
	{
	  tty_names_alloced *= 2;
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

  proc_server = getproc();

  err = ps_context_create(proc_server, &context);
  if (err)
    error(1, err, "ps_context_create");

  err = proc_stat_list_create(context, &procset);
  if (err)
    error(1, err, "proc_stat_list_create");

  /* Parse our options.  */
  while ((opt = getopt_long(argc, argv, "-" SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 1:			/* Non-option argument.  */
	if (!isdigit(*optarg))
	  /* Old-fashioned `ps' syntax takes options without the leading dash.
	     Prepend a dash and feed back to getopt.  */
	  {
	    size_t len = strlen (optarg) + 1;
	    argv[--optind] = alloca (1 + len);
	    argv[optind][0] = '-';
	    memcpy (&argv[optind][1], optarg, len);
	    break;
	  }
	/* Otherwise, fall through and treat the arg as a process id.  */
      case 'p':
	parse_numlist(optarg, add_pid, NULL, NULL, "PID");
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
	squash_bogus_fields = FALSE; break;
      case 's':
	show_threads = TRUE; break;
      case OPT_FMT:
	fmt_string = optarg; break;
      case OPT_SORT:
	sort_key_name = optarg; break;
      case 'r':
	sort_reverse = TRUE; break;

      case 't':
	parse_strlist(optarg, add_tty_name, current_tty_name, "tty");
	break;
      case 'o':
	parse_numlist(optarg, add_uid, NULL, lookup_user, "user");
	break;
      case OPT_SESSION:
	parse_numlist(optarg, add_sid, current_sid, NULL, "session id");
	break;
      case OPT_LOGIN:
	parse_numlist(optarg, add_lid, current_lid, NULL, "login collection");
	break;
      case OPT_PGRP:
	parse_numlist(optarg, add_pgrp, NULL, NULL, "process group");
	break;

      case OPT_HELP:
	usage(0);
      default:
	usage(1);
      }

  while (argv[optind] != NULL)
    parse_numlist(argv[optind++], add_pid, NULL, NULL, "PID");

  if (num_uids == 0 && (filter_mask & FILTER_OWNER))
    add_uid(getuid());

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
  if (num_uids > 0)
    proc_stat_list_filter1(procset, proc_stat_has_owner, PSTAT_INFO, FALSE);
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
	ps_fmt_squash(fmt, bogus_flags);
    }

  if (print_heading)
    if (proc_stat_list_num_procs(procset) > 0)
      {
	err = ps_fmt_write_titles(fmt, stdout, NULL);
	if (err)
	  error(0, err, "Can't print titles");
	putc('\n', stdout);
      }
    else
      error(0, 0, "No applicable processes");

  err = proc_stat_list_fmt(procset, fmt, stdout, NULL);
  if (err)
    error(5, err, "Couldn't output process status");

  exit(0);
}
