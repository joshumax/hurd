/* Proc server host management calls
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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

#include <mach.h>
#include <sys/types.h>
#include <hurd/hurd_types.h>
#include <stdlib.h>
#include <errno.h>
#include <mach/notify.h>
#include <string.h>
#include <hurd/exec.h>

#include "proc.h"
#include "proc_S.h"

static long hostid;
static char *hostname;
static int hostnamelen;
static mach_port_t *std_port_array;
static int *std_int_array;
static int n_std_ports, n_std_ints;

struct execdata_notify 
{
  mach_port_t notify_port;
  struct execdata_notify *next;
} *execdata_notifys;

/* Implement proc_sethostid as described in <hurd/proc.defs>. */
error_t
S_proc_sethostid (struct proc *p,
		int newhostid)
{
  if (! check_uid (p, 0))
    return EPERM;
  
  hostid = newhostid;

  return 0;
}

/* Implement proc_gethostid as described in <hurd/proc.defs>. */
error_t 
S_proc_gethostid (struct proc *p,
		int *outhostid)
{
  *outhostid = hostid;
  return 0;
}

/* Implement proc_sethostname as described in <hurd/proc.defs>. */
error_t
S_proc_sethostname (struct proc *p,
		  char *newhostname,
		  u_int newhostnamelen)
{
  if (! check_uid (p, 0))
    return EPERM;
  
  if (hostname)
    free (hostname);

  hostname = malloc (newhostnamelen + 1);
  hostnamelen = newhostnamelen;

  bcopy (newhostname, hostname, newhostnamelen);
  hostname[newhostnamelen] = '\0';

  return 0;
}

/* Implement proc_gethostname as described in <hurd/proc.defs>. */
error_t
S_proc_gethostname (struct proc *p,
		  char **outhostname,
		  u_int *outhostnamelen)
{
  if (*outhostnamelen > hostnamelen + 1)
    vm_allocate (mach_task_self (), (vm_address_t *)outhostname,
		 hostnamelen, 1);
  *outhostnamelen = hostnamelen + 1;
  bcopy (hostname, *outhostname, hostnamelen + 1);
  return 0;
}

/* Implement proc_getprivports as described in <hurd/proc.defs>. */
error_t
S_proc_getprivports (struct proc *p,
		   mach_port_t *hostpriv,
		   mach_port_t *devpriv)
{
  if (! check_uid (p, 0))
    return EPERM;
  
  *hostpriv = master_host_port;
  *devpriv = master_device_port;
  return 0;
}

/* Implement proc_setexecdata as described in <hurd/proc.defs>. */
error_t
S_proc_setexecdata (struct proc *p,
		  mach_port_t *ports,
		  u_int nports,
		  int *ints,
		  u_int nints)
{
  int i;
  struct execdata_notify *n;
  
  if (!check_uid (p, 0))
    return EPERM;
  
  if (std_port_array)
    {
      for (i = 0; i < n_std_ports; i++)
	mach_port_deallocate (mach_task_self (), std_port_array[i]);
      free (std_port_array);
    }
  if (std_int_array)
    free (std_int_array);
  
  std_port_array = malloc (sizeof (mach_port_t) * nports);
  n_std_ports = nports;
  bcopy (ports, std_port_array, sizeof (mach_port_t) * nports);
  
  std_int_array = malloc (sizeof (int) * nints);
  n_std_ints = nints;
  bcopy (ints, std_int_array, sizeof (int) * nints);
  
  for (n = execdata_notifys; n; n = n->next)
    exec_setexecdata (n->notify_port, std_port_array, MACH_MSG_TYPE_COPY_SEND,
		      n_std_ports, std_int_array, n_std_ints);
      
  return 0;
}

/* Implement proc_getexecdata as described in <hurd/proc.defs>. */
error_t 
S_proc_getexecdata (struct proc *p,
		  mach_port_t **ports,
		  mach_msg_type_name_t *portspoly,
		  u_int *nports,
		  int **ints,
		  u_int *nints)
{
  /* XXX memory leak here */

  if (*nports < n_std_ports)
    *ports = malloc (n_std_ports * sizeof (mach_port_t));
  bcopy (std_port_array, *ports, n_std_ports * sizeof (mach_port_t));
  *nports = n_std_ports;
  
  if (*nints < n_std_ints)
    *ints = malloc (n_std_ints * sizeof (mach_port_t));
  bcopy (std_int_array, *ints, n_std_ints * sizeof (int));
  *nints = n_std_ints;

  return 0;
}

/* Implement proc_execdata_notify as described in <hurd/proc.defs>. */
error_t
S_proc_execdata_notify (struct proc *p,
		      mach_port_t notify)
{
  struct execdata_notify *n = malloc (sizeof (struct execdata_notify));
  mach_port_t foo;

  n->notify_port = notify;
  n->next = execdata_notifys;
  execdata_notifys = n;

  mach_port_request_notification (mach_task_self (), notify, 
				  MACH_NOTIFY_DEAD_NAME, 1,
				  generic_port, MACH_MSG_TYPE_MAKE_SEND_ONCE,
				  &foo);

  if (foo)
    mach_port_deallocate (mach_task_self (), foo);
  
  exec_setexecdata (n->notify_port, std_port_array, MACH_MSG_TYPE_COPY_SEND, 
		    n_std_ports, std_int_array, n_std_ints);
  return 0;
}

