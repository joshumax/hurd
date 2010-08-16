/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   procfs_nonpid_files.c -- This file contains function definitions
                            to create and update the non-Per PID
                            files and their contents.
               
   Copyright (C) 2008, FSF.
   Written as a Summer of Code Project
   

   procfs is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   procfs is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. 
   
   A portion of the code in this file is based on vmstat.c code
   present in the hurd repositories copyrighted to FSF. The
   Copyright notice from that file is given below.
   
   Copyright (C) 1997,98,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
   This file is part of the GNU Hurd.
*/

#include <stdio.h>
#include <unistd.h>
#include <hurd/netfs.h>
#include <hurd/ihash.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <mach/vm_statistics.h>
#include <mach/default_pager.h>
#include <hurd.h>
#include <hurd/paths.h>
#include <mach.h>
#include <ps.h>
#include <time.h>

#include "procfs.h"

typedef long long val_t;
#define BADVAL ((val_t) - 1LL)

/* default pager port (must be privileged to fetch this).  */
mach_port_t def_pager;
struct default_pager_info def_pager_info;

error_t procfs_create_uptime (struct procfs_dir *dir, 
                           struct node **node,
                           time_t timestamp)
{
  int err;
  char *file_name, *file_path;
  struct procfs_dir_entry *dir_entry;
 
  if (asprintf (&file_name, "%s", "uptime") == -1)
    return errno;
  if (asprintf (&file_path, "%s", "uptime") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  return err;
}

error_t procfs_create_version(struct procfs_dir *dir, 
                           struct node **node,
                             time_t timestamp)
{
  int err;
  char *file_name, *file_path;
  struct procfs_dir_entry *dir_entry;
 
  if (asprintf (&file_name, "%s", "version") == -1)
    return errno;
  if (asprintf (&file_path, "%s", "version") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  return 0;
}

error_t procfs_create_stat (struct procfs_dir *dir, 
                           struct node **node,
                           time_t timestamp)
{
  int err;
  char *file_name, *file_path;
  struct procfs_dir_entry *dir_entry;
 
  if (asprintf (&file_name, "%s", "stat") == -1)
    return errno;
  if (asprintf (&file_path, "%s", "stat") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  return err;
}

error_t procfs_create_meminfo (struct procfs_dir *dir, 
                           struct node **node,
                           time_t timestamp)
{
  int err;
  char *file_name, *file_path;
  struct procfs_dir_entry *dir_entry;
 
  if (asprintf (&file_name, "%s", "meminfo") == -1)
    return errno;
  if (asprintf (&file_path, "%s", "meminfo") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  return err;
}

error_t procfs_create_loadavg (struct procfs_dir *dir, 
                           struct node **node,
                           time_t timestamp)
{
  int err;
  char *file_name, *file_path;
  struct procfs_dir_entry *dir_entry;
 
  if (asprintf (&file_name, "%s", "loadavg") == -1)
    return errno;
  if (asprintf (&file_path, "%s", "loadavg") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  return err;
}

error_t procfs_create_mounts (struct procfs_dir *dir,
                           struct node **node,
                           time_t timestamp)
{
  struct procfs_dir_entry *dir_entry;
  int err;

  dir_entry = update_pid_entries (dir, "mounts", timestamp, "/etc/mtab");
  err = procfs_create_node (dir_entry, "mounts", node);

  return err;
}

error_t get_uptime (struct timeval *uptime)
{
  struct timeval boot_time, now;
  error_t err;
  struct proc_stat *ps;
  
  err = _proc_stat_create (1, ps_context, &ps);
  
  if (err)
    return err;
    
  err = proc_stat_set_flags (ps, PSTAT_TASK_BASIC);
  if (!err && !(ps->flags & PSTAT_TASK_BASIC))
    err = EGRATUITOUS;
  
  if (! err)
    {
      time_value_t *const tv = &proc_stat_task_basic_info (ps)->creation_time;
      boot_time.tv_sec = tv->seconds;
      boot_time.tv_usec = tv->microseconds;
      if (gettimeofday (&now, 0) < 0)
        error (0, errno, "gettimeofday");
      timersub (&now, &boot_time, uptime);      
    }
    
  _proc_stat_free (ps); 
  return err;
}

error_t get_total_times (struct timeval *total_user_time,
                        struct timeval *total_system_time)
{
  error_t err;
  pid_t *pids;
  int pidslen = 0, count;
  struct proc_stat *ps;
  struct task_thread_times_info live_threads_times;
  
  struct timeval total_user_time_tmp;
  struct timeval total_system_time_tmp;
  struct timeval tmpval;
  
  timerclear (&total_user_time_tmp);
  timerclear (&total_system_time_tmp);
  
  pids = NULL;
  err = proc_getallpids (getproc (), &pids, &pidslen);
  
  if (!err)
    for (count = 0; count < pidslen; count++)
      {
        err = _proc_stat_create (pids[count], ps_context, &ps);
        if (err)
          return err;
          
        err = proc_stat_set_flags (ps, PSTAT_TASK_BASIC);
        if (!err && !(ps->flags & PSTAT_TASK_BASIC))
          err = EGRATUITOUS;
        
        if (! err)
          {
            tmpval.tv_sec = proc_stat_task_basic_info (ps)->user_time.seconds;
            tmpval.tv_usec = proc_stat_task_basic_info (ps)->user_time.seconds;
            timeradd (&total_user_time_tmp, &tmpval, &total_user_time_tmp);

            tmpval.tv_sec = proc_stat_task_basic_info (ps)->system_time.seconds;
            tmpval.tv_usec = proc_stat_task_basic_info (ps)->system_time.seconds;
            timeradd (&total_system_time_tmp, &tmpval, &total_system_time_tmp);
              
            error_t err = set_field_value (ps, PSTAT_TASK); 
            if (! err)
              {
                err = get_task_thread_times (ps->task, &live_threads_times);
                if (! err)
                  {
                    tmpval.tv_sec = live_threads_times.user_time.seconds;
                    tmpval.tv_usec = live_threads_times.user_time.microseconds;
                    timeradd (&total_user_time_tmp, &tmpval, &total_user_time_tmp);

                    tmpval.tv_sec = live_threads_times.system_time.seconds;
                    tmpval.tv_usec = live_threads_times.system_time.microseconds;
                    timeradd (&total_system_time_tmp, &tmpval, &total_system_time_tmp);
                  }          
              }
          }
        _proc_stat_free (ps); 
      }   
      
  total_user_time->tv_sec = total_user_time_tmp.tv_sec;
  total_user_time->tv_usec = total_user_time_tmp.tv_usec;
  
  total_system_time->tv_sec = total_system_time_tmp.tv_sec;
  total_system_time->tv_usec = total_system_time_tmp.tv_usec; 
   
  return err;
}

error_t procfs_read_nonpid_stat (struct dir_entry *dir_entry,
                        off_t offset, size_t *len, void *data)
{  
  char *stat_data;
  error_t err;
  jiffy_t total_user_time_jiffy, total_system_time_jiffy;
  jiffy_t idle_time_jiffy;
  struct timeval uptime, total_user_time, total_system_time;
  struct timeval idle_time;

  err = get_uptime (&uptime);
  
  if (! err)
    {
      err = get_total_times (&total_user_time, &total_system_time); 
      
      if (! err)
        {
          timersub (&uptime, &total_system_time,
                  &idle_time);
          
          total_user_time_jiffy = 100 * ((double) total_user_time.tv_sec + 
                       (double) total_user_time.tv_usec / (1000 * 1000));
          total_system_time_jiffy = 100 * ((double) total_system_time.tv_sec + 
                       (double) total_system_time.tv_usec / (1000 * 1000));
          idle_time_jiffy = 100 * ((double) idle_time.tv_sec + 
                       (double) idle_time.tv_usec / (1000 * 1000));
                       
          if (asprintf (&stat_data, "cpu  %llu %llu %llu %llu %llu %llu %d %d %d\n"
                   "cpu0 %llu %llu %llu %llu %llu %llu %d %d %d\n"
                   "intr %llu %llu %llu %llu %llu %llu %d %d %d\n",
                        total_user_time_jiffy, (long long unsigned) 0,
                        total_system_time_jiffy, idle_time_jiffy, 
                        (long long unsigned) 0, (long long unsigned) 0,
                        0, 0, 0, 
                        total_user_time_jiffy, (long long unsigned) 0,
                        total_system_time_jiffy, idle_time_jiffy, 
                        (long long unsigned) 0, (long long unsigned) 0,
                        0, 0, 0,
                        (long long unsigned) 0,
                        (long long unsigned) 0, (long long unsigned) 0, (long long unsigned) 0,
                        (long long unsigned) 0, 
                        (long long unsigned) 0, (long long unsigned) 0,
                        (long long unsigned) 0, (long long unsigned) 0) == -1)
            return errno;
        }    
    }      

  memcpy (data, stat_data, strlen(stat_data));
  *len = strlen (data);

  free (stat_data);
  return err;
}

/* Makes sure the default pager port and associated 
   info exists, and returns 0 if not (after printing
   an error).  */
static int
ensure_def_pager_info ()
{
  error_t err;

  if (def_pager == MACH_PORT_NULL)
    {
      mach_port_t host;

      err = get_privileged_ports (&host, 0);
      if (err == EPERM)
	{
	  /* We are not root, so try opening the /servers file.  */
	  def_pager = file_name_lookup (_SERVERS_DEFPAGER, O_READ, 0);
	  if (def_pager == MACH_PORT_NULL)
	    {
	      error (0, errno, _SERVERS_DEFPAGER);
	      return 0;
	    }
	}
      if (def_pager == MACH_PORT_NULL)
	{
	  if (err)
	    {
	      error (0, err, "get_privileged_ports");
	      return 0;
	    }

	  err = vm_set_default_memory_manager (host, &def_pager);
	  mach_port_deallocate (mach_task_self (), host);

	  if (err)
	    {
	      error (0, err, "vm_set_default_memory_manager");
	      return 0;
	    }
	}
    }

  if (!MACH_PORT_VALID (def_pager))
    {
      if (def_pager == MACH_PORT_NULL)
	{
	  error (0, 0,
		 "No default pager running, so no swap information available");
	  def_pager = MACH_PORT_DEAD; /* so we don't try again */
	}
      return 0;
    }

  err = default_pager_info (def_pager, &def_pager_info);
  if (err)
    error (0, err, "default_pager_info");
  return (err == 0);
}

#define SWAP_FIELD(getter, expr) \
  static val_t getter () \
  { return ensure_def_pager_info () ? (val_t) (expr) : BADVAL; }

SWAP_FIELD (get_swap_size, def_pager_info.dpi_total_space)
SWAP_FIELD (get_swap_free, def_pager_info.dpi_free_space)
SWAP_FIELD (get_swap_page_size, def_pager_info.dpi_page_size)
SWAP_FIELD (get_swap_active, (def_pager_info.dpi_total_space
			      - def_pager_info.dpi_free_space))

error_t procfs_read_nonpid_meminfo (struct dir_entry *dir_entry,
                        off_t offset, size_t *len, void *data)
{  
  char *meminfo_data;
  error_t err;
  struct vm_statistics vmstats;

  err = vm_statistics (mach_task_self (), &vmstats);
  
  unsigned long mem_size = ((vmstats.free_count + 
    vmstats.active_count + vmstats.inactive_count +
    vmstats.wire_count) * vmstats.pagesize) / 1024;

  if (! err)
    if (asprintf (&meminfo_data, "MemTotal:\t%lu kB\n"
        "MemFree:\t%lu kB\n"
        "Buffers:\t%ld kB\n"
        "Cached:\t\t%ld kB\n"
        "SwapCached:\t%ld kB\n"
        "Active:\t\t%lu kB\n"
        "Inactive:\t%lu kB\n"
        "HighTotal:\t%lu kB\n"
        "HighFree:\t%lu kB\n" 
        "LowTotal:\t%lu kB\n" 
        "LowFree:\t%lu kB\n" 
        "SwapTotal:\t%llu kB\n"
        "SwapFree:\t%llu kB\n", 
         mem_size, (PAGES_TO_BYTES(vmstats.free_count)) / 1024 , 0, 0, 0, 
         (PAGES_TO_BYTES(vmstats.active_count)) / 1024, 
         (PAGES_TO_BYTES(vmstats.inactive_count)) / 1024, 0, 0, 0, 0, 
         get_swap_size () / 1024, get_swap_free () / 1024) == -1)
      return errno;

  memcpy (data, meminfo_data, strlen(meminfo_data));
  *len = strlen (data);

  free (meminfo_data);
  return err;
}

error_t procfs_read_nonpid_loadavg (struct dir_entry *dir_entry,
                        off_t offset, size_t *len, void *data)
{  
  char *loadavg_data;
  error_t err;
  processor_set_info_t	info;
  natural_t *count;
  struct host_load_info *load;
  mach_port_t host;

  err = ps_host_load_info (&load);
  if (err)
    error (0, err, "ps_host_load_info");
    
  if (! err)
    if (asprintf (&loadavg_data, "%.2f %.2f %.2f %d/%d %d\n", 
          (double)load->avenrun[0] / (double)LOAD_SCALE,
	  (double)load->avenrun[1] / (double)LOAD_SCALE,
	  (double)load->avenrun[2] / (double)LOAD_SCALE, 0, 0, 0) == -1)
      return errno;

  memcpy (data, loadavg_data, strlen(loadavg_data));
  *len = strlen (data);

  free (loadavg_data);
  return err;
}

error_t procfs_read_nonpid_uptime (struct dir_entry *dir_entry,
                        off_t offset, size_t *len, void *data)
{  
  char *uptime_data;
  error_t err;
  double uptime_secs, idle_time_secs;

  struct timeval uptime_val;
  struct timeval uptime, total_user_time, total_system_time;
  struct timeval idle_time;
 
  
  err = get_uptime (&uptime);
  if (! err)
    {
      err = get_total_times (&total_user_time,
                           &total_system_time);
      if (! err)
        {
          timersub (&uptime, &total_system_time,
                    &idle_time);
                    
          uptime_secs = (double) uptime.tv_sec + 
                       (double) uptime.tv_usec / (1000 * 1000);
                       
          idle_time_secs = (double) idle_time.tv_sec + 
                       (double) idle_time.tv_usec / (1000 * 1000);

          if (asprintf (&uptime_data, "%.2f %.2f\n", 
	          uptime_secs, idle_time_secs) == -1)
            return errno;
         }                         
    }                       


  memcpy (data, uptime_data, strlen(uptime_data));
  *len = strlen (data);

  free (uptime_data);
  return err;
}

error_t procfs_read_nonpid_version (struct dir_entry *dir_entry,
                        off_t offset, size_t *len, void *data)
{  
  char *version_data;
  error_t err = 0;
  
  if (asprintf (&version_data, "Linux version 2.6.18\n", NULL) == -1)
    return errno;

  memcpy (data, version_data, strlen(version_data));
  *len = strlen (data);

  free (version_data);
  return err;
}
