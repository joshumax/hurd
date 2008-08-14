/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   procfs_pid.h -- This is the header file of which contains defintions
                   for structure of directory with PID as the name and
                   structure of each file in this directory.
               
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
*/

#ifndef __PROCFS_PID_H__
#define __PROCFS_PID_H__

struct procfs_pid_files
{
  struct procfs_cwd *procfs_cwd;
  struct procfs_environ *procfs_environ;
  struct procfs_cpu *procfs_cpu;
  struct procfs_root *procfs_root;
  struct procfs_exe *procfs_exe;
  struct procfs_stat *_procfs_stat;
  struct procfs_statm *procfs_statm;
};

struct procfs_stat
{
  pid_t pid;
  char *comm;
  char *state;
  pid_t ppid;
  pid_t pgid;
  pid_t sid;
  int tty_nr;
  pid_t tty_pgrp;
  unsigned flags;	
  long unsigned minflt;
  long unsigned cminflt;
  long unsigned majflt;
  long unsigned cmajflt;
  time_t utime;
  time_t stime;
  time_t cutime;
  time_t cstime;
  long priority;
  long nice;
  long num_threads;
  long itrealvalue;
  long long unsigned starttime;
  long unsigned vsize;
  long rss;
  long unsigned rlim;
  long unsigned startcode;
  long unsigned endcode;
  long unsigned startstack;
  long unsigned kstkesp;
  long unsigned kstkeip;
  long unsigned signal;
  long unsigned blocked;
  long unsigned sigignore;
  long unsigned sigcatch;
  long unsigned wchan;
  long unsigned nswap;
  long unsigned cnswap;
  int exit_signal;
  int processor;
  unsigned rt_priority;
  unsigned policy;
  long long unsigned delayacct_blkio_ticks;
};

#endif
