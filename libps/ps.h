/* Routines to gather and print process information.

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

#ifndef __PS_H__
#define __PS_H__

#include <hurd/hurd_types.h>
#include <mach/mach.h>
#include <pwd.h>
#include <ihash.h>
#include <errno.h>

#ifndef bool
#define bool int
#endif

/* ---------------------------------------------------------------- */
/* A PS_USER_T hold info about a particular user.  */

typedef struct ps_user *ps_user_t;

/* Possible states a ps_user's passwd can be in: valid, not fet */
enum ps_user_passwd_state
  { PS_USER_PASSWD_OK, PS_USER_PASSWD_PENDING, PS_USER_PASSWD_ERROR };

struct ps_user
{
  /* Which user this refers to.  */
  uid_t uid;

  /* The status */
     enum ps_user_passwd_state passwd_state;
     
  /* The user's password file entry.  Only valid if PASSWD_STATE ==
     PS_USER_PASSWD_OK.  */
  struct passwd passwd;

  /* String storage for strings pointed to by ENTRY.  */
  char *storage;
};

#define ps_user_uid(u) ((u)->uid)

/* Create a ps_user_t for the user referred to by UID, returning it in U.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t ps_user_create(uid_t uid, ps_user_t *u);

/* Free U and any resources it consumes.  */
void ps_user_free(ps_user_t u);

/* Returns the password file entry (struct passwd, from <pwd.h>) for the user
   referred to by U, or NULL if it can't be gotten.  */
struct passwd *ps_user_passwd(ps_user_t u);

/* Returns the user name for the user referred to by U, or NULL if it can't
   be gotten.  */
char *ps_user_name(ps_user_t u);

/* ---------------------------------------------------------------- */
/* A ps_tty_t holds information about a terminal.  */

typedef struct ps_tty *ps_tty_t;

/* Possible states a ps_tty's name can be in: valid, not fetched yet,
   couldn't fetch.  */
enum ps_tty_name_state
  { PS_TTY_NAME_OK, PS_TTY_NAME_PENDING, PS_TTY_NAME_ERROR };

struct ps_tty {
  /* Which tty this refers to.  */
  file_t port;
  
  /* The name of the tty, if we could figure it out.  */
  char *name;
  /* What state the name is in.  */
  enum ps_tty_name_state name_state;

  /* A more abbreviated name for the tty, or NULL if no name at all.  */
  char *short_name;
  bool short_name_alloced : 1;
};

#define ps_tty_port(tty) ((tty)->port)

/* Create a ps_tty_t for the tty referred to by PORT, returning it in TTY.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t ps_tty_create(file_t port, ps_tty_t *tty);

/* Frees TTY and any resources it consumes.  */
void ps_tty_free(ps_tty_t tty);

/* Returns the name of the tty, or NULL if it can't be figured out.  */
char *ps_tty_name(ps_tty_t tty);

/* Returns the standard abbreviated name of the tty, the whole name if there
   is no standard abbreviation, or NULL if it can't be figured out.  */
char *ps_tty_short_name(ps_tty_t tty);

/* ---------------------------------------------------------------- */
/* A ps_contex_t holds various information resulting from querying a
   particular process server, in particular a group of proc_stats, ps_users,
   and ps_ttys.  This information sticks around until the context is freed
   (subsets may be created by making proc_stat_lists).  */

typedef struct ps_context *ps_context_t;
typedef struct proc_stat *proc_stat_t;

struct ps_context
{
  /* The process server our process info is from.  */
  process_t server;

  /* proc_stat_t's for every process we know about, indexed by process id.  */
  ihash_t procs;

  /* ps_tty_t's for every tty we know about, indexed by the terminal port.  */
  ihash_t ttys;

  /* ps_tty_t's for every tty we know about, indexed by their ctty id port
     (from libc).  */
  ihash_t ttys_by_cttyid;

  /* ps_user_t's for every user we know about, indexed by user-id.  */
  ihash_t users;
};

#define ps_context_server(pc) ((pc)->server)

/* Returns in PC a new ps_context_t for the proc server SERVER.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t ps_context_create(process_t server, ps_context_t *pc);

/* Frees PC and any resources it consumes.  */
void ps_context_free(ps_context_t pc);

/* Find a proc_stat_t for the process referred to by PID, and return it in
   PS.  If an error occurs, it is returned, otherwise 0.  */
error_t ps_context_find_proc_stat(ps_context_t pc, pid_t pid, proc_stat_t *ps);

/* Find a ps_tty_t for the terminal referred to by the port TTY_PORT, and
   return it in TTY.  If an error occurs, it is returned, otherwise 0.  */
error_t ps_context_find_tty(ps_context_t pc, mach_port_t tty_port, ps_tty_t *tty);

/* Find a ps_tty_t for the terminal referred to by the ctty id port
   CTTYID_PORT, and return it in TTY.  If an error occurs, it is returned,
   otherwise 0.  */
error_t ps_context_find_tty_by_cttyid(ps_context_t pc,
				      mach_port_t cttyid_port, ps_tty_t *tty);

/* Find a ps_user_t for the user referred to by UID, and return it in U.  */
error_t ps_context_find_user(ps_context_t pc, uid_t uid, ps_user_t *u);

/* ---------------------------------------------------------------- */
/*
   A PROC_STAT_T holds lots of info about the process PID at SERVER; exactly
   which info is dependent on its FLAGS field.
 */

typedef unsigned ps_flags_t;
typedef unsigned ps_state_t;

struct proc_stat
  {
    /* Which process server this is from.  */
    ps_context_t context;

    /* The proc's process id; if <0 then this is a thread, not a process.  */
    pid_t pid;

    /* Flags describing which fields in this structure are valid.  */
    ps_flags_t flags;

    /* Thread fields -- these are valid if PID < 0.  */
    proc_stat_t thread_origin;	/* A proc_stat_t for the task we're in.  */
    unsigned thread_index;		/* Which thread in our proc we are.  */

    /* A process_t port for the process.  */
    process_t process;

    /* The mach task port for the process.  */
    task_t task;

    /* A libc msgport for the process.  This port is responded to by the
       process itself (usually by the c library); see <hurd/msg.defs> for the
       standard set of rpcs you can send to this port.  Accordingly, you
       cannot depend on a timely (or any) reply to messages sent here --
       program carefully!  */
    mach_port_t msgport;

    /* A pointer to the process's procinfo structure (as returned by
       proc_getinfo; see <hurd/hurd_types.h>).  */
    struct procinfo *info;
    /* The size of the info structure for deallocation purposes.  */
    unsigned info_size;

    /* Summaries of the proc's thread_{basic,sched}_info_t structures: sizes
       and cumulative times are summed, prioritys and delta time are
       averaged.  The run_states are added by having running thread take
       precedence over waiting ones, and if there are any other incompatible
       states, simply using a bogus value of -1 */
    thread_basic_info_data_t thread_basic_info;
    thread_sched_info_data_t thread_sched_info;

    /* Exec flags (see EXEC_* in <hurd/hurd_types.h>).  */
    unsigned exec_flags;

    /* A bitmask summarizing the scheduling state of this process and all its
       threads.  See the PSTAT_STATE_ defines below for a list of bits.  */
    ps_state_t state;

    /* A ps_user_t object for the owner of this process.  */
    ps_user_t owner;

    /* The process's argv, as a string with each element separated by '\0'.  */
    char *args;
    /* The length of ARGS.  */
    unsigned args_len;

    /* Virtual memory statistics for the process, as returned by task_info;
       see <mach/task_info.h> for a description of task_events_info_t.  */
    task_events_info_t task_events_info;
    task_events_info_data_t task_events_info_buf;
    unsigned task_events_info_size;

    /* Various libc ports:  */

    /* The process's ctty id port, or MACH_PORT_NULL if the process has no
       controlling terminal.  Note that this is just a magic cookie; we use
       it to fetch a port to the actual terminal -- it's not useful for much
       else.  */
    mach_port_t cttyid;

    /* A port to the process's current working directory.  */
    mach_port_t cwdir;

    /* The process's auth port, which we can use to determine who the process
       is authenticated as.  */
    mach_port_t auth;

    /* The process's umask, which controls which protection bits won't be set
       when creating a file.  */
    unsigned umask;

    /* A ps_tty_t object for the process's controlling terminal.  */
    ps_tty_t tty;
  };


/* Proc_stat flag bits; each bit is set in the FLAGS field if that
   information is currently valid.  */
#define PSTAT_PID		0x0001 /* Process ID.  */
#define PSTAT_THREAD		0x0002 /* thread_index & thread_origin */
#define PSTAT_PROCESS		0x0004 /* The process_t for the process.  */
#define PSTAT_TASK		0x0008 /* The task port for the process.  */
#define PSTAT_MSGPORT		0x0010 /* The process's msgport.  */
#define PSTAT_INFO		0x0020 /* A struct procinfo for the process. */
#define PSTAT_THREAD_INFO	0x0040 /* Thread summary info.  */
#define PSTAT_ARGS		0x0080 /* The process's args.  */
#define PSTAT_TASK_EVENTS_INFO	0x0100 /* A task_events_info_t for the proc. */
#define PSTAT_STATE		0x0200 /* A bitmask describing the process's
					  state (see below).  */
#define PSTAT_CTTYID		0x0800 /* The process's CTTYID port.  */
#define PSTAT_CWDIR		0x1000 /* A file_t for the proc's CWD.  */
#define PSTAT_AUTH		0x2000 /* The proc's auth port.  */
#define PSTAT_TTY		0x4000 /* A ps_tty_t for the proc's terminal.*/
#define PSTAT_OWNER		0x8000 /* A ps_user_t for the proc's owner.  */
#define PSTAT_UMASK	       0x10000 /* The proc's current umask.  */
#define PSTAT_EXEC_FLAGS       0x20000	/* The process's exec flags.  */

#define PSTAT_NUM_THREADS PSTAT_INFO

/* Flag bits that don't correspond precisely to any field.  */
#define PSTAT_NO_MSGPORT      0x100000 /* Don't use the msgport at all.  */

/* If the PSTAT_STATE flag is set, then the proc_stat's state field holds a
   bitmask of the following bits, describing the process's run state.  */
#define PSTAT_STATE_RUNNING  0x0001	/* R */
#define PSTAT_STATE_STOPPED  0x0002	/* T stopped (e.g., by ^Z) */
#define PSTAT_STATE_HALTED   0x0004	/* H */
#define PSTAT_STATE_WAIT     0x0008	/* D short term (uninterruptable) wait */
#define PSTAT_STATE_SLEEPING 0x0010	/* S sleeping */
#define PSTAT_STATE_IDLE     0x0020	/* I idle (sleeping > 20 seconds) */
#define PSTAT_STATE_SWAPPED  0x0040	/* W */
#define PSTAT_STATE_NICED    0X0080	/* N lowered priority */
#define PSTAT_STATE_PRIORITY 0x0100	/* < raised priority */
#define PSTAT_STATE_ZOMBIE   0x0200	/* Z process exited but not yet reaped */
#define PSTAT_STATE_FG	     0x0400	/* + in foreground process group */
#define PSTAT_STATE_SESSLDR  0x0800	/* s session leader */
#define PSTAT_STATE_FORKED   0x1000	/* f has forked and not execed */
#define PSTAT_STATE_NOMSG    0x2000	/* m no msg port */
#define PSTAT_STATE_NOPARENT 0x4000	/* p no parent */
#define PSTAT_STATE_ORPHANED 0x8000	/* o orphaned */
#define PSTAT_STATE_TRACED   0x10000    /* x traced */


/* This is a constant string holding a single character for each possible bit
   in a proc_stat's STATE field, in order from bit zero.  These are intended
   for printint a user-readable summary of a process's state. */
char *proc_stat_state_tags;

/*
   Process info accessor functions.

   You must be sure that the associated flag bit is set before accessing a
   field in a proc_stat_t!  A field FOO (with accessor macro proc_foo()), has
   a flag named PSTAT_FOO.  If the flag is'nt set, you may attempt to set it
   with proc_stat_set_flags (but note that this may not succeed).
 */

/* FLAGS doesn't have a flag bit; it's always valid */
#define proc_stat_flags(ps) ((ps)->flags)

/* These both use the flag PSTAT_THREAD.  */
#define proc_stat_thread_origin(ps) ((ps)->thread_origin)
#define proc_stat_thread_index(ps) ((ps)->thread_index)

#define proc_stat_pid(ps) ((ps)->pid)
#define proc_stat_process(ps) ((ps)->process)
#define proc_stat_task(ps) ((ps)->task)
#define proc_stat_msgport(ps) ((ps)->msgport)
#define proc_stat_info(ps) ((ps)->info)
#define proc_stat_num_threads(ps) ((ps)->info->nthreads)
#define proc_stat_thread_basic_info(ps) (&(ps)->thread_basic_info)
#define proc_stat_thread_sched_info(ps) (&(ps)->thread_sched_info)
#define proc_stat_args(ps) ((ps)->args)
#define proc_stat_args_len(ps) ((ps)->args_len)
#define proc_stat_state(ps) ((ps)->state)
#define proc_stat_cttyid(ps) ((ps)->cttyid)
#define proc_stat_cwdir(ps) ((ps)->cwdir)
#define proc_stat_owner(ps) ((ps)->owner)
#define proc_stat_auth(ps) ((ps)->auth)
#define proc_stat_umask(ps) ((ps)->umask)
#define proc_stat_tty(ps) ((ps)->tty)
#define proc_stat_task_events_info(ps) ((ps)->task_events_info)
#define proc_stat_has(ps, needs) (((ps)->flags & needs) == needs)

/* True if PS refers to a thread and not a process.  */
#define proc_stat_is_thread(ps) ((ps)->pid < 0)

/* Returns in PS a new proc_stat_t for the process PID in the ps context PC.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.
   Users shouldn't use this routine, use pc_context_find_proc_stat instead.  */
error_t _proc_stat_create(pid_t pid, ps_context_t context, proc_stat_t *ps);

/* Frees PS and any memory/ports it references.  Users shouldn't use this
   routine; proc_stat_ts are normally freed only when their ps_context goes
   away.  */
void _proc_stat_free(proc_stat_t ps);

/* Adds FLAGS to PS's flags, fetching information as necessary to validate
   the corresponding fields in PS.  Afterwards you must still check the flags
   field before using new fields, as something might have failed.  Returns
   a system error code if a fatal error occurred, and 0 otherwise.  */
error_t proc_stat_set_flags(proc_stat_t ps, ps_flags_t flags);

/* Returns in THREAD_PS a proc_stat_t for the Nth thread in the proc_stat_t
   PS (N should be between 0 and the number of threads in the process).  The
   resulting proc_stat_t isn't fully functional -- most flags can't be set in
   it.  If N was out of range, EINVAL is returned.  If a memory allocation
   error occured, ENOMEM is returned.  Otherwise, 0 is returned.  */
error_t proc_stat_thread_create(proc_stat_t ps, unsigned n, proc_stat_t *thread_ps);

/* ---------------------------------------------------------------- */
/*
   A PS_GETTER_T describes how to get a particular value from a PROC_STAT_T.

   To get a value from a proc_stat_t PS with a getter, you must make sure all
   the pstat_flags returned by ps_getter_needs(GETTER) are set in PS, and
   then call the function returned ps_getter_function(GETTER) with PS as the
   first argument.

   The way the actual value is returned from this funciton is dependent on
   the type of the value:
      For int's and float's, the value is the return value.
      For strings, you must pass in two extra arguments, a char **, which is
        filled in with a pointer to the string, or NULL if the string is
	NULL, and an int *, which is filled in with the length of the string.
*/

typedef struct ps_getter *ps_getter_t;

struct ps_getter
  {
    /* The getter's name */
    char *name;

    /* What proc_stat flags need to be set as a precondition to calling this
       getter's function.  */
    ps_flags_t needs;

    /* A function that will get the value; the protocol between this function
       and its caller is type-dependent.  */ 
    void (*fn) ();
  };

/* Access macros: */
#define ps_getter_name(g) ((g)->name)
#define ps_getter_needs(g) ((g)->needs)
#define ps_getter_function(g) ((g)->fn)

/* ---------------------------------------------------------------- */
/* A PS_FILTER_T describes how to select some subset of a PROC_STAT_LIST_T */

typedef struct ps_filter *ps_filter_t;

struct ps_filter
  {
    /* Name of this filter.  */
    char *name;

    /* The flags that need to be set in each proc_stat_t in the list to
       call the filter's predicate function; if these flags can't be set in a
       particular proc_stat_t, the function is not called, and it isn't deleted
       from the list.  */
    ps_flags_t needs;

    /* A function that returns true if called on a proc_stat_t that the
       filter accepts, or false if the filter rejects it.  */
    bool(*fn) (proc_stat_t ps);
  };

/* Access macros: */
#define ps_filter_name(f) ((f)->name)
#define ps_filter_needs(f) ((f)->needs)
#define ps_filter_predicate(f) ((f)->fn)

/* Some predefined filters.  These are structures; you must use the &
   operator to get a ps_filter_t from them */

/* A filter that retains only process's owned by getuid() */
struct ps_filter ps_own_filter;
/* A filter that retains only process's that aren't session leaders */
struct ps_filter ps_not_sess_leader_filter;
/* A filter that retains only process's with a controlling terminal */
struct ps_filter ps_ctty_filter;
/* A filter that retains only `unorphaned' process.  A process is unorphaned
   if it's a session leader, or the process's process group is not orphaned */
struct ps_filter ps_unorphaned_filter;
/* A filter that retains only `parented' process.  Typically only hurd
   processes have parents.  */
struct ps_filter ps_parent_filter;

/* ---------------------------------------------------------------- */
/*
   A PS_FMT_SPEC_T describes how to output something from a PROC_STAT_T; it
   is a combination of a getter (describing how to get the value), an output 
   function (which outputs the result of the getter), and a compare function
   (which can be used to sort proc_stat_t's according to how they are
   output).  It also specifies the default width of the field in which the
   output should be printed.
   */

typedef struct ps_fmt_spec *ps_fmt_spec_t;

struct ps_fmt_spec
  {
    /* The name of the spec (and it's default title) */
    char *name;
    
    ps_getter_t getter;

    /* A function that, given a ps, a getter, a field width, and a stream,
       will output what the getter gets in some format */
    error_t
      (*output_fn)(proc_stat_t ps, ps_getter_t getter,
		   int width, FILE *str, unsigned *count);

    /* A function that, given two pses and a getter, will compare what
       the getter gets for each ps, and return an integer ala qsort */
    int (*cmp_fn)(proc_stat_t ps1, proc_stat_t ps2, ps_getter_t getter);

    /* The default width of the field that this spec will be printed in if not
       overridden.  */
    int default_width;
  };

/* Accessor macros:  */
#define ps_fmt_spec_name(spec) ((spec)->name)
#define ps_fmt_spec_output_fn(spec) ((spec)->output_fn)
#define ps_fmt_spec_compare_fn(spec) ((spec)->cmp_fn)
#define ps_fmt_spec_getter(spec) ((spec)->getter)
#define ps_fmt_spec_default_width(spec) ((spec)->default_width)

/* Returns true if a pointer into an array of struct ps_fmt_specs is at  the
   end.  */
#define ps_fmt_spec_is_end(spec) ((spec)->name == NULL)

/* An array of struct ps_fmt_spec, suitable for use with find_ps_fmt_spec, 
   containing specs for most values in a proc_stat_t.  */
struct ps_fmt_spec ps_std_fmt_specs[];

/* Searches for a spec called NAME in SPECS (an array of struct ps_fmt_spec)
   and returns it if found, otherwise NULL.  */
ps_fmt_spec_t find_ps_fmt_spec(char *name, ps_fmt_spec_t specs);

/* ---------------------------------------------------------------- */
/* A PS_FMT_T describes how to output user-readable  version of a proc_stat_t.
   It consists of a series of PS_FMT_FIELD_Ts, each describing how to output
   one value.  */

/* PS_FMT_FIELD_T */
typedef struct ps_fmt_field *ps_fmt_field_t;
struct ps_fmt_field
  {
    /* A ps_fmt_spec_t describing how to output this field's value, or NULL
       if there is no value (in which case this is the last field, and exists
       just to output its prefix string).  */
    ps_fmt_spec_t spec;

    /* A non-zero-terminated string of characters that should be output
       between the previous field and this one.  */
    char *pfx;
    /* The number of characters from PFX that should be output.  */
    unsigned pfx_len;

    /* Returns the number of characters that the value portion of this field
       should consume.  If this field is negative, then the absolute value is
       used, and the field should be right-aligned, otherwise, it is
       left-aligned.  */
    int width;

    /* Returns the title used when printing a header line for this field.  */
    char *title;
  };

/* Accessor macros: */
#define ps_fmt_field_fmt_spec(field) ((field)->spec)
#define ps_fmt_field_prefix(field) ((field)->pfx)
#define ps_fmt_field_prefix_length(field) ((field)->pfx_len)
#define ps_fmt_field_width(field) ((field)->width)
#define ps_fmt_field_title(field) ((field)->title)

/* PS_FMT_T */
typedef struct ps_fmt *ps_fmt_t;
struct ps_fmt
  {
    /* A pointer to an array of struct ps_fmt_field's holding the individual
       fields to be formatted.  */
    ps_fmt_field_t fields;
    /* The (valid) length of the fields array.  */
    unsigned num_fields;

    /* A set of proc_stat flags describing what a proc_stat_t needs to hold in 
       order to print out every field in the fmt.  */
    ps_flags_t needs;

    /* Storage for various strings pointed to by the fields.  */
    char *src;
  };

/* Accessor macros: */
#define ps_fmt_fields(fmt) ((fmt)->fields)
#define ps_fmt_num_fields(fmt) ((fmt)->num_fields)
#define ps_fmt_needs(fmt) ((fmt)->needs)

/*
   Make a PS_FMT_T by parsing the string SRC, searching for any named
   field specs in FMT_SPECS, and returning the result in FMT.  If a memory
   allocation error occurs, ENOMEM is returned.  If SRC contains an unknown
   field name, EINVAL is returned.  Otherwise 0 is returned.

   The syntax of SRC is:
   SRC:  FIELD* [ SUFFIX ]
   FIELD: [ PREFIX ] SPEC
   SPEC: `~' [ `-' ] [ WIDTH ] [ `/' ] NAME [ `/' ]
   WIDTH: `[0-9]+'
   NAME: `[^/]*'

   PREFIXes and SUFFIXes are printed verbatim, and specs are replaced by the
   output of the named spec with that name (each spec specifies what
   proc_stat_t field to print, and how to print it, as well as a default
   field width into which put the output).  WIDTH is used to override the
   spec's default width.  If a `-' is included, the output is right-aligned
   within this width, otherwise it is left-aligned.
 */
error_t ps_fmt_create(char *src, ps_fmt_spec_t fmt_specs, ps_fmt_t *fmt);

/* Free FMT, and any resources it consumes.  */
void ps_fmt_free(ps_fmt_t fmt);

/* Write an appropiate header line for FMT, containing the titles of all its
   fields appropiately aligned with where the values would be printed, to
   STREAM (without a trailing newline).  If count is non-NULL, the total
   number number of characters output is added to the integer it points to.
   If any fatal error occurs, the error code is returned, otherwise 0.  */
error_t ps_fmt_write_titles(ps_fmt_t fmt, FILE *stream, unsigned *count);

/* Format a description as instructed by FMT, of the process described by PS
   to STREAM (without a trailing newline).  If count is non-NULL, the total
   number number of characters output is added to the integer it points to.
   If any fatal error occurs, the error code is returned, otherwise 0.  */
error_t ps_fmt_write_proc_stat(ps_fmt_t fmt,
			       proc_stat_t ps, FILE *stream, unsigned *count);

/* Remove those fields from FMT which would need the proc_stat flags FLAGS.
   Appropiate inter-field characters are also removed: those *following*
   deleted fields at the beginning of the fmt, and those *preceeding* deleted
   fields *not* at the beginning.  */
void ps_fmt_squash(ps_fmt_t fmt, ps_flags_t flags);

/* ---------------------------------------------------------------- */
/* A PROC_STAT_LIST_T represents a list of proc_stat_t's */

typedef struct proc_stat_list *proc_stat_list_t;

struct proc_stat_list
  {
    /* An array of proc_stat_t's for the processes in this list.  */
    proc_stat_t *proc_stats;

    /* The number of processes in the list.  */
    unsigned num_procs;

    /* The actual allocated length of PROC_STATS (in case we want to add more
       processes).  */
    unsigned alloced;

    /* Returns the proc context that these processes are from.  */
    ps_context_t context;
  };

/* Accessor macros: */
#define proc_stat_list_num_procs(pp) ((pp)->num_procs)
#define proc_stat_list_context(pp) ((pp)->context)

/* Creates a new proc_stat_list_t for processes from CONTEXT, which is
   returned in PP, and returns 0, or else returns ENOMEM if there wasn't
   enough memory.  */
error_t proc_stat_list_create(ps_context_t context, proc_stat_list_t *pp);

/* Free PP, and any resources it consumes.  */
void proc_stat_list_free(proc_stat_list_t pp);

/* Returns the proc_stat_t in PP with a process-id of PID, if there's one,
   otherwise, NULL.  */
proc_stat_t proc_stat_list_pid_proc_stat(proc_stat_list_t pp, pid_t pid);

/* Add proc_stat_t entries to PP for each process with a process id in the
   array PIDS (where NUM_PROCS is the length of PIDS).  Entries are only
   added for processes not already in PP.  ENOMEM is returned if a memory
   allocation error occurs, otherwise 0.  PIDs is not referenced by the
   resulting proc_stat_list_t, and so may be subsequently freed.  */
error_t proc_stat_list_add_pids(proc_stat_list_t pp, pid_t *pids, unsigned num_procs);

/* Add a proc_stat_t for the process designated by PID at PP's proc context to
   PP.  If PID already has an entry in PP, nothing is done.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t proc_stat_list_add_pid(proc_stat_list_t pp, pid_t pid);

/* Adds all proc_stat_t's in MERGEE to PP that don't correspond to processes
   already in PP; the resulting order of proc_stat_t's in PP is undefined.
   If MERGEE and PP point to different proc contexts, EINVAL is returned.  If a
   memory allocation error occurs, ENOMEM is returned.  Otherwise 0 is
   returned, and MERGEE is freed.  */
error_t proc_stat_list_merge(proc_stat_list_t pp, proc_stat_list_t mergee);

/* Add to PP entries for all processes at its context.  If an error occurs,
   the system error code is returned, otherwise 0.  */
error_t proc_stat_list_add_all(proc_stat_list_t pp);

/* Add to PP entries for all processes in the login collection LOGIN_ID at
   its context.  If an error occurs, the system error code is returned,
   otherwise 0.  */
error_t proc_stat_list_add_login_coll(proc_stat_list_t pp, pid_t login_id);

/* Add to PP entries for all processes in the session SESSION_ID at its
   context.  If an error occurs, the system error code is returned, otherwise
   0.  */
error_t proc_stat_list_add_session(proc_stat_list_t pp, pid_t session_id);

/* Add to PP entries for all processes in the process group PGRP at
   its context.  If an error occurs, the system error code is returned,
   otherwise 0.  */
error_t proc_stat_list_add_pgrp(proc_stat_list_t pp, pid_t pgrp);

/* Try to set FLAGS in each proc_stat_t in PP (but they may still not be set
   -- you have to check).  If a fatal error occurs, the error code is
   returned, otherwise 0.  */
error_t proc_stat_list_set_flags(proc_stat_list_t pp, ps_flags_t flags);

/* Destructively modify PP to only include proc_stat_t's for which the
   function PREDICATE returns true; if INVERT is true, only proc_stat_t's for
   which PREDICATE returns false are kept.  FLAGS is the set of pstat_flags
   that PREDICATE requires be set as precondition.  Regardless of the value
   of INVERT, all proc_stat_t's for which the predicate's preconditions can't
   be satisfied are kept.  If a fatal error occurs, the error code is
   returned, it returns 0.  */
error_t proc_stat_list_filter1(proc_stat_list_t pp,
			       bool (*predicate)(proc_stat_t ps), ps_flags_t flags,
			       bool invert);

/* Destructively modify PP to only include proc_stat_t's for which the
   predicate function in FILTER returns true; if INVERT is true, only
   proc_stat_t's for which the predicate returns false are kept.  Regardless
   of the value of INVERT, all proc_stat_t's for which the predicate's
   preconditions can't be satisfied are kept.  If a fatal error occurs,
   the error code is returned, it returns 0.  */
error_t proc_stat_list_filter(proc_stat_list_t pp,
			      ps_filter_t filter, bool invert);

/* Destructively sort proc_stats in PP by ascending value of the field
   returned by GETTER, and compared by CMP_FN; If REVERSE is true, use the
   opposite order.  If a fatal error occurs, the error code is returned, it
   returns 0.  */
error_t proc_stat_list_sort1(proc_stat_list_t pp,
			     ps_getter_t getter,
			     int (*cmp_fn)(proc_stat_t ps1, proc_stat_t ps2,
					   ps_getter_t getter),
			     bool reverse);

/* Destructively sort proc_stats in PP by ascending value of the field KEY;
   if REVERSE is true, use the opposite order.  If KEY isn't a valid sort
   key, EINVAL is returned.  If a fatal error occurs the error code is
   returned.  Otherwise, 0 is returned.  */
error_t proc_stat_list_sort(proc_stat_list_t pp,
			    ps_fmt_spec_t key, bool reverse);

/* Format a description as instructed by FMT, of the processes in PP to
   STREAM, separated by newlines (and with a terminating newline).  If COUNT
   is non-NULL, it points to an integer which is incremented by the number of
   characters output.  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t proc_stat_list_fmt(proc_stat_list_t pp,
			   ps_fmt_t fmt, FILE *stream, unsigned *count);

/* Modifies FLAGS to be the subset which can't be set in any proc_stat_t in
   PP (and as a side-effect, adds as many bits from FLAGS to each proc_stat_t
   as possible).  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t proc_stat_list_find_bogus_flags(proc_stat_list_t pp, ps_flags_t *flags);

/* Add thread entries for for every process in PP, located immediately after
   the containing process in sequence.  Subsequent sorting of PP will leave
   the thread entries located after the containing process, although the
   order of the thread entries themselves may change.  If a fatal error
   occurs, the error code is returned, otherwise 0.  */
error_t proc_stat_list_add_threads(proc_stat_list_t pp);

error_t proc_stat_list_remove_threads(proc_stat_list_t pp);


/* ---------------------------------------------------------------- */
/*
   The Basic & Sched info types are pretty static, so we cache them, but load
   info is dynamic so we don't cache that.

   See <mach/host_info.h> for information on the data types these routines
   return.
*/

/* Return the current host port.  */
host_t ps_get_host();

/* Return a pointer to basic info about the current host in HOST_INFO.  Since
   this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_basic_info(host_basic_info_t *host_info);

/* Return a pointer to scheduling info about the current host in HOST_INFO.
   Since this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_sched_info(host_sched_info_t *host_info);

/* Return a pointer to load info about the current host in HOST_INFO.  Since
   this is global information we just use a static buffer (if someone desires
   to keep old load info, they should copy the buffer we return a pointer
   to).  If a system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_load_info(host_load_info_t *host_info);

/* ---------------------------------------------------------------- */

/* Write at most MAX_LEN characters of STRING to STREAM (if MAX_LEN > the
   length of STRING, then write all of it; if MAX_LEN == -1, then write all
   of STRING regardless).  If COUNT is non-NULL, the number of characters
   written is added to the integer it points to.  If an error occurs, the
   error code is returned, otherwise 0.  */
error_t ps_write_string(char *string, int max_len, FILE *stream, unsigned *count);

/* Write NUM spaces to STREAM.  If COUNT is non-NULL, the number of spaces
   written is added to the integer it points to.  If an error occurs, the
   error code is returned, otherwise 0.  */
error_t ps_write_spaces(int num, FILE *stream, unsigned *count);

/* Write as many spaces to STREAM as required to make a field of width SOFAR
   be at least WIDTH characters wide (the absolute value of WIDTH is used).
   If COUNT is non-NULL, the number of spaces written is added to the integer
   it points to.  If an error occurs, the error code is returned, otherwise
   0.  */
error_t ps_write_padding(int sofar, int width, FILE *stream, unsigned *count);

/* Write the string BUF to STREAM, padded on one side with spaces to be at
   least the absolute value of WIDTH long: if WIDTH >= 0, then on the left
   side, otherwise on the right side.  If COUNT is non-NULL, the number of
   characters written is added to the integer it points to.  If an error
   occurs, the error code is returned, otherwise 0.  */
error_t ps_write_field(char *buf, int width, FILE *stream, unsigned *count);

/* Write the decimal representation of VALUE to STREAM, padded on one side
   with spaces to be at least the absolute value of WIDTH long: if WIDTH >=
   0, then on the left side, otherwise on the right side.  If COUNT is
   non-NULL, the number of characters written is added to the integer it
   points to.  If an error occurs, the error code is returned, otherwise 0.  */
error_t ps_write_int_field(int value, int width, FILE *stream, unsigned *count);


#endif /* __PS_H__ */
