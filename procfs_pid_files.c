/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   procfs_pid_files.c -- This file contains definitions to perform
                         file operations such as creating, writing to,
                         reading from and removing files that holds
                         information for each process with PID
               
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
   
   A portion of the code in this file is based on ftpfs code
   present in the hurd repositories copyrighted to FSF. The
   Copyright notice from that file is given below.

*/

#include <hurd/netfs.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <mach/task_info.h>
#include <sys/resource.h>

#include "procfs.h"
#include "procfs_pid.h"

/* Update the files named NAME within the directory named
   PID also with SYMLINK TARGET if necessary. */
struct procfs_dir_entry*
update_pid_entries (struct procfs_dir *dir, const char *name,
                          time_t timestamp,
                          const char *symlink_target)
{
  struct procfs_dir_entry *dir_entry;
  struct stat *stat = (struct stat *) malloc (sizeof (struct stat));
  stat->st_mode = S_IFREG;

  dir_entry = update_entries_list (dir, name, stat, 
                                 timestamp, symlink_target);

  return dir_entry;
}

/* Creates files to store process information for DIR 
   whose names are pids and returns these files in *NODE. */
error_t
procfs_create_files (struct procfs_dir *dir, 
                                    struct node **node,
                                    time_t timestamp)
{
  int err;
  char *file_name, *file_path;
  struct procfs_dir_entry *dir_entry;
 
  if (asprintf (&file_name, "%s", "stat") == -1)
    return errno;
  if (asprintf (&file_path, "%s/%s", dir->node->nn->dir_entry->name, "stat") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  if (asprintf (&file_name, "%s", "status") == -1)
    return errno;
  if (asprintf (&file_path, "%s/%s", dir->node->nn->dir_entry->name, "status") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  if (asprintf (&file_name, "%s", "cmdline") == -1)
    return errno;
  if (asprintf (&file_path, "%s/%s", dir->node->nn->dir_entry->name, "cmdline") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);

  if (asprintf (&file_name, "%s", "statm") == -1)
    return errno;
  if (asprintf (&file_path, "%s/%s", dir->node->nn->dir_entry->name, "statm") == -1)
    return errno;
    
  dir_entry = update_pid_entries (dir, file_name, timestamp, NULL);
  err = procfs_create_node (dir_entry, file_path, node);

  free (file_name);
  free (file_path);
  
#if 0
  nodes_list = &node_stat; 
  nodes_list++;
  node = nodes_list;
#endif

  return err;
}

/* Check if the PSTAT_FLAG is set in the corresponding PS
   structure, if not set it and check again and return error
   status accordingly. */
error_t set_field_value (struct proc_stat *ps, int pstat_flag)
{
  error_t err;

  if (! (ps->flags & pstat_flag))
    {
      err = proc_stat_set_flags (ps, pstat_flag);
      if (err)
        return err;

      /* This second check is done since ps.h specifies to
         do so since the previous call would not have set
         the required value. */
      if (! (ps->flags & pstat_flag))
        return EGRATUITOUS;
    }

  return 0;
}

/* Adjusts TIME_VAL structure having Seconds and
   Microseconds into the value in jiffies. The
   value of jiffy is a hack to adjust to what
   procps uses. */
time_t adjust_jiffy_time (time_value_t time_val)
{
  time_t jiffy_time = time_val.seconds * JIFFY_ADJUST;
  jiffy_time += (time_val.microseconds * JIFFY_ADJUST) 
                 / (1000 * 1000);

  return jiffy_time;
}

/* Extract the user and system time for the live threads of 
   the process. This information is directly retrieved from
   MACH since neither libps not proc makes this available. */
error_t get_task_thread_times (task_t task,
           struct task_thread_times_info *live_threads_times)
{
  error_t err;
  size_t tkcount = TASK_THREAD_TIMES_INFO_COUNT;
  
  err = task_info (task, TASK_THREAD_TIMES_INFO,
                  (task_info_t) live_threads_times, &tkcount);
  if (err == MACH_SEND_INVALID_DEST)
    err = ESRCH;
    
  return err;
}

/* Obtains the User Time in UTIME and System Time in STIME from
   MACH directly since this is neither made available by libps 
   nor by proc server. */
error_t get_live_threads_time (struct proc_stat *ps,
                               time_t *utime, time_t *stime)
{
  struct task_thread_times_info live_threads_times;
  error_t err = set_field_value (ps, PSTAT_TASK);
  
  if (! err)
    {
      err = get_task_thread_times (ps->task, &live_threads_times);
      if (! err)
        {
          *utime = adjust_jiffy_time (
               live_threads_times.user_time);
          *stime = adjust_jiffy_time (
               live_threads_times.system_time);
        }   
    }
    
  return err;
}

/* Get the data for stat file into the structure
   PROCFS_STAT. */
error_t get_stat_data (pid_t pid, 
                       struct procfs_stat **procfs_stat)
{
  error_t err;
  struct procfs_stat *new = (struct procfs_stat *)
                    malloc (sizeof (struct procfs_stat));

  struct proc_stat *ps;
  time_t utime, stime;
  
  err = _proc_stat_create (pid, ps_context, &ps);

  new->pid = pid;

  if (! err)
    {
      err = set_field_value (ps, PSTAT_ARGS);
      if (! err)
        asprintf (&new->comm, "%s", ps->args);
    }

  err = set_field_value (ps, PSTAT_STATE);
  if (! err)
    {
      if (ps->state & PSTAT_STATE_P_STOP) 
        new->state = strdup ("T");
      if (ps->state & PSTAT_STATE_P_ZOMBIE)
        new->state = strdup ("Z");
      if (ps->state & PSTAT_STATE_P_FG)
        new->state = strdup ("+");
      if (ps->state & PSTAT_STATE_P_SESSLDR)
        new->state = strdup ("s");
      if (ps->state & PSTAT_STATE_P_LOGINLDR)
        new->state = strdup ("l");
      if (ps->state & PSTAT_STATE_P_FORKED)
        new->state = strdup ("f");
      if (ps->state & PSTAT_STATE_P_NOMSG)
        new->state = strdup ("m");
      if (ps->state & PSTAT_STATE_P_NOPARENT)
        new->state = strdup ("p");
      if (ps->state & PSTAT_STATE_P_ORPHAN)
        new->state = strdup ("o");
      if (ps->state & PSTAT_STATE_P_TRACE)
        new->state = strdup ("x");
      if (ps->state & PSTAT_STATE_P_WAIT)
        new->state = strdup ("w");
      if (ps->state & PSTAT_STATE_P_GETMSG)
        new->state = strdup ("g");     
    }

  err = set_field_value (ps, PSTAT_PROC_INFO);
  if (! err)
    {
      new->ppid = ps->proc_info->ppid;
      new->pgid = ps->proc_info->pgrp;
      new->sid = ps->proc_info->session;
      new->tty_pgrp = ps->proc_info->pgrp;
    }
  else
    {
      new->ppid = 0;
      new->pgid = 0;
      new->sid = 0;
      new->tty_pgrp = 0;
    }

  err = set_field_value (ps, PSTAT_STATE);
  if (! err)
    new->flags = ps->state;
  else
    new->flags = 0;

  err = set_field_value (ps, PSTAT_TASK_EVENTS);
  if (! err)
    {
      new->minflt = ps->task_events_info->faults;
      new->majflt = ps->task_events_info->pageins;
    }
  else 
    {
      new->minflt = 0;
      new->majflt = 0;
    }

  /* This seems to be a bit inconsistent with setting of other
     fields in this code. There are two reasons for this. 
     1. The actual information required is not made available 
        by libps which should be directly obtained from MACH.
     2. The same code which is required to get the information
        have to be reused in procfs_nonpid_files.c */  
  err = get_live_threads_time (ps, &utime, &stime);
  if (! err)
    {
      new->utime = utime;
      new->stime = stime;      
    }
  else
    {
      new->utime = 0;
      new->stime = 0;
    }

  err = set_field_value (ps, PSTAT_TASK_BASIC);
  if (! err)
    {
      new->cutime = adjust_jiffy_time (
           ps->task_basic_info->user_time);
      new->cstime = adjust_jiffy_time (
           ps->task_basic_info->system_time);

      new->priority = ps->task_basic_info->base_priority;
      new->starttime = adjust_jiffy_time ( 
          ps->task_basic_info->creation_time);
              
      new->vsize = ps->task_basic_info->virtual_size;
      new->rss = ps->task_basic_info->resident_size;
    }   
  else
    {
      new->cutime = 0;
      new->cstime = 0;
      new->priority = 0;
      new->starttime = 0; 
      new->vsize = 0;
      new->rss = 0;
    }

  new->nice = getpriority (0, pid);
  
  err = set_field_value (ps, PSTAT_NUM_THREADS);
  if (! err)
    new->num_threads = ps->num_threads;
  else
    new->num_threads = 0;
    
  /* Not Supported in Linux 2.6 or later. */
  new->tty_nr = 0;
  new->itrealvalue = 0;
  new->nswap = 0;
  new->cnswap = 0;
      
  /* Temporarily set to 0 until correct 
     values are found .*/
  new->cminflt = 0;
  new->cmajflt = 0;     
  new->rlim = 0;
  new->startcode = 0;
  new->endcode = 0;
  new->startstack = 0;
  new->kstkesp = 0;
  new->kstkeip = 0;
  new->signal = 0;
  new->blocked = 0;
  new->sigignore = 0;
  new->sigcatch = 0; 
  new->wchan = 0;
  new->exit_signal = 0;
  new->processor = 0;
  new->rt_priority = 0; 
  new->policy = 0; 
  new->delayacct_blkio_ticks = 0;
  
  *procfs_stat = new;
  _proc_stat_free (ps);

  return err;
}

/* Writes required process information to stat file
   within the directory represented by pid. Return
   the data in DATA and actual length to be written
   in LEN. */
error_t
procfs_write_stat_file (struct procfs_dir_entry *dir_entry, 
                        off_t offset, size_t *len, void *data)
{
  error_t err;
  char *stat_data;
  struct procfs_stat *procfs_stat;
  pid_t pid = atoi (dir_entry->dir->node->nn->dir_entry->name);

  err = get_stat_data (pid, &procfs_stat);
  
  if (asprintf (&stat_data, "%d (%s) %s %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d %u %u %llu \n", 
           procfs_stat->pid, procfs_stat->comm, 
           procfs_stat->state, procfs_stat->ppid,
           procfs_stat->pgid, procfs_stat->sid,
           procfs_stat->tty_nr, procfs_stat->tty_pgrp, 
           procfs_stat->flags, procfs_stat->minflt,
           procfs_stat->cminflt, procfs_stat->majflt,
           procfs_stat->cmajflt, procfs_stat->utime,
           procfs_stat->stime, procfs_stat->cutime,
           procfs_stat->cstime, procfs_stat->priority, 
           procfs_stat->nice, procfs_stat->num_threads, 
           procfs_stat->itrealvalue, procfs_stat->starttime, 
           procfs_stat->vsize, BYTES_TO_PAGES(procfs_stat->rss), 
           procfs_stat->rlim, procfs_stat->startcode,
           procfs_stat->endcode, procfs_stat->startstack, 
           procfs_stat->kstkesp, procfs_stat->kstkeip, 
           procfs_stat->signal, procfs_stat->blocked, 
           procfs_stat->sigignore, procfs_stat->sigcatch, 
           procfs_stat->wchan, procfs_stat->nswap, 
           procfs_stat->cnswap, procfs_stat->exit_signal, 
           procfs_stat->processor, procfs_stat->rt_priority, 
           procfs_stat->policy, 
           procfs_stat->delayacct_blkio_ticks) == -1)
    return errno;


  memcpy (data, stat_data, strlen(stat_data));
  *len = strlen (data);
  
  free (stat_data);
  free (procfs_stat);
  
  return err;  
}

/* Writes required process's command line information
   to cmline file within the directory represented by
   pid. Return the data in DATA and actual length to
   be written in LEN. */
error_t
procfs_write_cmdline_file (struct procfs_dir_entry *dir_entry, 
                        off_t offset, size_t *len, void *data)
{	
  char *cmdline_data;
  error_t err;
  struct proc_stat *ps;
  pid_t pid = atoi (dir_entry->dir->node->nn->dir_entry->name);
  err = _proc_stat_create (pid, ps_context, &ps);

  err = set_field_value (ps, PSTAT_ARGS);

  if (! err)
    if (asprintf (&cmdline_data, "%s \n", ps->args) == -1)
      return errno;

  memcpy (data, cmdline_data, strlen(cmdline_data));
  *len = strlen (data);

  _proc_stat_free (ps); 
  free (cmdline_data);
  return err;
}

/* Writes required process's information that is represented by
   stat and statm in a human readable format to status file
   within the directory represented by pid. Return the data
   in DATA and actual length to be written in LEN. */
error_t
procfs_write_status_file (struct procfs_dir_entry *dir_entry, 
                        off_t offset, size_t *len, void *data)
{	
  char *status_data;
  error_t err;
  struct proc_stat *ps;
  struct procfs_stat *procfs_stat;

  pid_t pid = atoi (dir_entry->dir->node->nn->dir_entry->name);
  err = _proc_stat_create (pid, ps_context, &ps);

  err = get_stat_data (pid, &procfs_stat);

  if (! err)
    if (asprintf (&status_data, "Name:\t%s\nState:\t%s\nTgid:\t%d\nPid:\t%d\n", procfs_stat->comm, procfs_stat->state, procfs_stat->pid, procfs_stat->pid) == -1)
      return errno;

  memcpy (data, status_data, strlen(status_data));
  *len = strlen (data);

  _proc_stat_free (ps); 

  free (status_data);
  free (procfs_stat);

  return err;
}

/* Writes required process information to statm file
   within the directory represented by pid. Return
   the data in DATA and actual length to be written
   in LEN. */
error_t
procfs_write_statm_file (struct procfs_dir_entry *dir_entry, 
                        off_t offset, size_t *len, void *data)
{	
  char *statm_data;
  error_t err;
  struct proc_stat *ps;
  struct procfs_stat *procfs_stat;

  pid_t pid = atoi (dir_entry->dir->node->nn->dir_entry->name);
  err = _proc_stat_create (pid, ps_context, &ps);

  err = get_stat_data (pid, &procfs_stat);

  if (! err)
    if (asprintf (&statm_data, "%lu %ld %d %d %d %d %d\n", 
       BYTES_TO_PAGES(procfs_stat->vsize), 
       BYTES_TO_PAGES(procfs_stat->rss),
       0, 0, 0, 0, 0) == -1)
      return errno;

  memcpy (data, statm_data, strlen(statm_data));
  *len = strlen (data);

  _proc_stat_free (ps); 

  free (statm_data);
  free (procfs_stat);

  return err;
}

/* Writes required process information to each of files
   within directory represented by pid, for files specified
   by NODE. Return the data in DATA and actual length of 
   data in LEN. */
error_t 
procfs_write_files_contents (struct node *node,
                 off_t offset, size_t *len, void *data)
{
  error_t err;
  
  if (! strcmp (node->nn->dir_entry->name, "stat"))
    if (! strcmp (node->nn->dir_entry->dir->fs_path, ""))
      err = procfs_write_nonpid_stat (node->nn->dir_entry,
                                      offset, len, data);
    else 
      err = procfs_write_stat_file (node->nn->dir_entry, 
                                     offset, len, data);

  if (! strcmp (node->nn->dir_entry->name, "cmdline"))
      err = procfs_write_cmdline_file (node->nn->dir_entry, 
                                     offset, len, data);

  if (! strcmp (node->nn->dir_entry->name, "status"))
      err = procfs_write_status_file (node->nn->dir_entry, 
                                     offset, len, data);
                                     
  if (! strcmp (node->nn->dir_entry->name, "statm"))
      err = procfs_write_statm_file (node->nn->dir_entry, 
                                     offset, len, data);
                                                                          
  if (! strcmp (node->nn->dir_entry->name, "meminfo"))
    if (! strcmp (node->nn->dir_entry->dir->fs_path, ""))
      err = procfs_write_nonpid_meminfo (node->nn->dir_entry,
                                      offset, len, data); 
    else
      err = ENOENT;
      
  if (! strcmp (node->nn->dir_entry->name, "loadavg"))
    if (! strcmp (node->nn->dir_entry->dir->fs_path, ""))
      err = procfs_write_nonpid_loadavg (node->nn->dir_entry,
                                      offset, len, data);
    else 
      err = ENOENT;  
      
  if (! strcmp (node->nn->dir_entry->name, "uptime"))
    if (! strcmp (node->nn->dir_entry->dir->fs_path, ""))
      err = procfs_write_nonpid_uptime (node->nn->dir_entry,
                                      offset, len, data);
    else 
      err = ENOENT;                                                                    
    
  return err;
}
