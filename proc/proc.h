/* Process server definitions
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#ifndef PROC_H_INCLUDED
#define PROC_H_INCLUDED


struct proc
{
  mach_port_t p_reqport;

  /* List of members of a process group */
  struct proc *p_gnext, **p_gprevp; /* process group */

  /* Hash table pointers that point here */
  void **p_pidhashloc;		/* by pid */
  void **p_taskhashloc;		/* by task port */
  void **p_porthashloc;		/* by request port */

  /* Identification of this process */
  task_t p_task;
  pid_t p_pid;
  struct login *p_login;
  uid_t p_owner;
  struct ids *p_id;
  
  /* Process hierarchy */
  /* Every process is in the process hierarchy except processes
     0 and 1.  Processes which have not had proc_child called
     on their behalf are parented by 1. */
  struct proc *p_parent;	/* parent process */
  struct proc *p_ochild;	/* youngest child */
  struct proc *p_sib, **p_prevsib; /* next youngest sibling */

  /* Process group structure */
  struct pgrp *p_pgrp;

  /* Communication */
  mach_port_t p_msgport;	/* send right */

  /* Continuations */
  union
    {
      struct wait_c
	{
	  mach_port_t reply_port;
	  mach_msg_type_name_t reply_port_type;
	  pid_t pid;
	  int options;
	} wait_c;
      struct getmsgport_c
	{
	  mach_port_t reply_port;
	  mach_msg_type_name_t reply_port_type;
	  struct proc *msgp;
	} getmsgport_c;
    } p_continuation;
  
  /* Miscellaneous information */
  vm_address_t p_argv, p_envp;
  int p_status;			/* to return via wait */

  int p_exec:1;			/* has called proc_mark_exec */
  int p_stopped:1;		/* has called proc_mark_stop */
  int p_waited:1;		/* stop has been reported to parent */
  int p_exiting:1;		/* has called proc_mark_exit */
  int p_waiting:1;		/* blocked in wait */
  int p_traced:1;		/* has called proc_mark_traced */
  int p_nostopcld:1;		/* has called proc_mark_nostopchild */
  int p_parentset:1;		/* has had a parent set with proc_child */
  int p_deadmsg:1;		/* hang on requests for a message port */
  int p_checkmsghangs:1;	/* someone is currently hanging on us */
  int p_msgportwait:1;		/* blocked in getmsgport */
  int p_noowner:1;		/* has no owner known */
  int p_loginleader:1;		/* leader of login collection */
};

typedef struct proc *pstruct_t;

struct pgrp
{
  void **pg_hashloc;
  struct proc *pg_plist;	/* member processes */
  struct pgrp *pg_next, **pg_prevp; /* list of pgrps in session */
  pid_t pg_pgid;
  struct session *pg_session;
  int pg_orphcnt;		/* number of non-orphaned processes */
};

struct session
{
  void **s_hashloc;
  pid_t s_sid;
  struct pgrp *s_pgrps;		/* list of member pgrps */
  mach_port_t s_sessionid;	/* receive right */
};

struct login
{
  int l_refcnt;
  char l_name[0];
};

struct ids
{
  int i_nuids, i_ngids;
  uid_t *i_uids;
  gid_t *i_gids;
  int i_refcnt;
};

struct exc
{
  mach_port_t excport;
  mach_port_t forwardport;
  int flavor;
  mach_port_t replyport;
  mach_msg_type_name_t replyporttype;
  mach_msg_type_number_t statecnt;
  void **hashloc;
  natural_t thread_state[0];
};

struct zombie 
{
  struct zombie *next;
  pid_t pid, pgrp;
  struct proc *parent;
  int exit_status;
  struct rusage ru;
};

struct zombie *zombie_list;

mach_port_t authserver;
struct proc *self_proc;		/* process 0 (us) */
struct proc *startup_proc;	/* process 1 (init) */

mach_port_t request_portset;

mach_port_t master_host_port;
mach_port_t master_device_port;

mach_port_t generic_port;	/* messages not related to a specific proc */

/* Our name for version system */
#define OUR_SERVER_NAME "proc"
#define OUR_VERSION "0.0 pre-alpha"

/* Forward declarations */
void complete_wait (struct proc *, int);
int nextprime (int);
int check_uid (struct proc *, uid_t);
void addalltasks (void);
void prociterate (void (*)(struct proc *, void *), void *);
void count_up (void *);
void store_pid (void *);
void free_process (struct proc *);
void panic (char *);
int valid_task (task_t);
int genpid ();
void abort_getmsgport (struct proc *);
int zombie_check_pid (pid_t);
void check_message_dying (struct proc *, struct proc *);
void message_port_dead (struct proc *);
void check_dead_execdata_notify (mach_port_t);

void add_proc_to_hash (struct proc *);
void add_exc_to_hash (struct exc *);
void remove_proc_from_hash (struct proc *);
void add_pgrp_to_hash (struct pgrp *);
void add_session_to_hash (struct session *);
void remove_session_from_hash (struct session *);
void remove_pgrp_from_hash (struct pgrp *);
void remove_exc_from_hash (struct exc *);
struct exc *exc_find (mach_port_t);
struct proc *pid_find (int);
struct proc *task_find (task_t);
struct proc *task_find_nocreate (task_t);
struct pgrp *pgrp_find (int);
struct proc *reqport_find (mach_port_t);
struct session *session_find (pid_t);

struct proc *add_tasks (task_t);
int pidfree (pid_t);

struct proc *new_proc (task_t);

void leave_pgrp (struct proc *);
void join_pgrp (struct proc *);
void boot_setsid (struct proc *);

void process_has_exited (struct proc *);
void alert_parent (struct proc *);
void reparent_zombies (struct proc *);

void initialize_version_info (void);

void send_signal (mach_port_t, int, mach_port_t);




#endif
