#include <stdio.h>
#include <stdlib.h>
#include <hurd/process.h>
#include "procfs.h"
#include "procfs_dir.h"
#include "process.h"

struct process_node {
  process_t procserv;
  pid_t pid;
  struct procinfo info;
};


/* The proc_getprocargs() and proc_getprocenv() calls have the same
   prototype and we use them in the same way; namely, publish the data
   they return as-is.  We take advantage of this to have common code and
   use a function pointer as the procfs_dir "entry hook" to choose the
   call to use on a file by file basis.  */

struct process_argz_node
{
  struct process_node pn;
  error_t (*getargz) (process_t, pid_t, void **, mach_msg_type_number_t *);
};

static error_t
process_argz_get_contents (void *hook, void **contents, size_t *contents_len)
{
  struct process_argz_node *pz = hook;
  error_t err;

  *contents_len = 0;
  err = pz->getargz (pz->pn.procserv, pz->pn.pid, contents, contents_len);
  if (err)
    return EIO;

  return 0;
}

static struct node *
process_argz_make_node (void *dir_hook, void *entry_hook)
{
  static const struct procfs_node_ops ops = {
    .get_contents = process_argz_get_contents,
    .cleanup_contents = procfs_cleanup_contents_with_vm_deallocate,
    .cleanup = free,
  };
  struct process_argz_node *zn;

  zn = malloc (sizeof *zn);
  if (! zn)
    return NULL;

  memcpy (&zn->pn, dir_hook, sizeof zn->pn);
  zn->getargz = entry_hook;

  return procfs_make_node (&ops, zn);
}

/* The other files don't need any information besides the data in struct
   process_node. Furthermore, their contents don't have any nul byte.
   Consequently, we use a simple "multiplexer" based on the information
   below.  */

struct process_file_node
{
  struct process_node pn;
  error_t (*get_contents) (struct process_node *pn, char **contents);
};

static char mapstate (int hurd_state)
{
  return '?';
}

static error_t
process_file_gc_stat (struct process_node *pn, char **contents)
{
  char *argz;
  size_t argz_len;
  int len;

  argz = NULL, argz_len = 0;
  proc_getprocargs(pn->procserv, pn->pid, &argz, &argz_len);

  len = asprintf (contents,
      "%d (%s) %c "		/* pid, command, state */
      "%d %d %d "		/* ppid, pgid, session */
      "%d %d "			/* controling tty stuff */
      "%u "			/* flags, as defined by <linux/sched.h> */
      "%lu %lu %lu %lu "	/* page fault counts */
      "%lu %lu %ld %ld "	/* user/sys times, in sysconf(_SC_CLK_TCK) */
      "%ld %ld "		/* scheduler params (priority, nice) */
      "%ld %ld "		/* number of threads, [obsolete] */
      "%llu "			/* start time since boot (jiffies) */
      "%lu %ld %lu "		/* virtual size, rss, rss limit */
      "%lu %lu %lu %lu %lu "	/* some vm addresses (code, stack, sp, pc) */
      "%lu %lu %lu %lu "	/* pending, blocked, ignored and caught sigs */
      "%lu "			/* wait channel */
      "%lu %lu "		/* swap usage (not maintained in Linux) */
      "%d "			/* exit signal, to be sent to the parent */
      "%d "			/* last processor used */
      "%u %u "			/* RT priority and policy */
      "%llu "			/* aggregated block I/O delay */
      "\n",
      pn->pid, argz ?: "", mapstate (pn->info.state),
      pn->info.ppid, pn->info.pgrp, pn->info.session,
      0, 0,
      0,
      0L, 0L, 0L, 0L,
      0L, 0L, 0L, 0L,
      0L, 0L,
      0L, 0L,
      0LL,
      0L, 0L, 0L,
      0L, 0L, 0L, 0L, 0L,
      0L, 0L, 0L, 0L,
      0L,
      0L, 0L,
      0,
      0,
      0, 0,
      0LL);

  vm_deallocate (mach_task_self (), (vm_address_t) argz, argz_len);

  if (len < 0)
    return ENOMEM;

  return 0;
}

static error_t
process_file_get_contents (void *hook, void **contents, size_t *contents_len)
{
  struct process_file_node *fn = hook;
  error_t err;

  err = fn->get_contents (&fn->pn, (char **) contents);
  if (err)
    return err;

  *contents_len = strlen (*contents);
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
  struct process_file_node *fn;

  fn = malloc (sizeof *fn);
  if (! fn)
    return NULL;

  memcpy (&fn->pn, dir_hook, sizeof fn->pn);
  fn->get_contents = entry_hook;

  return procfs_make_node (&ops, fn);
}


static struct node *
process_make_node (process_t procserv, pid_t pid, const struct procinfo *info)
{
  static const struct procfs_dir_entry entries[] = {
    { "cmdline",	process_argz_make_node,		proc_getprocargs,	},
    { "environ",	process_argz_make_node,		proc_getprocenv,	},
    { "stat",		process_file_make_node,		process_file_gc_stat,	},
    { NULL, }
  };
  struct process_node *pn;
  struct node *np;

  pn = malloc (sizeof *pn);
  if (! pn)
    return NULL;

  pn->procserv = procserv;
  pn->pid = pid;
  memcpy (&pn->info, info, sizeof pn->info);

  np = procfs_dir_make_node (entries, pn, process_cleanup);
  np->nn_stat.st_uid = pn->info.owner;

  return np;
}

error_t
process_lookup_pid (process_t procserv, pid_t pid, struct node **np)
{
  procinfo_t info;
  size_t info_sz;
  data_t tw;
  size_t tw_sz;
  int flags;
  error_t err;

  tw_sz = info_sz = 0, flags = 0;
  err = proc_getprocinfo (procserv, pid, &flags, &info, &info_sz, &tw, &tw_sz);
  if (err == ESRCH)
    return ENOENT;
  if (err)
    return EIO;

  assert (info_sz * sizeof *info >= sizeof (struct procinfo));
  *np = process_make_node (procserv, pid, (struct procinfo *) info);
  vm_deallocate (mach_task_self (), (vm_address_t) info, info_sz);

  if (! *np)
    return ENOMEM;

  return 0;
}
