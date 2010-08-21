#include <mach/vm_statistics.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <ps.h>
#include "procfs.h"
#include "procfs_dir.h"

/* This implements a directory node with the static files in /proc */

#define INIT_PID 1

static error_t
get_boottime (struct ps_context *pc, struct timeval *tv)
{
  struct proc_stat *ps;
  error_t err;

  err = _proc_stat_create (INIT_PID, pc, &ps);
  if (err)
    return err;

  err = proc_stat_set_flags (ps, PSTAT_TASK_BASIC);
  if (err || !(proc_stat_flags (ps) & PSTAT_TASK_BASIC))
    err = EIO;

  if (! err)
    {
      task_basic_info_t tbi = proc_stat_task_basic_info (ps);
      tv->tv_sec = tbi->creation_time.seconds;
      tv->tv_usec = tbi->creation_time.microseconds;
    }

  _proc_stat_free (ps);
  return err;
}

static error_t
rootdir_gc_version (void *hook, void **contents, size_t *contents_len)
{
  struct utsname uts;
  int r;

  r = uname (&uts);
  if (r < 0)
    return errno;

  *contents_len = asprintf ((char **) contents,
      "Linux version 2.6.1 (%s %s %s %s)\n",
      uts.sysname, uts.release, uts.version, uts.machine);

  return *contents_len >= 0 ? 0 : ENOMEM;
}

/* Uptime -- we use the start time of init to deduce it. This is probably a bit
   fragile, as any clock update will make the result inaccurate. */
static error_t
rootdir_gc_uptime (void *hook, void **contents, size_t *contents_len)
{
  struct timeval time, boottime;
  double up_secs;
  error_t err;

  err = gettimeofday (&time, NULL);
  if (err < 0)
    return errno;

  err = get_boottime (hook, &boottime);
  if (err)
    return err;

  timersub (&time, &boottime, &time);
  up_secs = time.tv_sec + time.tv_usec / 1000000.;

  /* The second field is the total idle time. As far as I know we don't
     keep track of it. */
  *contents_len = asprintf ((char **) contents, "%.2lf %.2lf\n", up_secs, 0.);

  return *contents_len >= 0 ? 0 : ENOMEM;
}

static error_t
rootdir_gc_stat (void *hook, void **contents, size_t *contents_len)
{
  struct timeval boottime;
  struct vm_statistics vmstats;
  error_t err;

  err = get_boottime (hook, &boottime);
  if (err)
    return err;

  err = vm_statistics (mach_task_self (), &vmstats);
  if (err)
    return EIO;

  *contents_len = asprintf ((char **) contents,
      /* Does Mach keeps track of any of this? */
      "cpu  0 0 0 0 0 0 0 0 0\n"
      "cpu0 0 0 0 0 0 0 0 0 0\n"
      "intr 0\n"
      /* This we know. */
      "page %d %d\n"
      "btime %lu\n",
      vmstats.pageins, vmstats.pageouts,
      boottime.tv_sec);

  return *contents_len >= 0 ? 0 : ENOMEM;
}

static struct node *
rootdir_file_make_node (void *dir_hook, void *entry_hook)
{
  return procfs_make_node (entry_hook, dir_hook);
}

static struct procfs_dir_entry rootdir_entries[] = {
  {
    .name = "version",
    .make_node = rootdir_file_make_node,
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_version,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "uptime",
    .make_node = rootdir_file_make_node,
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_uptime,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "stat",
    .make_node = rootdir_file_make_node,
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_stat,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {}
};

error_t
rootdir_create_node (struct node **np)
{
  struct ps_context *pc;
  error_t err;

  err = ps_context_create (getproc (), &pc);
  if (err)
    return err;

  *np = procfs_dir_make_node (rootdir_entries, pc,
			      (void (*)(void *)) ps_context_free);
  return 0;
}

