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
#include <stdio.h>
#include <hurd/exec.h>

#define HURD_VERSION_DEFINE
#include <hurd/version.h>

#include "proc.h"
#include "proc_S.h"

static long hostid;
static char *hostname;
static int hostnamelen;
static mach_port_t *std_port_array;
static int *std_int_array;
static int n_std_ports, n_std_ints;
static struct utsname uname_info;
static char *machversion;

struct server_version
{
  char *name;
  char *version;
  char *release;
} *server_versions;
int nserver_versions, server_versions_nalloc;



struct execdata_notify 
{
  mach_port_t notify_port;
  struct execdata_notify *next;
} *execdata_notifys;

/* Implement proc_sethostid as described in <hurd/proc.defs>. */
kern_return_t
S_proc_sethostid (struct proc *p,
		int newhostid)
{
  if (! check_uid (p, 0))
    return EPERM;
  
  hostid = newhostid;

  return 0;
}

/* Implement proc_gethostid as described in <hurd/proc.defs>. */
kern_return_t 
S_proc_gethostid (struct proc *p,
		int *outhostid)
{
  *outhostid = hostid;
  return 0;
}

/* Implement proc_sethostname as described in <hurd/proc.defs>. */
kern_return_t
S_proc_sethostname (struct proc *p,
		  char *newhostname,
		  u_int newhostnamelen)
{
  int len;
  if (! check_uid (p, 0))
    return EPERM;
  
  if (hostname)
    free (hostname);

  hostname = malloc (newhostnamelen + 1);
  hostnamelen = newhostnamelen;

  bcopy (newhostname, hostname, newhostnamelen);
  hostname[newhostnamelen] = '\0';

  len = newhostnamelen + 1;
  if (len > sizeof uname_info.nodename)
    len = sizeof uname_info.nodename;
  bcopy (hostname, uname_info.nodename, len);
  uname_info.nodename[sizeof uname_info.nodename] = '\0';

  return 0;
}

/* Implement proc_gethostname as described in <hurd/proc.defs>. */
kern_return_t
S_proc_gethostname (struct proc *p,
		  char **outhostname,
		  u_int *outhostnamelen)
{
  if (*outhostnamelen < hostnamelen + 1)
    vm_allocate (mach_task_self (), (vm_address_t *)outhostname,
		 hostnamelen + 1, 1);
  *outhostnamelen = hostnamelen + 1;
  if (hostname)
    bcopy (hostname, *outhostname, hostnamelen + 1);
  else
    **outhostname = '\0';
  return 0;
}

/* Implement proc_getprivports as described in <hurd/proc.defs>. */
kern_return_t
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
kern_return_t
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
kern_return_t 
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
kern_return_t
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

/* Version information handling.

   A server registers its name, and version with
   startup_register_version.  Each release of the Hurd is defined by
   some set of versions for the programs making up the Hurd (found in
   <hurd/version.h>).  If the server being being registered matches its
   entry in that file, then the register request is ignored.

   These are then element of `struct utsname', returned by uname.  The
   release is compared against `hurd_release', as compiled into init.
   If it matches, it is omitted.  A typical version string for the
   system might be:  
  
   "GNU Hurd Version 0.0 [Mach 3.0 VERSION(MK75)] auth 2.6" 
   Which indicates that the Hurd servers are all from Hurd 0.0 except
   for auth, which is auth version 2.6. 

   The version for the Hurd itself comes from <hurd/hurd_types.h> and
   is compiled into proc. */

/* Rebuild the uname version string. */
static void
rebuild_uname ()
{
  int i, j;
  int nmatches, greatestmatch;
  char *hurdrelease;
  struct hurd_version *runninghurd;
  char *p = uname_info.version;
  char *end = &uname_info.version[sizeof uname_info.version];

  /* Tell if the SERVER was distributed in HURD. */
  inline int version_matches (struct server_version *server,
			      struct hurd_version *hurd)
    {
      int i;
      if (strcmp (server->release, hurd->hurdrelease))
	return 0;
      for (i = 0; i < hurd->nservers; i++)
	if (!strcmp (server->name, hurd->vers[i].name)
	    && !strcmp (server->version, hurd->vers[i].version))
	  return 1;
      return 0;
    }

  /* Add STR to uname_info.version. */
  inline void addstr (char *string)
    {
      if (p < end)
	{
	  size_t len = strlen (string);
	  if (end - 1 - p < len)
	    memcpy (p, string, len);
	  p += len;
	}
    }

  /* Look through the hurd_versions array and find the spec which matches
     the most releases of our versions; this will define the release. */
  greatestmatch = 0;
  for (i = 0; i < nhurd_versions; i++)
    {
      nmatches = 0;
      for (j = 0; j < nserver_versions; j++)
	if (!strcmp (hurd_versions[i].hurdrelease, server_versions[j].release))
	  nmatches++;
      if (nmatches >= greatestmatch)
	{
	  greatestmatch = nmatches;
	  hurdrelease = hurd_versions[i].hurdrelease;
	}
    }
  
  /* Now try and figure out which of the hurd versions that is this release
     we should deem the version; again, base it on which has the greatest
     number of matches among running servers. */
  greatestmatch = 0;
  for (i = 0; i < nhurd_versions; i++)
    {
      if (strcmp (hurd_versions[i].hurdrelease, hurdrelease))
	continue;
      nmatches = 0;
      for (j = 0; j < nserver_versions; j++)
	if (version_matches (&server_versions[j], &hurd_versions[i]))
	  nmatches++;
      if (nmatches >= greatestmatch)
	{
	  greatestmatch = nmatches;
	  runninghurd = &hurd_versions[i];
	}
    }
  
  /* Now build the uname strings.  The uname "release" looks like this:
     GNU Hurd Release N.NN (kernel-version) 
     */
  sprintf (uname_info.release, "GNU Hurd Release %s (%s)",
	   runninghurd->hurdrelease, machversion);
  
  /* The uname "version" looks like this:
     GNU Hurd Release N.NN, Version N.NN (kernel-version)
     followed by a spec for each server that does not match the one
     in runninghurd in the form "(ufs N.NN)" or the form 
     "(ufs N.NN [M.MM])"; N.NN is the version of the server and M.MM
     is the Hurd release it expects. */
  addstr ("GNU Hurd Release ");
  addstr (runninghurd->hurdrelease);
  addstr (", Version ");
  addstr (runninghurd->hurdversion);
  addstr (" (");
  addstr (machversion);
  addstr (")");
  
  for (i = 0; i < nserver_versions; i++)
    if (!version_matches (&server_versions[i], runninghurd))
      {
	addstr ("; (");
	addstr (server_versions[i].name);
	addstr (" ");
	addstr (server_versions[i].version);
	if (!strcmp (server_versions[i].release, runninghurd->hurdrelease))
	  {
	    addstr (" [");
	    addstr (server_versions[i].release);
	    addstr ("]");
	  }
	addstr (")");
      }
  
  *p = '\0';			/* Null terminate uname_info.version */
}

      
void
initialize_version_info (void)
{
  extern const char *const mach_cpu_types[];
  extern const char *const mach_cpu_subtypes[][32];
  kernel_version_t kernel_version;
  char *p;
  struct host_basic_info info;
  unsigned int n = sizeof info;
  
  /* Fill in fixed slots sysname and machine. */
  strcpy (uname_info.sysname, "GNU");

  host_info (mach_host_self (), HOST_BASIC_INFO, (int *) &info, &n);
  sprintf (uname_info.machine, "%s %s",
	   mach_cpu_types[info.cpu_type],
	   mach_cpu_subtypes[info.cpu_type][info.cpu_subtype]);
  
  /* Construct machversion for use in release and version strings. */
  host_kernel_version (mach_host_self (), kernel_version);
  p = index (kernel_version, ':');
  if (p)
    *p = '\0';
  machversion = strdup (kernel_version);
  
  /* Notice our own version and initialize server version varables. */
  server_versions = malloc (sizeof (struct server_version) * 10);
  server_versions_nalloc = 10;
  nserver_versions = 1;
  server_versions->name = malloc (sizeof OUR_SERVER_NAME);
  server_versions->release = malloc (sizeof HURD_RELEASE);
  server_versions->version = malloc (sizeof OUR_VERSION);
  strcpy (server_versions->name, OUR_SERVER_NAME);
  strcpy (server_versions->release, HURD_RELEASE);
  strcpy (server_versions->version, OUR_VERSION);
  
  rebuild_uname ();
  
  uname_info.nodename[0] = '\0';
}

kern_return_t
S_proc_uname (pstruct_t process,
	      struct utsname *uname)
{
  *uname = uname_info;
  return 0;
}

kern_return_t
S_proc_register_version (pstruct_t server,
			 mach_port_t credential,
			 char *name,
			 char *release, 
			 char *version)
{
  int i;

  if (credential != master_host_port)
    /* Must be privileged to register for uname. */
    return EPERM;
  
  for (i = 0; i < nserver_versions; i++)
    if (!strcmp (name, server_versions[i].name))
      {
	/* Change this entry */
	free (server_versions[i].version);
	free (server_versions[i].release);
	server_versions[i].version = malloc (strlen (version) + 1);
	server_versions[i].release = malloc (strlen (version) + 1);
	strcpy (server_versions[i].version, version);
	strcpy (server_versions[i].release, release);
	break;
      }
  if (i == nserver_versions)
    {
      /* Didn't find it; extend. */
      if (nserver_versions == server_versions_nalloc)
	{
	  server_versions_nalloc *= 2;
	  server_versions = realloc (server_versions,
				     sizeof (struct server_version) *
				     server_versions_nalloc);
	}
      server_versions[nserver_versions].name = malloc (strlen (name) + 1);
      server_versions[nserver_versions].version = malloc (strlen (version) 
							  + 1);
      server_versions[nserver_versions].release = malloc (strlen (release)
							  + 1);
      strcpy (server_versions[nserver_versions].name, name);
      strcpy (server_versions[nserver_versions].version, version);
      strcpy (server_versions[nserver_versions].release, release);
      nserver_versions++;
    }
  
  rebuild_uname ();
  mach_port_deallocate (mach_task_self (), credential);
  return 0;
}
