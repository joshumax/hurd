#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hurd/process.h>
#include <hurd/resource.h>
#include <mach/vm_param.h>
#include <ps.h>
#include "procfs.h"
#include "procfs_dir.h"
#include "process.h"
#include "main.h"

/* Implementations for the process_file_desc.get_contents callback.  */

static ssize_t
process_file_gc_cmdline (struct proc_stat *ps, char **contents)
{
  *contents = proc_stat_args(ps);
  return proc_stat_args_len(ps);
}

static ssize_t
process_file_gc_environ (struct proc_stat *ps, char **contents)
{
  *contents = proc_stat_env(ps);
  return proc_stat_env_len(ps);
}

static char state_char (struct proc_stat *ps)
{
  int i;

  for (i = 0; (1 << i) & (PSTAT_STATE_P_STATES | PSTAT_STATE_T_STATES); i++)
    if (proc_stat_state (ps) & (1 << i))
      return proc_stat_state_tags[i];

  return '?';
}

static const char *state_string (struct proc_stat *ps)
{
  static const char *const state_strings[] = {
    "T (stopped)",
    "Z (zombie)",
    "R (running)",
    "H (halted)",
    "D (disk sleep)",
    "S (sleeping)",
    "I (idle)",
    NULL
  };
  int i;

  for (i = 0; state_strings[i]; i++)
    if (proc_stat_state (ps) & (1 << i))
      return state_strings[i];

  return "? (unknown)";
}

static long int sc_tv (time_value_t tv)
{
  double usecs = ((double) tv.seconds) * 1000000 + tv.microseconds;
  return usecs * opt_clk_tck / 1000000;
}

static long long int jiff_tv (time_value_t tv)
{
  double usecs = ((double) tv.seconds) * 1000000 + tv.microseconds;
  /* Let's say a jiffy is 1/100 of a second.. */
  return usecs * 100 / 1000000;
}

static ssize_t
process_file_gc_stat (struct proc_stat *ps, char **contents)
{
  struct procinfo *pi = proc_stat_proc_info (ps);
  task_basic_info_t tbi = proc_stat_task_basic_info (ps);
  thread_basic_info_t thbi = proc_stat_thread_basic_info (ps);

  /* See proc(5) for more information about the contents of each field for the
     Linux procfs.  */
  return asprintf (contents,
      "%d (%s) %c "		/* pid, command, state */
      "%d %d %d "		/* ppid, pgid, session */
      "%d %d "			/* controling tty stuff */
      "%u "			/* flags, as defined by <linux/sched.h> */
      "%lu %lu %lu %lu "	/* page fault counts */
      "%lu %lu %ld %ld "	/* user/sys times, in sysconf(_SC_CLK_TCK) */
      "%d %d "			/* scheduler params (priority, nice) */
      "%d %ld "			/* number of threads, [obsolete] */
      "%llu "			/* start time since boot (jiffies) */
      "%lu %ld %lu "		/* virtual size (bytes), rss (pages), rss lim */
      "%lu %lu %lu %lu %lu "	/* some vm addresses (code, stack, sp, pc) */
      "%lu %lu %lu %lu "	/* pending, blocked, ignored and caught sigs */
      "%lu "			/* wait channel */
      "%lu %lu "		/* swap usage (not maintained in Linux) */
      "%d "			/* exit signal, to be sent to the parent */
      "%d "			/* last processor used */
      "%u %u "			/* RT priority and policy */
      "%llu "			/* aggregated block I/O delay */
      "\n",
      proc_stat_pid (ps), proc_stat_args (ps), state_char (ps),
      pi->ppid, pi->pgrp, pi->session,
      0, 0,		/* no such thing as a major:minor for ctty */
      0,		/* no such thing as CLONE_* flags on Hurd */
      0L, 0L, 0L, 0L,	/* TASK_EVENTS_INFO is unavailable on GNU Mach */
      sc_tv (thbi->user_time), sc_tv (thbi->system_time),
      0L, 0L,		/* cumulative time for children */
      MACH_PRIORITY_TO_NICE(thbi->base_priority) + 20,
      MACH_PRIORITY_TO_NICE(thbi->base_priority),
      pi->nthreads, 0L,
      jiff_tv (thbi->creation_time), /* FIXME: ... since boot */
      (long unsigned int) tbi->virtual_size,
      (long unsigned int) tbi->resident_size / PAGE_SIZE, 0L,
      0L, 0L, 0L, 0L, 0L,
      0L, 0L, 0L, 0L,
      (long unsigned int) proc_stat_thread_rpc (ps), /* close enough */
      0L, 0L,
      0,
      0,
      0, 0,
      0LL);
}

static ssize_t
process_file_gc_statm (struct proc_stat *ps, char **contents)
{
  task_basic_info_t tbi = proc_stat_task_basic_info (ps);
  return asprintf (contents,
      "%lu %lu 0 0 0 0 0\n",
      tbi->virtual_size  / sysconf(_SC_PAGE_SIZE),
      tbi->resident_size / sysconf(_SC_PAGE_SIZE));
}

static ssize_t
process_file_gc_status (struct proc_stat *ps, char **contents)
{
  task_basic_info_t tbi = proc_stat_task_basic_info (ps);
  return asprintf (contents,
      "Name:\t%s\n"
      "State:\t%s\n"
      "Tgid:\t%u\n"
      "Pid:\t%u\n"
      "PPid:\t%u\n"
      "Uid:\t%u\t%u\t%u\t%u\n"
      "VmSize:\t%8u kB\n"
      "VmPeak:\t%8u kB\n"
      "VmRSS:\t%8u kB\n"
      "VmHWM:\t%8u kB\n" /* ie. resident peak */
      "Threads:\t%u\n",
      proc_stat_args (ps),
      state_string (ps),
      proc_stat_pid (ps), /* XXX will need more work for threads */
      proc_stat_pid (ps),
      proc_stat_proc_info (ps)->ppid,
      proc_stat_owner_uid (ps),
      proc_stat_owner_uid (ps),
      proc_stat_owner_uid (ps),
      proc_stat_owner_uid (ps),
      tbi->virtual_size / 1024,
      tbi->virtual_size / 1024,
      tbi->resident_size / 1024,
      tbi->resident_size / 1024,
      proc_stat_num_threads (ps));
}


/* Describes a file in a process directory.  This is used as an "entry hook"
 * for our procfs_dir entry table, passed to process_file_make_node.  */
struct process_file_desc
{
  /* The proc_stat information required to get the contents of this file.  */
  ps_flags_t needs;

  /* Once we have acquired the necessary information, there can be only
     memory allocation errors, hence this simplified signature.  */
  ssize_t (*get_contents) (struct proc_stat *ps, char **contents);

  /* The cmdline and environ contents don't need any cleaning since they are
     part of a proc_stat structure.  */
  int no_cleanup;

  /* If specified, the file mode to be set with procfs_node_chmod().  */
  mode_t mode;
};

/* Information associated to an actual file node.  */
struct process_file_node
{
  const struct process_file_desc *desc;
  struct proc_stat *ps;
};

static error_t
process_file_get_contents (void *hook, char **contents, ssize_t *contents_len)
{
  struct process_file_node *file = hook;
  error_t err;

  err = proc_stat_set_flags (file->ps, file->desc->needs);
  if (err)
    return EIO;
  if ((proc_stat_flags (file->ps) & file->desc->needs) != file->desc->needs)
    return EIO;

  *contents_len = file->desc->get_contents (file->ps, contents);
  return 0;
}

static struct node *
process_file_make_node (void *dir_hook, void *entry_hook)
{
  static const struct procfs_node_ops ops = {
    .get_contents = process_file_get_contents,
    .cleanup_contents = procfs_cleanup_contents_with_free,
    .cleanup = free,
  };
  static const struct procfs_node_ops ops_no_cleanup = {
    .get_contents = process_file_get_contents,
    .cleanup = free,
  };
  struct process_file_node *f;
  struct node *np;

  f = malloc (sizeof *f);
  if (! f)
    return NULL;

  f->desc = entry_hook;
  f->ps = dir_hook;

  np = procfs_make_node (f->desc->no_cleanup ? &ops_no_cleanup : &ops, f);
  if (! np)
    return NULL;

  procfs_node_chown (np, proc_stat_owner_uid (f->ps));
  if (f->desc->mode)
    procfs_node_chmod (np, f->desc->mode);

  return np;
}


static struct procfs_dir_entry entries[] = {
  {
    .name = "cmdline",
    .make_node = process_file_make_node,
    .hook = & (struct process_file_desc) {
      .get_contents = process_file_gc_cmdline,
      .needs = PSTAT_ARGS,
      .no_cleanup = 1,
    },
  },
  {
    .name = "environ",
    .make_node = process_file_make_node,
    .hook = & (struct process_file_desc) {
      .get_contents = process_file_gc_environ,
      .needs = PSTAT_ENV,
      .no_cleanup = 1,
      .mode = 0400,
    },
  },
  {
    /* Beware of the hack below, which requires this to be entries[2].  */
    .name = "stat",
    .make_node = process_file_make_node,
    .hook = & (struct process_file_desc) {
      .get_contents = process_file_gc_stat,
      .needs = PSTAT_PID | PSTAT_ARGS | PSTAT_STATE | PSTAT_PROC_INFO
	| PSTAT_TASK | PSTAT_TASK_BASIC | PSTAT_THREAD_BASIC
	| PSTAT_THREAD_WAIT,
      .mode = 0400,
    },
  },
  {
    .name = "statm",
    .make_node = process_file_make_node,
    .hook = & (struct process_file_desc) {
      .get_contents = process_file_gc_statm,
      .needs = PSTAT_TASK_BASIC,
      .mode = 0444,
    },
  },
  {
    .name = "status",
    .make_node = process_file_make_node,
    .hook = & (struct process_file_desc) {
      .get_contents = process_file_gc_status,
      .needs = PSTAT_PID | PSTAT_ARGS | PSTAT_STATE | PSTAT_PROC_INFO
        | PSTAT_TASK_BASIC | PSTAT_OWNER_UID | PSTAT_NUM_THREADS,
      .mode = 0444,
    },
  },
  {}
};

error_t
process_lookup_pid (struct ps_context *pc, pid_t pid, struct node **np)
{
  struct proc_stat *ps;
  int owner;
  error_t err;

  err = _proc_stat_create (pid, pc, &ps);
  if (err == ESRCH)
    return ENOENT;
  if (err)
    return EIO;

  err = proc_stat_set_flags (ps, PSTAT_OWNER_UID);
  if (err || ! (proc_stat_flags (ps) & PSTAT_OWNER_UID))
    return EIO;

  /* FIXME: have a separate proc_desc structure for each file, so this can be
     accessed in a more robust and straightforward way. */
  ((struct process_file_desc *) entries[2].hook)->mode = opt_stat_mode;

  *np = procfs_dir_make_node (entries, ps, (void (*)(void *)) _proc_stat_free);
  if (! *np)
    return ENOMEM;

  owner = proc_stat_owner_uid (ps);
  procfs_node_chown (*np, owner >= 0 ? owner : opt_anon_owner);
  return 0;
}
