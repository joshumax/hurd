/* 
   Copyright (C) 1994 Free Software Foundation

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* return a string describing the amount of memory SIZE represents. */
char *
mem_str (vm_size_t size)
{
  char *ret = malloc (10);
  char *spec=" KMG";
  int dec = 0;

  while (size > 1000)
    {
      dec = size % 1000;
      size /= 1000;
      spec++;
    }
  
  if (size >= 100)
    sprintf (ret, "%d%c", size, *spec);
  else if (size >= 10)
    sprintf (ret, "%d.%d%c", size, dec / 100, *spec);
  else 
    sprintf (ret, "%d.%d%c", size, dec / 10, *spec);

  return ret;
}

/* Return a string representing time T. */
char *
time_str (time_value_t *t)
{
  char *ret = malloc (20);
  int centiseconds;

  if (t->microseconds >= 1000000)
    {
      t->seconds += t->microseconds / 1000000;
      t->microseconds %= 1000000;
    }
  centiseconds = t->microseconds / (1000000 / 100);
  
  sprintf (ret, "%d:%02d.%02d",
	   t->seconds / 60,	/* minutes */
	   t->seconds % 60,	/* seconds */
	   centiseconds);
  return ret;
}

/* Print a string describing the args of proc PID */
void
print_args_str (process_t proc, pid_t pid)
{
  char *args;
  u_int nargs = 0;
  char *p;
  error_t err;

  err = proc_getprocargs (proc, pid, &args, &nargs);
  if (err)
    return;
  p = args;
  while (p - args < nargs)
    {
      printf ("%s ", p);
      p = strchr (p, '\0') + 1;
    }
  vm_deallocate (mach_task_self (), (vm_address_t) args, nargs);
}
  
/* Very simple PS */
int
main ()
{
  process_t proc;
  pid_t pids[20];
  pid_t *pp = pids;
  u_int npids = 20;
  int ind;
  struct thread_basic_info tbi;
  struct thread_sched_info tsi;

#if 0
  stdout = mach_open_devstream (getdport (1), "w");
#endif

  puts ("PID\tUSER\tPP\tPG\tSS\tThds\tVMem\tRSS\tPRI\t%CPU\tUser\tSystem\tArgs");
  proc = getproc ();
  proc_getallpids (proc, &pp, &npids);
  for (ind = 0; ind < npids; ind++)
    {
      int procinfobuf[0];
      struct procinfo *pi = (struct procinfo *) procinfobuf;
      u_int pisize = 0;
      int i;

      proc_getprocinfo (proc, pp[ind], (int **)&pi, &pisize);

      if (pi->state & PI_NOPARENT)
	continue;
      
      bzero (&tbi, sizeof tbi);
      bzero (&tsi, sizeof tsi);
      for (i = 0; i < pi->nthreads; i++)
	{
	  tsi.base_priority += pi->threadinfos[i].pis_si.base_priority;
	  tsi.cur_priority += pi->threadinfos[i].pis_si.cur_priority;
	  tbi.cpu_usage += pi->threadinfos[i].pis_bi.cpu_usage;
	  tbi.user_time.seconds += pi->threadinfos[i].pis_bi.user_time.seconds;
	  tbi.user_time.microseconds
	    += pi->threadinfos[i].pis_bi.user_time.microseconds;
	  tbi.system_time.seconds
	    += pi->threadinfos[i].pis_bi.system_time.seconds;
	  tbi.system_time.microseconds
	    += pi->threadinfos[i].pis_bi.system_time.microseconds;
	}
      tsi.base_priority /= pi->nthreads;
      tsi.cur_priority /= pi->nthreads;
      tbi.user_time.seconds += tbi.user_time.microseconds / 1000000;
      tbi.user_time.microseconds %= 1000000;
      tbi.system_time.seconds += tbi.system_time.microseconds / 1000000;
      tbi.system_time.microseconds %= 1000000;
      printf ("%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d/%d\t%d\t%s\t%s\t",
	      pp[ind], 
	      (pi->state & PI_NOTOWNED) ? -1 : pi->owner,
	      pi->ppid,
	      pi->pgrp,
	      pi->session,
	      pi->nthreads,
	      mem_str (pi->taskinfo.virtual_size),
	      mem_str (pi->taskinfo.resident_size),
	      tsi.base_priority,
	      tsi.cur_priority,
	      tbi.cpu_usage,
	      time_str (&tbi.user_time),
	      time_str (&tbi.system_time));
      print_args_str (proc, pp[ind]);
      putchar ('\n');
    }
  return 0;
}
