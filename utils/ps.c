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

#include "error.h"
#include "ps.h"
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
                             `hurd', `hurd-long', or a format-string\n\
  -g                         Include session leaders\n\
  -H, --no-header            Don't print a descriptive header line\n\
  -l                         Use the `long' output-format'\n\
      --login[=LID]	     Add the processes from the login collection LID\n\
                             (which defaults that of the current process)\n
  -P, --no-parent	     Include processes without parents\n\
  -Q, --all-fields	     Don't elide unusable fields (normally if there's\n\
                             some reason ps can't print a field for any\n\
                             process, it's removed from the output entirely)\n\
  -r, --reverse              Reverse the order of any sort\n\
      --session[=SID]	     Add the processes from the session SID (which\n\
                             defaults to the sid of the current process)\n
      --sort=FIELD           Sort the output with respect to FIELD\n\
  -t, --threads		     Show the threads for each process\n\
  -u                         Use the `user' output-format'\n\
  -v                         Use the `vmem' output-format'\n\
  -x                         Include orphaned processes\n\
");
    }

  exit(status);
}

int 
enum_name(char *arg, char **choices, char *kind, int allow_mismatches)
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
      fprintf(stderr, "%s: invalid %s", arg, kind);
      usage(1);
    }
}

/* ---------------------------------------------------------------- */

#define FILTER_OWN		0x01
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

/* -1 is EOF */
#define OPT_LOGIN	-2
#define OPT_SESSION	-3
#define OPT_SORT	-4
#define OPT_FMT		-5
#define OPT_HELP	-6

#define SHORT_OPTIONS "agHlPQrtuvx"

static struct option options[] =
{
  {"all-users", no_argument, 0, 'a'},
  {"all-fields", no_argument, 0, 'Q'},
  {"fmt", required_argument, 0, OPT_FMT},
  {"help", no_argument, 0, OPT_HELP},
  {"login", optional_argument, 0, OPT_LOGIN},
  {"no-header", no_argument, 0, 'H'},
  {"no-squash", no_argument, 0, 'Q'},
  {"no-parent", no_argument, 0, 'P'},
  {"session", optional_argument, 0, OPT_SESSION},
  {"threads", no_argument, 0, 't'},
  {"reverse", no_argument, 0, 'r'},
  {"sort", required_argument, 0, OPT_SORT},
  {0, 0, 0, 0}
};

/* The names of various predefined output formats.  */
char *fmt_names[] =
  {"default", "user", "vmem", "long", "hurd", "hurd-long", 0};
/* How each of those formats should be sorted; */
char *fmt_sortkeys[] =
  {"pid", "%cpu", "%mem", "pid", "pid", "pid"};
/* and whether the sort should be backwards or forwards; */ 
bool fmt_sortrev[] =
  {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE};
/* and the actual format strings.  */
char *fmts[] =
{
  "~PID ~TH# ~TT=tty ~STAT=state ~TIME ~COMMAND=args",
  "~USER ~PID ~TH# ~%CPU ~%MEM ~SZ=vsize ~RSS=rsize ~TT=tty ~STAT=state ~COMMAND=args",
  "~PID ~TH# ~STAT=state ~SL=sleep ~PAGEIN=pgins ~FAULTS=pgflts ~COWFLT=cowflts ~ZFILLS ~SIZE=vsize ~RSS=rsize ~%CPU ~%MEM ~COMMAND=args",
  "~UID ~PID ~TH# ~PPID ~PRI ~NI=bpri ~TH=nth ~MSGIN=msgsin ~MSGOUT=msgsout ~SZ=vsize ~RSS=rsize ~STAT=state ~TT=tty ~TIME ~COMMAND=args",
  "~PID ~Th# ~UID ~NTh ~VMem=vsize ~RSS=rsize ~User=utime ~System=stime ~Args",
  "~PID ~Th# ~UID ~PPID ~PGRP ~Sess ~NTh ~VMem=vsize ~RSS=rsize ~%CPU ~User=utime ~System=stime ~Args"
};

void 
main(int argc, char *argv[])
{
  int opt;
  error_t err;
  ps_fmt_t fmt;
  proc_stat_list_t procset;
  process_t cur_proc = getproc();
  int cur_pid = getpid();
  char *fmt_string = "default", *sort_key_name = NULL;
  int filter_mask =
    FILTER_OWN | FILTER_NSESSLDR | FILTER_UNORPHANED | FILTER_PARENTED;
  bool sort_reverse = FALSE, print_heading = TRUE, squash_bogus_fields = TRUE;
  bool show_threads = FALSE;

  program_invocation_short_name = argv[0];

  err = proc_stat_list_create(cur_proc, &procset);
  if (err)
    error(1, err, "proc_stat_list_create");

  while ((opt = getopt_long(argc, argv, SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 'a':
	filter_mask &= ~FILTER_OWN; break;
      case 'g':
	filter_mask &= ~FILTER_NSESSLDR; break;
      case 'x':
	filter_mask &= ~FILTER_UNORPHANED; break;
      case 'P':
	filter_mask &= ~FILTER_PARENTED; break;
      case 'u':
	fmt_string = "user"; break;
      case 'v':
	fmt_string = "vmem"; break;
      case 'l':
	fmt_string = "long"; break;
      case 'H':
	print_heading = 0; break;
      case 'Q':
	squash_bogus_fields = FALSE; break;
      case 't':
	show_threads = TRUE; break;
      case OPT_FMT:
	fmt_string = optarg; break;
      case OPT_SORT:
	sort_key_name = optarg; break;
      case 'r':
	sort_reverse = TRUE; break;
      case OPT_SESSION:
	{
	  int sid = (optarg == NULL ? -1 : atoi(optarg));

	  if (sid < 0) {
	    /* Get the current session; a bit of a pain */
	    err = proc_getsid(cur_proc, cur_pid, &sid);
	    if (err)
	      error(2, err, "Couldn't get current session id");
	  }

	  err = proc_stat_list_add_session(procset, sid);
	  if (err)
	    error(2, err, "Couldn't add session pids");
	}
	break;
      case OPT_LOGIN:
	{
	  int login_coll = (optarg == NULL ? -1 : atoi(optarg));

	  if (login_coll < 0) {
	    err = proc_getloginid(cur_proc, cur_pid, &login_coll);
	    if (err)
	      error(2, err, "Couldn't get current login collection");
	  }

	  err = proc_stat_list_add_login_coll(procset, login_coll);
	  if (err)
	    error(2, err, "Couldn't add login collection pids");
	}
	break;
      case OPT_HELP:
	usage(0);
      default:
	usage(1);
      }

  {
    int fmt_index = enum_name(fmt_string, fmt_names, "format type", 1);
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

  while (argv[optind] != NULL)
    {
      int pid = atoi(argv[optind]);

      if (pid == 0 && strcmp(argv[optind], "0") != 0)
	error(3, 0, "%s: invalid process id", argv[optind]);

      err = proc_stat_list_add_pid(procset, pid);
      if (err)
	error(3, err, "%d: can't add process id", pid);

      optind++;
    }

  if (proc_stat_list_num_procs(procset) == 0)
    {
      err = proc_stat_list_add_all(procset);
      if (err)
	error(2, err, "Couldn't get process list");
    }

  if (filter_mask & FILTER_OWN)
    proc_stat_list_filter(procset, &ps_own_filter, FALSE);
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
      int nfields = ps_fmt_num_fields(fmt);
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
      int bogus_flags = ps_fmt_needs(fmt);

      err = proc_stat_list_find_bogus_flags(procset, &bogus_flags);
      if (err)
	error(0, err, "Couldn't remove bogus fields");
      else
	ps_fmt_squash(fmt, bogus_flags);
    }

  if (print_heading)
    {
      err = ps_fmt_write_titles(fmt, stdout, NULL);
      if (err)
	error(0, err, "Can't print titles");
      putc('\n', stdout);
    }

  err = proc_stat_list_fmt(procset, fmt, stdout, NULL);
  if (err)
    error(5, err, "Couldn't output process status");

  exit(0);
}
