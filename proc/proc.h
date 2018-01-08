/* Process server definitions
   Copyright (C) 1992,93,94,95,96,99,2000,01,13
     Free Software Foundation, Inc.

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

#include <sys/resource.h>
#include <sys/mman.h>
#include <hurd/ports.h>
#include <hurd/ihash.h>
#include <pthread.h>

struct proc
{
  struct port_info p_pi;

  /* List of members of a process group */
  struct proc *p_gnext, **p_gprevp; /* process group */

  /* Hash table pointers that point here */
  hurd_ihash_locp_t p_pidhashloc;		/* by pid */
  hurd_ihash_locp_t p_taskhashloc;		/* by task port */

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

  /* Processes may live in a task namespace identified by the
     notification port registered by proc_make_task_namespace.  */
  mach_port_t p_task_namespace;	/* send right */

  /* Communication */
  mach_port_t p_msgport;	/* send right */

  pthread_cond_t p_wakeup;

  /* Miscellaneous information */
  char *exe;			/* path to binary executable */
  vm_address_t p_argv, p_envp;
  vm_address_t start_code;	/* all executable segments are in this range */
  vm_address_t end_code;
  vm_address_t p_entry;		/* executable entry */
  int p_status;			/* to return via wait */
  int p_sigcode;
  struct rusage p_rusage;	/* my usage if I'm dead, to return via wait */

  struct rusage p_child_rusage;	/* accumulates p_rusage of all dead children */

  unsigned int p_exec:1;	/* has called proc_mark_exec */
  unsigned int p_stopped:1;	/* has called proc_mark_stop */
  unsigned int p_waited:1;	/* stop has been reported to parent */
  unsigned int p_exiting:1;	/* has called proc_mark_exit */
  unsigned int p_waiting:1;	/* blocked in wait */
  unsigned int p_traced:1;	/* has called proc_mark_traced */
  unsigned int p_nostopcld:1;	/* has called proc_mark_nostopchild */
  unsigned int p_parentset:1;	/* has had a parent set with proc_child */
  unsigned int p_deadmsg:1;	/* hang on requests for a message port */
  unsigned int p_checkmsghangs:1; /* someone is currently hanging on us */
  unsigned int p_msgportwait:1;	/* blocked in getmsgport */
  unsigned int p_noowner:1;	/* has no owner known */
  unsigned int p_loginleader:1;	/* leader of login collection */
  unsigned int p_dead:1;	/* process is dead */
  unsigned int p_important:1;	/* has called proc_mark_important */
};

typedef struct proc *pstruct_t;

struct pgrp
{
  hurd_ihash_locp_t pg_hashloc;
  struct proc *pg_plist;	/* member processes */
  struct pgrp *pg_next, **pg_prevp; /* list of pgrps in session */
  pid_t pg_pgid;
  struct session *pg_session;
  int pg_orphcnt;		/* number of non-orphaned processes */
};

struct session
{
  hurd_ihash_locp_t s_hashloc;
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
  int i_refcnt;
  int i_nuids;
  uid_t i_uids[0];
};

/* Structure for an exception port we are listening on.  */
struct exc
{
  struct port_info pi;
  mach_port_t forwardport;	/* Send right to forward msg to.  */
  int flavor;			/* State to restore faulting thread to.  */
  mach_msg_type_number_t statecnt;
  natural_t thread_state[0];
};

mach_port_t authserver;
struct proc *self_proc;		/* process HURD_PID_PROC (us) */
struct proc *init_proc;		/* process 1 (sysvinit) */
struct proc *startup_proc;	/* process 2 (hurd/startup) */

struct port_bucket *proc_bucket;
struct port_class *proc_class;
struct port_class *generic_port_class;
struct port_class *exc_class;

mach_port_t generic_port;	/* messages not related to a specific proc */
struct proc *kernel_proc;

pthread_mutex_t global_lock;

extern int startup_fallback;	/* (ab)use /hurd/startup's message port */

/* Forward declarations */
void complete_wait (struct proc *, int);
int check_uid (struct proc *, uid_t);
int check_owner (struct proc *, struct proc *);
void addalltasks (void);
void prociterate (void (*)(struct proc *, void *), void *);
void free_process (struct proc *);
void panic (char *);
int valid_task (task_t);
int genpid (void);
void abort_getmsgport (struct proc *);
int zombie_check_pid (pid_t);
void check_message_dying (struct proc *, struct proc *);
int check_msgport_death (struct proc *);
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
struct proc *pid_find_allow_zombie (int);
struct proc *task_find (task_t);
struct proc *task_find_nocreate (task_t);
struct pgrp *pgrp_find (int);
struct proc *reqport_find (mach_port_t);
struct session *session_find (pid_t);

void exc_clean (void *);

struct proc *add_tasks (task_t);
int pidfree (pid_t);

struct proc *create_init_proc (void);
struct proc *allocate_proc (task_t);
void proc_death_notify (struct proc *);
void complete_proc (struct proc *, pid_t);

void leave_pgrp (struct proc *);
void join_pgrp (struct proc *);
void boot_setsid (struct proc *);

int namespace_is_subprocess (struct proc *p);
error_t namespace_translate_pids (mach_port_t namespace, pid_t *pids, size_t pids_len);
struct proc *namespace_find_root (struct proc *);
void process_has_exited (struct proc *);
void alert_parent (struct proc *);
void reparent_zombies (struct proc *);
void complete_exit (struct proc *);

void initialize_version_info (void);

void send_signal (mach_port_t, int, mach_port_t);


#endif
