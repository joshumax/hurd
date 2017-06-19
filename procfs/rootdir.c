/* Hurd /proc filesystem, permanent files of the root directory.
   Copyright (C) 2010,13,14,17 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <mach/gnumach.h>
#include <mach/vm_param.h>
#include <mach/vm_statistics.h>
#include <mach/vm_cache_statistics.h>
#include "default_pager_U.h"
#include <mach/default_pager.h>
#include <mach_debug/mach_debug_types.h>
#include <hurd/paths.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <argz.h>
#include <ps.h>
#include <glob.h>
#include "procfs.h"
#include "procfs_dir.h"
#include "main.h"

#include "mach_debug_U.h"

/* This implements a directory node with the static files in /proc.
   NB: the libps functions for host information return static storage;
   using them would require locking and as a consequence it would be
   more complicated, not simpler.  */


/* Helper functions */

/* We get the boot time by using that of the kernel process. */
static error_t
get_boottime (struct ps_context *pc, struct timeval *tv)
{
  struct proc_stat *ps;
  error_t err;

  err = _proc_stat_create (opt_kernel_pid, pc, &ps);
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

/* We get the idle time by querying the kernel's idle thread. */
static error_t
get_idletime (struct ps_context *pc, struct timeval *tv)
{
  struct proc_stat *ps, *pst;
  thread_basic_info_t tbi;
  error_t err;
  int i;

  err = _proc_stat_create (opt_kernel_pid, pc, &ps);
  if (err)
    return err;

  pst = NULL, tbi = NULL;

  err = proc_stat_set_flags (ps, PSTAT_NUM_THREADS);
  if (err || !(proc_stat_flags (ps) & PSTAT_NUM_THREADS))
    {
      err = EIO;
      goto out;
    }

  /* Look for the idle thread */
  for (i=0; !tbi || !(tbi->flags & TH_FLAGS_IDLE); i++)
    {
      if (pst)
	_proc_stat_free (pst);

      pst = NULL, tbi = NULL;
      if (i >= proc_stat_num_threads (ps))
	{
	  err = ESRCH;
	  goto out;
	}

      err = proc_stat_thread_create (ps, i, &pst);
      if (err)
	continue;

      err = proc_stat_set_flags (pst, PSTAT_THREAD_BASIC);
      if (err || ! (proc_stat_flags (pst) & PSTAT_THREAD_BASIC))
	continue;

      tbi = proc_stat_thread_basic_info (pst);
    }

  /* We found it! */
  tv->tv_sec = tbi->system_time.seconds;
  tv->tv_usec = tbi->system_time.microseconds;
  err = 0;

out:
  if (pst) _proc_stat_free (pst);
  _proc_stat_free (ps);
  return err;
}

static error_t
get_swapinfo (default_pager_info_t *info)
{
  mach_port_t defpager;
  error_t err;

  defpager = file_name_lookup (_SERVERS_DEFPAGER, O_READ, 0);
  if (defpager == MACH_PORT_NULL)
    return errno;

  err = default_pager_info (defpager, info);
  mach_port_deallocate (mach_task_self (), defpager);

  return err;
}


/* Content generators */

static error_t
rootdir_gc_version (void *hook, char **contents, ssize_t *contents_len)
{
  struct utsname uts;
  int r;

  r = uname (&uts);
  if (r < 0)
    return errno;

  *contents_len = asprintf (contents,
      "Linux version 2.6.1 (%s %s %s %s)\n",
      uts.sysname, uts.release, uts.version, uts.machine);

  return 0;
}

static error_t
rootdir_gc_uptime (void *hook, char **contents, ssize_t *contents_len)
{
  struct timeval time, boottime, idletime;
  double up_secs, idle_secs;
  error_t err;

  err = gettimeofday (&time, NULL);
  if (err < 0)
    return errno;

  err = get_boottime (hook, &boottime);
  if (err)
    return err;

  err = get_idletime (hook, &idletime);
  if (err)
    return err;

  timersub (&time, &boottime, &time);
  up_secs = (time.tv_sec * 1000000. + time.tv_usec) / 1000000.;
  idle_secs = (idletime.tv_sec * 1000000. + idletime.tv_usec) / 1000000.;

  /* The second field is the total idle time. As far as I know we don't
     keep track of it.  However, procps uses it to compute "USER_HZ", and
     proc(5) specifies that it should be equal to USER_HZ times the idle value
     in ticks from /proc/stat.  So we assume a completely idle system both here
     and there to make that work.  */
  *contents_len = asprintf (contents, "%.2lf %.2lf\n", up_secs, idle_secs);

  return 0;
}

static error_t
rootdir_gc_stat (void *hook, char **contents, ssize_t *contents_len)
{
  struct timeval boottime, time, idletime;
  struct vm_statistics vmstats;
  unsigned long up_ticks, idle_ticks;
  error_t err;

  err = gettimeofday (&time, NULL);
  if (err < 0)
    return errno;

  err = get_boottime (hook, &boottime);
  if (err)
    return err;

  err = get_idletime (hook, &idletime);
  if (err)
    return err;

  err = vm_statistics (mach_task_self (), &vmstats);
  if (err)
    return EIO;

  timersub (&time, &boottime, &time);
  up_ticks = opt_clk_tck * (time.tv_sec * 1000000. + time.tv_usec) / 1000000.;
  idle_ticks = opt_clk_tck * (idletime.tv_sec * 1000000. + idletime.tv_usec) / 1000000.;

  *contents_len = asprintf (contents,
      "cpu  %lu 0 0 %lu 0 0 0 0 0\n"
      "cpu0 %lu 0 0 %lu 0 0 0 0 0\n"
      "intr 0\n"
      "page %d %d\n"
      "btime %lu\n",
      up_ticks - idle_ticks, idle_ticks,
      up_ticks - idle_ticks, idle_ticks,
      vmstats.pageins, vmstats.pageouts,
      boottime.tv_sec);

  return 0;
}

static error_t
rootdir_gc_loadavg (void *hook, char **contents, ssize_t *contents_len)
{
  host_load_info_data_t hli;
  mach_msg_type_number_t cnt;
  error_t err;

  cnt = HOST_LOAD_INFO_COUNT;
  err = host_info (mach_host_self (), HOST_LOAD_INFO, (host_info_t) &hli, &cnt);
  if (err)
    return err;

  assert_backtrace (cnt == HOST_LOAD_INFO_COUNT);
  *contents_len = asprintf (contents,
      "%.2f %.2f %.2f 1/0 0\n",
      hli.avenrun[0] / (double) LOAD_SCALE,
      hli.avenrun[1] / (double) LOAD_SCALE,
      hli.avenrun[2] / (double) LOAD_SCALE);

  return 0;
}

static error_t
rootdir_gc_meminfo (void *hook, char **contents, ssize_t *contents_len)
{
  host_basic_info_data_t hbi;
  mach_msg_type_number_t cnt;
  struct vm_statistics vmstats;
  struct vm_cache_statistics cache_stats;
  default_pager_info_t swap;
  FILE *m;
  error_t err;

  m = open_memstream (contents, (size_t *) contents_len);
  if (m == NULL)
    {
      err = ENOMEM;
      goto out;
    }

  err = vm_statistics (mach_task_self (), &vmstats);
  if (err)
    {
      err = EIO;
      goto out;
    }

  err = vm_cache_statistics (mach_task_self (), &cache_stats);
  if (err)
    {
      err = EIO;
      goto out;
    }

  cnt = HOST_BASIC_INFO_COUNT;
  err = host_info (mach_host_self (), HOST_BASIC_INFO, (host_info_t) &hbi, &cnt);
  if (err)
    goto out;

  assert_backtrace (cnt == HOST_BASIC_INFO_COUNT);
  fprintf (m,
      "MemTotal: %14lu kB\n"
      "MemFree:  %14lu kB\n"
      "Buffers:  %14lu kB\n"
      "Cached:   %14lu kB\n"
      "Active:   %14lu kB\n"
      "Inactive: %14lu kB\n"
      "Mlocked:  %14lu kB\n"
      ,
      (long unsigned) hbi.memory_size / 1024,
      (long unsigned) vmstats.free_count * PAGE_SIZE / 1024,
      0UL,
      (long unsigned) cache_stats.cache_count * PAGE_SIZE / 1024,
      (long unsigned) vmstats.active_count * PAGE_SIZE / 1024,
      (long unsigned) vmstats.inactive_count * PAGE_SIZE / 1024,
      (long unsigned) vmstats.wire_count * PAGE_SIZE / 1024);

  err = get_swapinfo (&swap);
  if (err)
    /* This is not fatal, we just omit the information.  */
    err = 0;
  else
    fprintf (m,
      "SwapTotal:%14lu kB\n"
      "SwapFree: %14lu kB\n"
      ,
      (long unsigned) swap.dpi_total_space / 1024,
      (long unsigned) swap.dpi_free_space / 1024);

 out:
  fclose (m);
  return err;
}

static error_t
rootdir_gc_vmstat (void *hook, char **contents, ssize_t *contents_len)
{
  struct vm_statistics vmstats;
  error_t err;

  err = vm_statistics (mach_task_self (), &vmstats);
  if (err)
    return EIO;

  *contents_len = asprintf (contents,
      "nr_free_pages %lu\n"
      "nr_inactive_anon %lu\n"
      "nr_active_anon %lu\n"
      "nr_inactive_file %lu\n"
      "nr_active_file %lu\n"
      "nr_unevictable %lu\n"
      "nr_mlock %lu\n"
      "pgpgin %lu\n"
      "pgpgout %lu\n"
      "pgfault %lu\n",
      (long unsigned) vmstats.free_count,
      /* FIXME: how can we distinguish the anon/file pages? Maybe we can
         ask the default pager how many it manages? */
      (long unsigned) vmstats.inactive_count,
      (long unsigned) vmstats.active_count,
      (long unsigned) 0,
      (long unsigned) 0,
      (long unsigned) vmstats.wire_count,
      (long unsigned) vmstats.wire_count,
      (long unsigned) vmstats.pageins,
      (long unsigned) vmstats.pageouts,
      (long unsigned) vmstats.faults);

  return 0;
}

static error_t
rootdir_gc_cmdline (void *hook, char **contents, ssize_t *contents_len)
{
  struct ps_context *pc = hook;
  struct proc_stat *ps;
  error_t err;

  err = _proc_stat_create (opt_kernel_pid, pc, &ps);
  if (err)
    return EIO;

  err = proc_stat_set_flags (ps, PSTAT_ARGS);
  if (err || ! (proc_stat_flags (ps) & PSTAT_ARGS))
    {
      err = EIO;
      goto out;
    }

  *contents_len = proc_stat_args_len (ps);
  *contents = malloc (*contents_len);
  if (! *contents)
    {
      err = ENOMEM;
      goto out;
    }

  memcpy (*contents, proc_stat_args (ps), *contents_len);
  argz_stringify (*contents, *contents_len, ' ');
  (*contents)[*contents_len - 1] = '\n';

out:
  _proc_stat_free (ps);
  return err;
}

static struct node *rootdir_self_node;
static struct node *rootdir_mounts_node;

static error_t
rootdir_gc_slabinfo (void *hook, char **contents, ssize_t *contents_len)
{
  error_t err;
  FILE *m;
  const char header[] =
    "cache                          obj slab  bufs   objs   bufs"
    "    total reclaimable\n"
    "name                  flags   size size /slab  usage  count"
    "   memory      memory\n";
  cache_info_array_t cache_info;
  size_t mem_usage, mem_reclaimable, mem_total, mem_total_reclaimable;
  mach_msg_type_number_t cache_info_count;
  int i;

  cache_info = NULL;
  cache_info_count = 0;

  err = host_slab_info (mach_host_self(), &cache_info, &cache_info_count);
  if (err)
    return err;

  m = open_memstream (contents, (size_t *) contents_len);
  if (m == NULL)
    {
      err = ENOMEM;
      goto out;
    }

  fprintf (m, "%s", header);

  mem_total = 0;
  mem_total_reclaimable = 0;

  for (i = 0; i < cache_info_count; i++)
    {
      mem_usage =
	(cache_info[i].nr_slabs * cache_info[i].slab_size) >> 10;
      mem_total += mem_usage;
      mem_reclaimable =
	(cache_info[i].nr_free_slabs * cache_info[i].slab_size) >> 10;
      mem_total_reclaimable += mem_reclaimable;
      fprintf (m,
               "%-21s %04x %7zu %3zuk  %4lu %6lu %6lu %7zuk %10zuk\n",
               cache_info[i].name, cache_info[i].flags,
               cache_info[i].obj_size, cache_info[i].slab_size >> 10,
               cache_info[i].bufs_per_slab, cache_info[i].nr_objs,
               cache_info[i].nr_bufs, mem_usage, mem_reclaimable);
    }

  fprintf (m, "total: %zuk, reclaimable: %zuk\n",
           mem_total, mem_total_reclaimable);

  fclose (m);

 out:
  vm_deallocate (mach_task_self (), (vm_address_t) cache_info,
                 cache_info_count * sizeof *cache_info);
  return err;
}

static error_t
rootdir_gc_hostinfo (void *hook, char **contents, ssize_t *contents_len)
{
  error_t err;
  FILE *m;
  host_basic_info_t basic;
  host_sched_info_t sched;
  host_load_info_t load;

  m = open_memstream (contents, (size_t *) contents_len);
  if (m == NULL)
    return ENOMEM;

  err = ps_host_basic_info (&basic);
  if (! err)
    fprintf (m, "Basic info:\n"
             "max_cpus	= %10u	/* max number of cpus possible */\n"
             "avail_cpus	= %10u	/* number of cpus now available */\n"
             "memory_size	= %10u	/* size of memory in bytes */\n"
             "cpu_type	= %10u	/* cpu type */\n"
             "cpu_subtype	= %10u	/* cpu subtype */\n",
             basic->max_cpus,
             basic->avail_cpus,
             basic->memory_size,
             basic->cpu_type,
             basic->cpu_subtype);

  err = ps_host_sched_info (&sched);
  if (! err)
    fprintf (m, "\nScheduling info:\n"
             "min_timeout	= %10u	/* minimum timeout in milliseconds */\n"
             "min_quantum	= %10u	/* minimum quantum in milliseconds */\n",
             sched->min_timeout,
             sched->min_quantum);

  err = ps_host_load_info (&load);
  if (! err)
    fprintf (m, "\nLoad info:\n"
             "avenrun[3]	= { %.2f, %.2f, %.2f }\n"
             "mach_factor[3]	= { %.2f, %.2f, %.2f }\n",
             load->avenrun[0] / (double) LOAD_SCALE,
             load->avenrun[1] / (double) LOAD_SCALE,
             load->avenrun[2] / (double) LOAD_SCALE,
             load->mach_factor[0] / (double) LOAD_SCALE,
             load->mach_factor[1] / (double) LOAD_SCALE,
             load->mach_factor[2] / (double) LOAD_SCALE);

  fclose (m);
  return 0;
}

static error_t
rootdir_gc_filesystems (void *hook, char **contents, ssize_t *contents_len)
{
  error_t err = 0;
  size_t i;
  int glob_ret;
  glob_t matches;
  FILE *m;

  m = open_memstream (contents, (size_t *) contents_len);
  if (m == NULL)
    return errno;

  glob_ret = glob (_HURD "*fs", 0, NULL, &matches);
  switch (glob_ret)
    {
    case 0:
      for (i = 0; i < matches.gl_pathc; i++)
	{
	  /* Get ith entry, shave off the prefix.  */
	  char *name = &matches.gl_pathv[i][sizeof _HURD - 1];

	  /* Linux naming convention is a bit inconsistent.  */
	  if (strncmp (name, "ext", 3) == 0
	      || strcmp (name, "procfs") == 0)
	    /* Drop the fs suffix.  */
	    name[strlen (name) - 2] = 0;

	  fprintf (m, "\t%s\n", name);
	}

      globfree (&matches);
      break;

    case GLOB_NOMATCH:
      /* Poor fellow.  */
      break;

    case GLOB_NOSPACE:
      err = ENOMEM;
      break;

    default:
      /* This should not happen.  */
      err = EGRATUITOUS;
    }

  fclose (m);
  return err;
}

static error_t
rootdir_gc_swaps (void *hook, char **contents, ssize_t *contents_len)
{
  mach_port_t defpager;
  error_t err = 0;
  FILE *m;
  vm_size_t *free = NULL;
  size_t nfree = 0;
  vm_size_t *size = NULL;
  size_t nsize = 0;
  char *names = NULL, *name;
  size_t names_len = 0;
  size_t i;

  m = open_memstream (contents, (size_t *) contents_len);
  if (m == NULL)
    return errno;

  defpager = file_name_lookup (_SERVERS_DEFPAGER, O_READ, 0);
  if (defpager == MACH_PORT_NULL)
    {
      err = errno;
      goto out_fclose;
    }

  err = default_pager_storage_info (defpager, &size, &nsize, &free, &nfree,
				    &names, &names_len);
  if (err)
    goto out;

  fprintf(m, "Filename\tType\t\tSize\tUsed\tPriority\n");
  name = names;
  for (i = 0; i < nfree; i++)
    {
      fprintf (m, "%s\tpartition\t%zu\t%zu\t-1\n",
	       name, size[i] >> 10, (size[i] - free[i]) >> 10);
      name = argz_next (names, names_len, name);
    }

  vm_deallocate (mach_task_self(), (vm_offset_t) free, nfree * sizeof(*free));
  vm_deallocate (mach_task_self(), (vm_offset_t) size, nsize * sizeof(*size));
  vm_deallocate (mach_task_self(), (vm_offset_t) names, names_len);

out:
  mach_port_deallocate (mach_task_self (), defpager);
out_fclose:
  fclose (m);
  return err;
}

/* Glue logic and entries table */

static struct node *
rootdir_file_make_node (void *dir_hook, const void *entry_hook)
{
  /* The entry hook we use is actually a procfs_node_ops for the file to be
     created.  The hook associated to these newly created files (and passed
     to the generators above as a consequence) is always the same global
     ps_context, which we get from rootdir_make_node as the directory hook. */
  return procfs_make_node (entry_hook, dir_hook);
}

static struct node *
rootdir_symlink_make_node (void *dir_hook, const void *entry_hook)
{
  struct node *np = procfs_make_node (entry_hook, dir_hook);
  if (np)
    procfs_node_chtype (np, S_IFLNK);
  return np;
}

/* Translator linkage.  */

static pthread_spinlock_t rootdir_translated_node_lock =
  PTHREAD_SPINLOCK_INITIALIZER;

struct procfs_translated_node_ops
{
  struct procfs_node_ops node_ops;

  struct node **npp;
  char *argz;
  size_t argz_len;
};

static struct node *
rootdir_make_translated_node (void *dir_hook, const void *entry_hook)
{
  const struct procfs_translated_node_ops *ops = entry_hook;
  struct node *np, *prev;

  pthread_spin_lock (&rootdir_translated_node_lock);
  np = *ops->npp;
  pthread_spin_unlock (&rootdir_translated_node_lock);

  if (np != NULL)
    {
      netfs_nref (np);
      return np;
    }

  np = procfs_make_node (entry_hook, (void *) entry_hook);
  if (np == NULL)
    return NULL;

  procfs_node_chtype (np, S_IFREG | S_IPTRANS);
  procfs_node_chmod (np, 0444);

  pthread_spin_lock (&rootdir_translated_node_lock);
  prev = *ops->npp;
  if (*ops->npp == NULL)
    *ops->npp = np;
  pthread_spin_unlock (&rootdir_translated_node_lock);

  if (prev != NULL)
    {
      procfs_cleanup (np);
      np = prev;
    }

  return np;
}

static error_t
rootdir_translated_node_get_translator (void *hook, char **argz,
					size_t *argz_len)
{
  const struct procfs_translated_node_ops *ops = hook;

  *argz = malloc (ops->argz_len);
  if (! *argz)
    return ENOMEM;

  memcpy (*argz, ops->argz, ops->argz_len);
  *argz_len = ops->argz_len;
  return 0;
}

#define ROOTDIR_DEFINE_TRANSLATED_NODE(NPP, ARGZ)		  \
  &(struct procfs_translated_node_ops) {			  \
    .node_ops = {						  \
      .get_translator = rootdir_translated_node_get_translator,	  \
    },								  \
    .npp = NPP,							  \
    .argz = (ARGZ),						  \
    .argz_len = sizeof (ARGZ),					  \
  }

static const struct procfs_dir_entry rootdir_entries[] = {
  {
    .name = "self",
    .hook = ROOTDIR_DEFINE_TRANSLATED_NODE (&rootdir_self_node,
					    _HURD_MAGIC "\0pid"),
    .ops = {
      .make_node = rootdir_make_translated_node,
    }
  },
  {
    .name = "version",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_version,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "uptime",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_uptime,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "stat",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_stat,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "loadavg",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_loadavg,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "meminfo",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_meminfo,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "vmstat",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_vmstat,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "cmdline",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_cmdline,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "mounts",
    .hook = ROOTDIR_DEFINE_TRANSLATED_NODE (&rootdir_mounts_node,
					    _HURD_MTAB "\0/"),
    .ops = {
      .make_node = rootdir_make_translated_node,
    }
  },
  {
    .name = "slabinfo",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_slabinfo,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "hostinfo",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_hostinfo,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "filesystems",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_filesystems,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
  {
    .name = "swaps",
    .hook = & (struct procfs_node_ops) {
      .get_contents = rootdir_gc_swaps,
      .cleanup_contents = procfs_cleanup_contents_with_free,
    },
  },
#ifdef PROFILE
  /* In order to get a usable gmon.out file, we must apparently use exit(). */
  {
    .name = "exit",
    .ops = {
      .make_node = exit,
    },
  },
#endif
  {}
};

struct node
*rootdir_make_node (struct ps_context *pc)
{
  static const struct procfs_dir_ops ops = {
    .entries = rootdir_entries,
    .entry_ops = {
      .make_node = rootdir_file_make_node,
    },
  };
  return procfs_dir_make_node (&ops, pc);
}

