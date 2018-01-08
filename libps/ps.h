/* Routines to gather and print process information.

   Copyright (C) 1995,96,99,2001,02 Free Software Foundation, Inc.

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

#ifndef __PS_H__
#define __PS_H__

#include <hurd/hurd_types.h>
#include <hurd/ihash.h>
#include <mach/mach.h>

#include <pwd.h>
#include <errno.h>

/* A PS_USER holds info about a particular user.  */

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

/* Create a ps_user for the user referred to by UID, returning it in U.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t ps_user_create (uid_t uid, struct ps_user **u);

/* Create a ps_user for the user referred to by UNAME, returning it in U.
   If a memory allocation error occurs, ENOMEM is returned.  If no such user
   is known, EINVAL is returned.  */
error_t ps_user_uname_create (char *uname, struct ps_user **u);

/* Makes makes a ps_user containing PW (which is copied).  */
error_t ps_user_passwd_create (struct passwd *pw, struct ps_user **u);

/* Free U and any resources it consumes.  */
void ps_user_free (struct ps_user *u);

/* Returns the password file entry (struct passwd, from <pwd.h>) for the user
   referred to by U, or NULL if it can't be gotten.  */
struct passwd *ps_user_passwd (struct ps_user *u);

/* Returns the user name for the user referred to by U, or NULL if it can't
   be gotten.  */
char *ps_user_name (struct ps_user *u);

/* A ps_tty holds information about a terminal.  */

/* Possible states a ps_tty's name can be in: valid, not fetched yet,
   couldn't fetch.  */
enum ps_tty_name_state
  { PS_TTY_NAME_OK, PS_TTY_NAME_PENDING, PS_TTY_NAME_ERROR };

struct ps_tty {
  /* Which tty this refers to.  */
  file_t port;

  /* The name of the tty, if we could figure it out.  */
  const char *name;
  /* What state the name is in.  */
  enum ps_tty_name_state name_state;

  /* A more abbreviated name for the tty, or NULL if no name at all.  */
  const char *short_name;
  int short_name_alloced : 1;
};

#define ps_tty_port(tty) ((tty)->port)

/* Create a ps_tty for the tty referred to by PORT, returning it in TTY.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t ps_tty_create (file_t port, struct ps_tty **tty);

/* Frees TTY and any resources it consumes.  */
void ps_tty_free (struct ps_tty *tty);

/* Returns the name of the tty, or NULL if it can't be figured out.  */
const char *ps_tty_name (struct ps_tty *tty);

/* Returns the standard abbreviated name of the tty, the whole name if there
   is no standard abbreviation, or NULL if it can't be figured out.  */
const char *ps_tty_short_name (struct ps_tty *tty);

/* A PS_CONTEXT holds various information resulting from querying a
   particular process server, in particular a group of proc_stats, ps_users,
   and ps_ttys.  This information sticks around until the context is freed
   (subsets may be created by making proc_stat_lists).  */

struct proc_stat;		/* Fwd declared */

struct ps_context
{
  /* The process server our process info is from.  */
  process_t server;

  /* proc_stats for every process we know about, indexed by process id.  */
  struct hurd_ihash procs;

  /* ps_ttys for every tty we know about, indexed by the terminal port.  */
  struct hurd_ihash ttys;

  /* ps_ttys for every tty we know about, indexed by their ctty id port
     (from libc).  */
  struct hurd_ihash ttys_by_cttyid;

  /* A ps_user for every user we know about, indexed by user-id.  */
  struct hurd_ihash users;

  /* Functions that can be set to extend the behavior of proc_stats.  */
  struct ps_user_hooks *user_hooks;
};

#define ps_context_server(pc) ((pc)->server)

/* Returns in PC a new ps_context for the proc server SERVER.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t ps_context_create (process_t server, struct ps_context **pc);

/* Frees PC and any resources it consumes.  */
void ps_context_free (struct ps_context *pc);

/* Find a proc_stat for the process referred to by PID, and return it in
   PS.  If an error occurs, it is returned, otherwise 0.  */
error_t ps_context_find_proc_stat (struct ps_context *pc, pid_t pid,
				   struct proc_stat **ps);

/* Find a ps_tty for the terminal referred to by the port TTY_PORT, and
   return it in TTY.  If an error occurs, it is returned, otherwise 0.  */
error_t ps_context_find_tty (struct ps_context *pc, mach_port_t tty_port,
			     struct ps_tty **tty);

/* Find a ps_tty for the terminal referred to by the ctty id port
   CTTYID_PORT, and return it in TTY.  If an error occurs, it is returned,
   otherwise 0.  */
error_t ps_context_find_tty_by_cttyid (struct ps_context *pc,
				       mach_port_t cttyid_port,
				       struct ps_tty **tty);

/* Find a ps_user for the user referred to by UID, and return it in U.  */
error_t ps_context_find_user (struct ps_context *pc, uid_t uid,
			      struct ps_user **u);

/* A PROC_STAT holds lots of info about the process PID at SERVER; exactly
   which info is dependent on its FLAGS field.  */

typedef unsigned ps_flags_t;
typedef unsigned ps_state_t;

struct proc_stat
{
  /* Which process server this is from.  */
  struct ps_context *context;

  /* The proc's process id; if <0 then this is a thread, not a process.  */
  pid_t pid;

  /* Flags describing which fields in this structure are valid.  */
  ps_flags_t flags;
  ps_flags_t failed;		/* flags that we tried to set and couldn't.  */
  ps_flags_t inapp;		/* flags that don't apply to this procstat;
				   subset of FAILED.  */

  /* Thread fields -- these are valid if PID < 0.  */
  struct proc_stat *thread_origin; /* A proc_stat for the task we're in.  */
  unsigned thread_index;	/* Which thread in our proc we are.  */

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
     proc_getinfo; see <hurd/hurd_types.h>).  Vm_alloced.  */
  struct procinfo *proc_info;
  /* The size of the info structure for deallocation purposes.  */
  unsigned proc_info_size;

  /* If present, these are just pointers into the proc_info structure.  */
  unsigned num_threads;
  task_basic_info_t task_basic_info;

  /* For a thread, the obvious structures; for a process, summaries of the
     proc's thread_{basic,sched}_info_t structures: sizes and cumulative
     times are summed, prioritys and delta time are averaged.  The
     run_states are added by having running thread take precedence over
     waiting ones, and if there are any other incompatible states, simply
     using a bogus value of -1.  Malloced. */
  thread_basic_info_t thread_basic_info;
  thread_sched_info_t thread_sched_info;

  /* For a blocked thread, these next fields describe how it's blocked.  */

  /* A string (pointing into the thread_waits field of the parent
     procstat), describing what's being blocked on.  If "KERNEL", a system
     call (not mach_msg), and thread_rpc is the system call number.
     Otherwise if thread_rpc isn't zero, this string describes the port the
     rpc is on; if thread_rpc is 0, this string describes a non-rpc event. */
  char *thread_wait;
  /* The rpc that it's blocked on.  For a process the rpc blocking the
     first blocked thread (if any).  0 means no block. */
  mach_msg_id_t thread_rpc;

  /* Storage for child-thread waits.  */
  char *thread_waits;
  size_t thread_waits_len;

  /* The task or thread suspend count (whatever this proc_stat refers to). */
  int suspend_count;

  /* A bitmask summarizing the scheduling state of this process and all its
     threads.  See the PSTAT_STATE_ defines below for a list of bits.  */
  ps_state_t state;

  /* A ps_user object for the owner of this process, or NULL if none.  */
  struct ps_user *owner;
  int owner_uid;		/* The corresponding UID, or -1.  */

  /* The process's argv, as a string with each element separated by '\0'.  */
  char *args;
  /* The length of ARGS.  */
  size_t args_len;

  /* Virtual memory statistics for the process, as returned by task_info;
     see <mach/task_info.h> for a description of task_events_info_t.  */
  /* FIXME: we are actually currently storing it into proc_info, see
     fetch_procinfo.  */
  task_events_info_t task_events_info;
  task_events_info_data_t task_events_info_buf;
  size_t task_events_info_size;

  /* Flags showing whether a field is vm_alloced or malloced.  */
  unsigned proc_info_vm_alloced : 1;
  unsigned thread_waits_vm_alloced : 1;
  unsigned args_vm_alloced : 1;
  unsigned env_vm_alloced : 1;
  unsigned exe_vm_alloced : 1;

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

  /* A ps_tty object for the process's controlling terminal.  */
  struct ps_tty *tty;

  /* A hook for the user to use.  */
  void *hook;

  /* XXX these members added at the end for binary compatibility */
  /* The process's envp, as a string with each element separated by '\0'.  */
  char *env;
  /* The length of ENV.  */
  size_t env_len;

  unsigned num_ports;

  /* The path to process's binary executable.  */
  char *exe;
  /* The length of EXE.  */
  size_t exe_len;
};

/* Proc_stat flag bits; each bit is set in the FLAGS field if that
   information is currently valid.  */
#define PSTAT_PID	       0x00001 /* Process ID */
#define PSTAT_THREAD	       0x00002 /* thread_index & thread_origin */
#define PSTAT_PROCESS	       0x00004 /* The process_t for the process */
#define PSTAT_TASK	       0x00008 /* The task port for the process */
#define PSTAT_MSGPORT	       0x00010 /* The process's msgport */
#define PSTAT_PROC_INFO	       0x00020 /* Basic process info. */
#define PSTAT_TASK_BASIC       0x00040 /* The task's struct task_basic_info. */
#define PSTAT_TASK_EVENTS      0x00080 /* A task_events_info_t for the proc. */
#define PSTAT_NUM_THREADS      0x00100 /* The number of threads in the task. */
/* Note that for a process-procstat, the thread information fields generally
   are a summary of the process's threads, and imply that the corresponding
   information has been fetched for all its threads.  The exception is
   thread-wait information (PSTAT_THREAD_WAIT), which is expensive to fetch
   for processes with lots of threads, and not terrible useful.  In this
   case, the thread waits vector containing per-thread information is only
   guaranteed to be valid if PSTAT_THREAD_WAITS is true as well.  */
#define PSTAT_THREAD_BASIC     0x00200 /* A struct thread_basic_info. */
#define PSTAT_THREAD_SCHED     0x00400 /* A struct thread_sched_info. */
#define PSTAT_THREAD_WAIT      0x00800 /* The rpc the thread is waiting on. */
#define PSTAT_THREAD_WAITS     0x01000 /* Thread waits for this PS's threads */
#define PSTAT_ARGS	       0x02000 /* The process's args */
#define PSTAT_ENV	     0x2000000 /* The process's environment */
#define PSTAT_STATE	       0x04000 /* A bitmask describing the process's
					  state (see below) */
#define PSTAT_SUSPEND_COUNT    0x08000 /* Task/thread suspend count */
#define PSTAT_CTTYID	       0x10000 /* The process's CTTYID port */
#define PSTAT_CWDIR	       0x20000 /* A file_t for the proc's CWD */
#define PSTAT_AUTH	       0x40000 /* The proc's auth port */
#define PSTAT_TTY	       0x80000 /* A ps_tty for the proc's terminal.*/
#define PSTAT_OWNER	      0x100000 /* A ps_user for the proc's owner */
#define PSTAT_OWNER_UID	      0x200000 /* The uid of the the proc's owner */
#define PSTAT_UMASK	      0x400000 /* The proc's current umask */
#define PSTAT_HOOK	      0x800000 /* Has a non-zero hook */
#define PSTAT_NUM_PORTS      0x4000000 /* Number of Mach ports in the task */
#define PSTAT_TIMES          0x8000000 /* Task/thread user and system times */
#define PSTAT_EXE           0x10000000 /* Path to binary executable */

/* Flag bits that don't correspond precisely to any field.  */
#define PSTAT_NO_MSGPORT     0x1000000 /* Don't use the msgport at all */

/* Bits from PSTAT_USER_BASE on up are available for user-use.  */
#define PSTAT_USER_BASE      0x20000000
#define PSTAT_USER_MASK      ~(PSTAT_USER_BASE - 1)

/* If the PSTAT_STATE flag is set, then the proc_stats state field holds a
   bitmask of the following bits, describing the process's run state.  If you
   change the value of these, you must change proc_stat_state_tags as well!  */

/* Process global state.  */

/* Mutually exclusive bits, each of which is a possible process `state'.  */
#define PSTAT_STATE_P_STOP	0x00001 /* T stopped (e.g., by ^Z) */
#define PSTAT_STATE_P_ZOMBIE	0x00002 /* Z process exited but not reaped */

#define PSTAT_STATE_P_STATES	(PSTAT_STATE_P_STOP | PSTAT_STATE_P_ZOMBIE)

/* Independent bits describing additional attributes of the process.  */
#define PSTAT_STATE_P_FG	0x00400 /* + in foreground process group */
#define PSTAT_STATE_P_SESSLDR	0x00800 /* s session leader */
#define PSTAT_STATE_P_LOGINLDR	0x01000 /* l login collection leader */
#define PSTAT_STATE_P_FORKED	0x02000 /* f has forked and not execed */
#define PSTAT_STATE_P_NOMSG	0x04000 /* m no msg port */
#define PSTAT_STATE_P_NOPARENT	0x08000 /* p no parent */
#define PSTAT_STATE_P_ORPHAN	0x10000 /* o orphaned */
#define PSTAT_STATE_P_TRACE     0x20000 /* x traced */
#define PSTAT_STATE_P_WAIT	0x40000 /* w process waiting for a child */
#define PSTAT_STATE_P_GETMSG	0x80000 /* g waiting for a msgport */

#define PSTAT_STATE_P_ATTRS  (PSTAT_STATE_P_FG | PSTAT_STATE_P_SESSLDR \
			      | PSTAT_STATE_P_LOGINLDR | PSTAT_STATE_P_FORKED \
			      | PSTAT_STATE_P_NOMSG | PSTAT_STATE_P_NOPARENT \
			      | PSTAT_STATE_P_ORPHAN | PSTAT_STATE_P_TRACE \
			      | PSTAT_STATE_P_WAIT | PSTAT_STATE_P_GETMSG)

/* Per-thread state; in a process, these represent the union of its threads. */

/* Mutually exclusive bits, each of which is a possible thread `state'.  */
#define PSTAT_STATE_T_RUN	0x00004 /* R thread is running */
#define PSTAT_STATE_T_HALT	0x00008 /* H thread is halted */
#define PSTAT_STATE_T_WAIT	0x00010 /* D uninterruptable wait */
#define PSTAT_STATE_T_SLEEP	0x00020 /* S sleeping */
#define PSTAT_STATE_T_IDLE	0x00040 /* I idle (sleeping > 20 seconds) */

#define PSTAT_STATE_T_STATES	(PSTAT_STATE_T_RUN | PSTAT_STATE_T_HALT \
				 | PSTAT_STATE_T_WAIT | PSTAT_STATE_T_SLEEP \
				 | PSTAT_STATE_T_IDLE)

/* Independent bits describing additional attributes of the thread.  */
#define PSTAT_STATE_T_NICE	0x00080 /* N lowered priority */
#define PSTAT_STATE_T_NASTY     0x00100 /* < raised priority */
#define PSTAT_STATE_T_UNCLEAN	0x00200 /* u thread is uncleanly halted */

#define PSTAT_STATE_T_ATTRS	(PSTAT_STATE_T_UNCLEAN \
				 | PSTAT_STATE_T_NICE | PSTAT_STATE_T_NASTY)

/* This is a constant string holding a single character for each possible bit
   in a proc_stats STATE field, in order from bit zero.  These are intended
   for printing a user-readable summary of a process's state. */
extern char *proc_stat_state_tags;

/* Process info accessor functions.

   You must be sure that the associated flag bit is set before accessing a
   field in a proc_stat!  A field FOO (with accessor macro proc_foo ()), has
   a flag named PSTAT_FOO.  If the flag is'nt set, you may attempt to set it
   with proc_stat_set_flags (but note that this may not succeed).  */

/* FLAGS doesn't have a flag bit; it's always valid */
#define proc_stat_flags(ps) ((ps)->flags)

/* These both use the flag PSTAT_THREAD.  */
#define proc_stat_thread_origin(ps) ((ps)->thread_origin)
#define proc_stat_thread_index(ps) ((ps)->thread_index)

#define proc_stat_pid(ps) ((ps)->pid)
#define proc_stat_process(ps) ((ps)->process)
#define proc_stat_task(ps) ((ps)->task)
#define proc_stat_msgport(ps) ((ps)->msgport)
#define proc_stat_proc_info(ps) ((ps)->proc_info)
#define proc_stat_num_threads(ps) ((ps)->num_threads)
#define proc_stat_task_basic_info(ps) ((ps)->task_basic_info)
#define proc_stat_thread_basic_info(ps) ((ps)->thread_basic_info)
#define proc_stat_thread_sched_info(ps) ((ps)->thread_sched_info)
#define proc_stat_thread_rpc(ps) ((ps)->thread_rpc)
#define proc_stat_thread_wait(ps) ((ps)->thread_rpc)
#define proc_stat_suspend_count(ps) ((ps)->suspend_count)
#define proc_stat_args(ps) ((ps)->args)
#define proc_stat_args_len(ps) ((ps)->args_len)
#define proc_stat_env(ps) ((ps)->env)
#define proc_stat_env_len(ps) ((ps)->env_len)
#define proc_stat_state(ps) ((ps)->state)
#define proc_stat_cttyid(ps) ((ps)->cttyid)
#define proc_stat_cwdir(ps) ((ps)->cwdir)
#define proc_stat_owner(ps) ((ps)->owner)
#define proc_stat_owner_uid(ps) ((ps)->owner_uid)
#define proc_stat_auth(ps) ((ps)->auth)
#define proc_stat_umask(ps) ((ps)->umask)
#define proc_stat_tty(ps) ((ps)->tty)
#define proc_stat_task_events_info(ps) ((ps)->task_events_info)
#define proc_stat_num_ports(ps) ((ps)->num_ports)
#define proc_stat_exe(ps) ((ps)->exe)
#define proc_stat_exe_len(ps) ((ps)->exe_len)
#define proc_stat_has(ps, needs) (((ps)->flags & needs) == needs)

/* True if PS refers to a thread and not a process.  */
#define proc_stat_is_thread(ps) ((ps)->pid < 0)

/* Returns in PS a new proc_stat for the process PID in the ps context PC.
   If a memory allocation error occurs, ENOMEM is returned, otherwise 0.
   Users shouldn't use this routine, use ps_context_find_proc_stat instead.  */
error_t _proc_stat_create (pid_t pid, struct ps_context *context,
			   struct proc_stat **ps);

/* Frees PS and any memory/ports it references.  Users shouldn't use this
   routine; proc_stats are normally freed only when their ps_context goes
   away.  Insubordinate users will make sure they free the thread proc_stats
   before they free the corresponding process proc_stat since the thread_wait
   fields of the former may reference the latter.  */
void _proc_stat_free (struct proc_stat *ps);

/* Adds FLAGS to PS's flags, fetching information as necessary to validate
   the corresponding fields in PS.  Afterwards you must still check the flags
   field before using new fields, as something might have failed.  Returns
   a system error code if a fatal error occurred, and 0 otherwise.  */
error_t proc_stat_set_flags (struct proc_stat *ps, ps_flags_t flags);

/* Returns in THREAD_PS a proc_stat for the Nth thread in the proc_stat
   PS (N should be between 0 and the number of threads in the process).  The
   resulting proc_stat isn't fully functional -- most flags can't be set in
   it.  If N was out of range, EINVAL is returned.  If a memory allocation
   error occurred, ENOMEM is returned.  Otherwise, 0 is returned.  */
error_t proc_stat_thread_create (struct proc_stat *ps, unsigned n,
				 struct proc_stat **thread_ps);

/* A struct ps_user_hooks holds functions that allow the user to extend the
   behavior of libps.  */

struct ps_user_hooks
{
  /* Given a set of flags in the range defined by PSTAT_USER_MASK, should
     return any other flags (user or system) which should be set as a
     precondition to setting them.  */
  ps_flags_t (*dependencies) (ps_flags_t flags);

  /* Try and fetch the information corresponding to NEED (which is in the
     range defined by PSTAT_USER_MASK), and fill in the necessary fields in
     PS (probably in a user defined structure pointed to by the hook field).
     The user flags corresponding to what is successfully fetched should be
     returned.  HAVE are the flags defining whas is currently valid in PS. */
  ps_flags_t (*fetch) (struct proc_stat *ps, ps_flags_t need, ps_flags_t have);

  /* When a proc_stat goes away, this function is called on it.  */
  void (*cleanup) (struct proc_stat *ps);
};

/* A PS_GETTER describes how to get a particular value from a PROC_STAT.

   To get a value from a proc_stat PS with a getter, you must make sure all
   the pstat_flags returned by ps_getter_needs (GETTER) are set in PS, and
   then call the function returned ps_getter_function (GETTER) with PS as the
   first argument.

   The way the actual value is returned from this funciton is dependent on
   the type of the value:
      For ints and floats, the value is the return value.
      For strings, you must pass in two extra arguments, a char **, which is
        filled in with a pointer to the string, or NULL if the string is NULL,
        and an int *, which is filled in with the length of the string.  */

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

/* A PS_FILTER_T describes how to select some subset of a PROC_STAT_LIST_T */

struct ps_filter
  {
    /* Name of this filter.  */
    char *name;

    /* The flags that need to be set in each proc_stat in the list to
       call the filter's predicate function; if these flags can't be set in a
       particular proc_stat, the function is not called, and it isn't deleted
       from the list.  */
    ps_flags_t needs;

    /* A function that returns true if called on a proc_stat that the
       filter accepts, or false if the filter rejects it.  */
    int (*fn) (struct proc_stat *ps);
  };

/* Access macros: */
#define ps_filter_name(f) ((f)->name)
#define ps_filter_needs(f) ((f)->needs)
#define ps_filter_predicate(f) ((f)->fn)

/* Some predefined filters.  These are structures; you must use the &
   operator to get a ps_filter from them */

/* A filter that retains only processes owned by getuid () */
extern const struct ps_filter ps_own_filter;
/* A filter that retains only processes that aren't session or login leaders */
extern const struct ps_filter ps_not_leader_filter;
/* A filter that retains only processes with a controlling terminal */
extern const struct ps_filter ps_ctty_filter;
/* A filter that retains only `unorphaned' process.  A process is unorphaned
   if it's a session leader, or the process's process group is not orphaned */
extern const struct ps_filter ps_unorphaned_filter;
/* A filter that retains only `parented' process.  Typically only hurd
   processes have parents.  */
extern const struct ps_filter ps_parent_filter;
/* A filter that retains only processes/threads that aren't totally dead.  */
extern const struct ps_filter ps_alive_filter;

/* A ps_stream describes an output stream for libps to use.  */

struct ps_stream
{
  FILE *stream;			/* The actual destination.  */
  int pos;			/* The number of characters output.  */
  int spaces;			/* The number of spaces pending.  */
};

/* Create a stream outputing to DEST, and return it in STREAM, or an error.  */
error_t ps_stream_create (FILE *dest, struct ps_stream **stream);

/* Frees STREAM.  The destination file is *not* closed.  */
void ps_stream_free (struct ps_stream *stream);

/* Write at most MAX_LEN characters of STRING to STREAM (if MAX_LEN > the
   length of STRING, then write all of it; if MAX_LEN == -1, then write all
   of STRING regardless).  */
error_t ps_stream_write (struct ps_stream *stream,
			 const char *string, int max_len);

/* Write NUM spaces to STREAM.  NUM may be negative, in which case the same
   number of adjacent spaces (written by other calls to ps_stream_space) are
   consumed if possible.  If an error occurs, the error code is returned,
   otherwise 0.  */
error_t ps_stream_space (struct ps_stream *stream, int num);

/* Write as many spaces to STREAM as required to make a field of width SOFAR
   be at least WIDTH characters wide (the absolute value of WIDTH is used).
   If an error occurs, the error code is returned, otherwise 0.  */
error_t ps_stream_pad (struct ps_stream *stream, int sofar, int width);

/* Write a newline to STREAM, resetting its position to zero.  */
error_t ps_stream_newline (struct ps_stream *stream);

/* Write the string BUF to STREAM, padded on one side with spaces to be at
   least the absolute value of WIDTH long: if WIDTH >= 0, then on the left
   side, otherwise on the right side.  If an error occurs, the error code is
   returned, otherwise 0.  */
error_t ps_stream_write_field (struct ps_stream *stream,
			       const char *buf, int width);

/* Like ps_stream_write_field, but truncates BUF to make it fit into WIDTH.  */
error_t ps_stream_write_trunc_field (struct ps_stream *stream,
				     const char *buf, int width);

/* Write the decimal representation of VALUE to STREAM, padded on one side
   with spaces to be at least the absolute value of WIDTH long: if WIDTH >=
   0, then on the left side, otherwise on the right side.  If an error
   occurs, the error code is returned, otherwise 0.  */
error_t ps_stream_write_int_field (struct ps_stream *stream,
				   int value, int width);

/* A PS_FMT_SPEC describes how to output something from a PROC_STAT; it
   is a combination of a getter (describing how to get the value), an output
   function (which outputs the result of the getter), and a compare function
   (which can be used to sort proc_stats according to how they are
   output).  It also specifies the default width of the field in which the
   output should be printed. */

struct ps_fmt_field;		/* fwd decl */

struct ps_fmt_spec
  {
    /* The name of the spec (and it's title, if TITLE is NULL).  */
    const char *name;

    /* The title to be printed in the headers.  */
    const char *title;

    /* The width of the field that this spec will be printed in if not
       overridden.  */
    int width;

    /* A default value for the fields `precision'.  */
    int precision;

    /* Default values for PS_FMT_FIELD_ flags.  */
    int flags;

    const struct ps_getter *getter;

    /* A function that outputs what FIELD specifies in PS to STREAM.  */
    error_t (*output_fn)(struct proc_stat *ps, struct ps_fmt_field *field,
			 struct ps_stream *stream);

    /* A function that, given two pses and a getter, will compare what
       the getter gets for each ps, and return an integer ala qsort.  This
       may be NULL, in which case values in this field cannot be compared.  */
    int (*cmp_fn)(struct proc_stat *ps1, struct proc_stat *ps2,
		  const struct ps_getter *getter);

    /* A function that, given a ps and a getter, will return true if what the
       getter gets from the ps is `nominal' -- a default unexciting value.
       This may be NULL, in which case values in this field are _always_
       exciting...  */
    int (*nominal_fn)(struct proc_stat *ps, const struct ps_getter *getter);
  };

/* Accessor macros:  */
#define ps_fmt_spec_name(spec) ((spec)->name)
#define ps_fmt_spec_title(spec) ((spec)->title)
#define ps_fmt_spec_width(spec) ((spec)->width)
#define ps_fmt_spec_output_fn(spec) ((spec)->output_fn)
#define ps_fmt_spec_compare_fn(spec) ((spec)->cmp_fn)
#define ps_fmt_spec_nominal_fn(spec) ((spec)->nominal_fn)
#define ps_fmt_spec_getter(spec) ((spec)->getter)

/* Returns true if a pointer into an array of struct ps_fmt_specs is at  the
   end.  */
#define ps_fmt_spec_is_end(spec) ((spec)->name == NULL)

struct ps_fmt_specs
{
  const struct ps_fmt_spec *specs; /* An array of specs. */
  struct ps_fmt_specs *parent;	/* A link to more specs shadowed by us. */
  struct ps_fmt_spec_block *expansions; /* Storage for expanded aliases.  */
};

/* An struct ps_fmt_specs, suitable for use with ps_fmt_specs_find,
   containing specs for most values in a proc_stat.  */
extern struct ps_fmt_specs ps_std_fmt_specs;

/* Searches for a spec called NAME in SPECS and returns it if found,
   otherwise NULL.  */
const struct ps_fmt_spec *ps_fmt_specs_find (struct ps_fmt_specs *specs,
					     const char *name);

/* A PS_FMT describes how to output user-readable  version of a proc_stat.
   It consists of a series of PS_FMT_FIELD_Ts, each describing how to output
   one value.  */

/* Flags for ps_fmt_fields.  */
#define PS_FMT_FIELD_AT_MOD		0x1 /* `@' modifier */
#define PS_FMT_FIELD_COLON_MOD		0x2 /* `:' modifier */
#define PS_FMT_FIELD_KEEP		0x4 /* Never nominalize this field. */
#define PS_FMT_FIELD_UPCASE_TITLE	0x8 /* Upcase this field's title.  */

/* PS_FMT_FIELD */
struct ps_fmt_field
  {
    /* A ps_fmt_spec describing how to output this field's value, or NULL
       if there is no value (in which case this is the last field, and exists
       just to output its prefix string).  */
    const struct ps_fmt_spec *spec;

    /* A non-zero-terminated string of characters that should be output
       between the previous field and this one.  */
    const char *pfx;
    /* The number of characters from PFX that should be output.  */
    unsigned pfx_len;

    /* The number of characters that the value portion of this field should
       consume.  If this field is negative, then the absolute value is used,
       and the field should be right-aligned, otherwise, it is left-aligned. */
    int width;

    /* User-specifiable attributes, interpreted by each output format. */
    int precision;		/* fraction following field width */

    /* Flags, from the set PS_FMT_FIELD_.  */
    int flags;

    /* Returns the title used when printing a header line for this field.  */
    const char *title;
  };

/* Accessor macros: */
#define ps_fmt_field_fmt_spec(field) ((field)->spec)
#define ps_fmt_field_prefix(field) ((field)->pfx)
#define ps_fmt_field_prefix_length(field) ((field)->pfx_len)
#define ps_fmt_field_width(field) ((field)->width)
#define ps_fmt_field_title(field) ((field)->title)

/* PS_FMT */
struct ps_fmt
{
  /* A pointer to an array of struct ps_fmt_fields holding the individual
     fields to be formatted.  */
  struct ps_fmt_field *fields;
  /* The (valid) length of the fields array.  */
  unsigned num_fields;

  /* A set of proc_stat flags describing what a proc_stat needs to hold in
     order to print out every field in the fmt.  */
  ps_flags_t needs;

  /* Storage for various strings pointed to by the fields.  */
  char *src;
  size_t src_len;		/* Size of SRC.  */

  /* The string displayed by default for fields that aren't appropriate for
     this procstat. */
  char *inapp;

  /* The string displayed by default for fields which are appropriate, but
     couldn't be fetched due to some error.  */
  char *error;
};

/* Accessor macros: */
#define ps_fmt_fields(fmt) ((fmt)->fields)
#define ps_fmt_num_fields(fmt) ((fmt)->num_fields)
#define ps_fmt_needs(fmt) ((fmt)->needs)
#define ps_fmt_inval (fmt) ((fmt)->inval)

/* Make a PS_FMT by parsing the string SRC, searching for any named
   field specs in FMT_SPECS, and returning the result in FMT.  If a memory
   allocation error occurs, ENOMEM is returned.  If SRC contains an unknown
   field name, EINVAL is returned.  Otherwise 0 is returned.

   If POSIX is true, a posix-style format string is parsed, otherwise
   the syntax of SRC is:

   SRC:  FIELD* [ SUFFIX ]
   FIELD: [ PREFIX ] SPEC
   SPEC: `%' [ FLAGS ] [ `-' ] [ WIDTH ] [ `.' PRECISION ] NAMESPEC
   FLAGS: `[!?@:]+'
   WIDTH, PRECISION: `[0-9]+'
   NAMESPEC: `{' NAME [ `:' TITLE ] `}' | NAME_AN
   NAME, TITLE: `[^}]*'
   NAME_AN: `[A-Za-z0-9_]*'

   PREFIXes and SUFFIXes are printed verbatim, and specs are replaced by the
   output of the named spec with that name (each spec specifies what
   proc_stat field to print, and how to print it, as well as a default field
   width into which put the output).  WIDTH is used to override the spec's
   default width.  If a `-' is included, the output is right-aligned within
   this width, otherwise it is left-aligned.  The FLAGS `@' & `:' are
   spec-specific, `!' means never omit a nominal field, and `?' means omit a
   field if it's nominal (in case the defualt is to never do so).  PRECISION
   has a spec-specific meaning.  */
error_t ps_fmt_create (char *src, int posix, struct ps_fmt_specs *fmt_specs,
		       struct ps_fmt **fmt);

/* Given the same arguments as a previous call to ps_fmt_create that returned
   an error, this function returns a malloced string describing the error.  */
void ps_fmt_creation_error (char *src, int posix,
			    struct ps_fmt_specs *fmt_specs,
			    char **error);

/* Free FMT, and any resources it consumes.  */
void ps_fmt_free (struct ps_fmt *fmt);

/* Return a copy of FMT in COPY, or an error.  This is useful if, for
   instance, you would like squash a format without destroying the original.  */
error_t ps_fmt_clone (struct ps_fmt *fmt, struct ps_fmt **copy);

/* Write an appropriate header line for FMT, containing the titles of all its
   fields appropriately aligned with where the values would be printed, to
   STREAM (without a trailing newline).  If count is non-NULL, the total
   number number of characters output is added to the integer it points to.
   If any fatal error occurs, the error code is returned, otherwise 0.  */
error_t ps_fmt_write_titles (struct ps_fmt *fmt, struct ps_stream *stream);

/* Format a description as instructed by FMT, of the process described by PS
   to STREAM (without a trailing newline).  If count is non-NULL, the total
   number number of characters output is added to the integer it points to.
   If any fatal error occurs, the error code is returned, otherwise 0.  */
error_t ps_fmt_write_proc_stat (struct ps_fmt *fmt, struct proc_stat *ps,
				struct ps_stream *stream);

/* Remove those fields from FMT for which the function FN, when called on the
   field, returns true.  Appropriate inter-field characters are also removed:
   those *following* deleted fields at the beginning of the fmt, and those
   *preceding* deleted fields *not* at the beginning. */
void ps_fmt_squash (struct ps_fmt *fmt, int (*fn)(struct ps_fmt_field *field));

/* Remove those fields from FMT which would need the proc_stat flags FLAGS.
   Appropriate inter-field characters are also removed: those *following*
   deleted fields at the beginning of the fmt, and those *preceding* deleted
   fields *not* at the beginning.  */
void ps_fmt_squash_flags (struct ps_fmt *fmt, ps_flags_t flags);

/* Try and restrict the number of output columns in FMT to WIDTH.  */
void ps_fmt_set_output_width (struct ps_fmt *fmt, int width);

/* A PROC_STAT_LIST represents a list of proc_stats */

struct proc_stat_list
  {
    /* An array of proc_stats for the processes in this list.  */
    struct proc_stat **proc_stats;

    /* The number of processes in the list.  */
    unsigned num_procs;

    /* The actual allocated length of PROC_STATS (in case we want to add more
       processes).  */
    unsigned alloced;

    /* Returns the proc context that these processes are from.  */
    struct ps_context *context;
  };

/* Accessor macros: */
#define proc_stat_list_num_procs(pp) ((pp)->num_procs)
#define proc_stat_list_context(pp) ((pp)->context)

/* Creates a new proc_stat_list_t for processes from CONTEXT, which is
   returned in PP, and returns 0, or else returns ENOMEM if there wasn't
   enough memory.  */
error_t proc_stat_list_create (struct ps_context *context,
			       struct proc_stat_list **pp);

/* Free PP, and any resources it consumes.  */
void proc_stat_list_free (struct proc_stat_list *pp);

/* Returns a copy of PP in COPY, or an error.  */
error_t proc_stat_list_clone (struct proc_stat_list *pp,
			      struct proc_stat_list **copy);

/* Returns the proc_stat in PP with a process-id of PID, if there's one,
   otherwise, NULL.  */
struct proc_stat *proc_stat_list_pid_proc_stat (struct proc_stat_list *pp,
						pid_t pid);

/* Add proc_stat entries to PP for each process with a process id in the
   array PIDS (where NUM_PROCS is the length of PIDS).  Entries are only
   added for processes not already in PP.  ENOMEM is returned if a memory
   allocation error occurs, otherwise 0.  PIDs is not referenced by the
   resulting proc_stat_list_t, and so may be subsequently freed.  If
   PROC_STATS is non-NULL, a malloced array NUM_PROCS entries long of the
   resulting proc_stats is returned in it.  */
error_t proc_stat_list_add_pids (struct proc_stat_list *pp,
				 pid_t *pids, unsigned num_procs,
				 struct proc_stat ***proc_stats);

/* Add a proc_stat for the process designated by PID at PP's proc context
   to PP.  If PID already has an entry in PP, nothing is done.  If a memory
   allocation error occurs, ENOMEM is returned, otherwise 0.  If PS is
   non-NULL, the resulting entry is returned in it.  */
error_t proc_stat_list_add_pid (struct proc_stat_list *pp, pid_t pid,
				struct proc_stat **ps);

/* Adds all proc_stats in MERGEE to PP that don't correspond to processes
   already in PP; the resulting order of proc_stats in PP is undefined.
   If MERGEE and PP point to different proc contexts, EINVAL is returned.  If a
   memory allocation error occurs, ENOMEM is returned.  Otherwise 0 is
   returned, and MERGEE is freed.  */
error_t proc_stat_list_merge (struct proc_stat_list *pp,
			      struct proc_stat_list *mergee);

/* Add to PP entries for all processes at its context.  If an error occurs,
   the system error code is returned, otherwise 0.  If PROC_STATS and
   NUM_PROCS are non-NULL, a malloced vector of the resulting entries is
   returned in them.  */
error_t proc_stat_list_add_all (struct proc_stat_list *pp,
				struct proc_stat ***proc_stats,
				size_t *num_procs);

/* Add to PP entries for all processes in the login collection LOGIN_ID at
   its context.  If an error occurs, the system error code is returned,
   otherwise 0.  If PROC_STATS and NUM_PROCS are non-NULL, a malloced vector
   of the resulting entries is returned in them.  */
error_t proc_stat_list_add_login_coll (struct proc_stat_list *pp,
				       pid_t login_id,
				       struct proc_stat ***proc_stats,
				       size_t *num_procs);

/* Add to PP entries for all processes in the session SESSION_ID at its
   context.  If an error occurs, the system error code is returned, otherwise
   0.  If PROC_STATS and NUM_PROCS are non-NULL, a malloced vector of the
   resulting entries is returned in them.  */
error_t proc_stat_list_add_session (struct proc_stat_list *pp,
				    pid_t session_id,
				    struct proc_stat ***proc_stats,
				    size_t *num_procs);

/* Add to PP entries for all processes in the process group PGRP at its
   context.  If an error occurs, the system error code is returned, otherwise
   0.  If PROC_STATS and NUM_PROCS are non-NULL, a malloced vector of the
   resulting entries is returned in them.  */
error_t proc_stat_list_add_pgrp (struct proc_stat_list *pp, pid_t pgrp,
				 struct proc_stat ***proc_stats,
				 size_t *num_procs);

/* Try to set FLAGS in each proc_stat in PP (but they may still not be set
   -- you have to check).  If a fatal error occurs, the error code is
   returned, otherwise 0.  */
error_t proc_stat_list_set_flags (struct proc_stat_list *pp, ps_flags_t flags);

/* Destructively modify PP to only include proc_stats for which the
   function PREDICATE returns true; if INVERT is true, only proc_stats for
   which PREDICATE returns false are kept.  FLAGS is the set of pstat_flags
   that PREDICATE requires be set as precondition.  Regardless of the value
   of INVERT, all proc_stats for which the predicate's preconditions can't
   be satisfied are kept.  If a fatal error occurs, the error code is
   returned, it returns 0.  */
error_t proc_stat_list_filter1 (struct proc_stat_list *pp,
				int (*predicate)(struct proc_stat *ps),
				ps_flags_t flags,
				int invert);

/* Destructively modify PP to only include proc_stats for which the
   predicate function in FILTER returns true; if INVERT is true, only
   proc_stats for which the predicate returns false are kept.  Regardless
   of the value of INVERT, all proc_stats for which the predicate's
   preconditions can't be satisfied are kept.  If a fatal error occurs,
   the error code is returned, it returns 0.  */
error_t proc_stat_list_filter (struct proc_stat_list *pp,
			       const struct ps_filter *filter, int invert);

/* Destructively sort proc_stats in PP by ascending value of the field
   returned by GETTER, and compared by CMP_FN; If REVERSE is true, use the
   opposite order.  If a fatal error occurs, the error code is returned, it
   returns 0.  */
error_t proc_stat_list_sort1 (struct proc_stat_list *pp,
			      const struct ps_getter *getter,
			      int (*cmp_fn)(struct proc_stat *ps1,
					    struct proc_stat *ps2,
					    const struct ps_getter *getter),
			      int reverse);

/* Destructively sort proc_stats in PP by ascending value of the field KEY;
   if REVERSE is true, use the opposite order.  If KEY isn't a valid sort
   key, EINVAL is returned.  If a fatal error occurs the error code is
   returned.  Otherwise, 0 is returned.  */
error_t proc_stat_list_sort (struct proc_stat_list *pp,
			     const struct ps_fmt_spec *key, int reverse);

/* Format a description as instructed by FMT, of the processes in PP to
   STREAM, separated by newlines (and with a terminating newline).  If COUNT
   is non-NULL, it points to an integer which is incremented by the number of
   characters output.  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t proc_stat_list_fmt (struct proc_stat_list *pp, struct ps_fmt *fmt,
			    struct ps_stream *stream);

/* Modifies FLAGS to be the subset which can't be set in any proc_stat in
   PP (and as a side-effect, adds as many bits from FLAGS to each proc_stat
   as possible).  If a fatal error occurs, the error code is returned,
   otherwise 0.  */
error_t proc_stat_list_find_bogus_flags (struct proc_stat_list *pp,
					 ps_flags_t *flags);

/* Add thread entries for for every process in PP, located immediately after
   the containing process in sequence.  Subsequent sorting of PP will leave
   the thread entries located after the containing process, although the
   order of the thread entries themselves may change.  If a fatal error
   occurs, the error code is returned, otherwise 0.  */
error_t proc_stat_list_add_threads (struct proc_stat_list *pp);

error_t proc_stat_list_remove_threads (struct proc_stat_list *pp);

/* Calls FN in order for each proc_stat in PP.  If FN ever returns a non-zero
   value, then the iteration is stopped, and the value is returned
   immediately; otherwise, 0 is returned.  */
int proc_stat_list_for_each (struct proc_stat_list *pp,
			     int (*fn)(struct proc_stat *ps));

/* Returns true if SPEC is `nominal' in every entry in PP.  */
int proc_stat_list_spec_nominal (struct proc_stat_list *pp,
				 const struct ps_fmt_spec *spec);

/* The Basic & Sched info types are pretty static, so we cache them, but load
   info is dynamic so we don't cache that.  See <mach/host_info.h> for
   information on the data types these routines return.  */

/* Return the current host port.  */
mach_port_t ps_get_host ();

/* Return a pointer to basic info about the current host in HOST_INFO.  Since
   this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_basic_info (host_basic_info_t *host_info);

/* Return a pointer to scheduling info about the current host in HOST_INFO.
   Since this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_sched_info (host_sched_info_t *host_info);

/* Return a pointer to load info about the current host in HOST_INFO.  Since
   this is global information we just use a static buffer (if someone desires
   to keep old load info, they should copy the buffer we return a pointer
   to).  If a system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_load_info (host_load_info_t *host_info);

#endif /* __PS_H__ */
