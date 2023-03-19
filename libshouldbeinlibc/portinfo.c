/* Print information about a task's ports

   Copyright (C) 1996,98,99,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

#include <sys/types.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include <mach/default_pager.h>
#include <hurd/fsys.h>
#include <hurd/msg.h>
#include <hurd/pci.h>
#include <hurd/process.h>
#include <hurd/socket.h>
#include <hurd/term.h>
#include <hurd/tioctl.h>
#include <hurd.h>

#include "portinfo.h"

/* Prints info about NAME in TASK to STREAM, in a way described by the flags
   in SHOW.  If TYPE is non-zero, it should be what mach_port_type returns
   for NAME.  */
error_t
print_port_info (mach_port_t name, mach_port_type_t type, task_t task,
		 unsigned show, FILE *stream)
{
  int hex_names = (show & PORTINFO_HEX_NAMES);
  int first = 1, subfirst = 1;
  void comma (void)
    {
      if (first)
	first = 0;
      else
	fprintf (stream, ", ");
    }
  void subcomma (void)
    {
      if (subfirst)
	subfirst = 0;
      else
	fprintf (stream, ",");
    }
  void prefs (mach_port_right_t right)
    {
      mach_port_urefs_t refs;
      error_t err = mach_port_get_refs (task, name, right, &refs);
      if (! err)
	fprintf (stream, " (refs: %zu)", refs);
    }

  if (type == 0)
    {
      error_t err = mach_port_type (task, name, &type);
      if (err)
	return err;
    }

  fprintf (stream, hex_names ? "%#6x: " : "%6u: ", name);

  if (type & MACH_PORT_TYPE_RECEIVE)
    {
      comma ();
      fprintf (stream, "receive");
    }
  if (type & MACH_PORT_TYPE_SEND)
    {
      comma ();
      fprintf (stream, "send");
    }
  if (type & MACH_PORT_TYPE_SEND_ONCE)
    {
      comma ();
      fprintf (stream, "send-once");
    }
  if (type & MACH_PORT_TYPE_DEAD_NAME)
    {
      comma ();
      fprintf (stream, "dead-name");
    }
  if (type & MACH_PORT_TYPE_PORT_SET)
    {
      comma ();
      fprintf (stream, "port-set");
    }

  if (show & PORTINFO_DETAILS)
    {
      error_t err;
      mach_port_t port;
      mach_msg_type_name_t acquired_type;

      /* Get the port itself.  */
      err = mach_port_extract_right (task, name,
				     (type & MACH_PORT_TYPE_RECEIVE
				      ? MACH_MSG_TYPE_MAKE_SEND
				      : MACH_MSG_TYPE_COPY_SEND),
				     &port, &acquired_type);

      if (!err)
	{
	  process_t proc = getproc ();
	  mach_port_t msgport;
	  pid_t pid;

	  if (port == MACH_PORT_DEAD)
	    err = EIEIO;
	  else
	    err = proc_task2pid (proc, task, &pid);
	  if (!err)
	    err = proc_getmsgport (proc, pid, &msgport);
	  if (!err)
	    {
	      /* Check if it is installed as an FD.  */
	      mach_port_t *dtable = NULL;
	      mach_msg_type_number_t ndtable = 0;
	      unsigned i;

	      msg_get_dtable (msgport, task, &dtable, &ndtable);

	      for (i = 0; i < ndtable; i++)
		{
		  if (dtable[i] == port)
		    fprintf (stream, " fd(%d)", i);
		  mach_port_deallocate (mach_task_self (), dtable[i]);
		}
	      vm_deallocate (mach_task_self (), (vm_address_t) dtable, ndtable * sizeof (*dtable));

	      /* First check for init ports.  */
	      void check_init_port (int num, const char *name)
		{
		  mach_port_t init_port;

		  err = msg_get_init_port (msgport, task, num, &init_port);
		  if (!err)
		    {
		      if (port == init_port)
			fprintf (stream, " %s", name);
		      mach_port_deallocate (mach_task_self (), init_port);
		    }
		}

	      check_init_port (INIT_PORT_CWDIR, "CWDIR");
	      check_init_port (INIT_PORT_CRDIR, "CRDIR");
	      check_init_port (INIT_PORT_AUTH, "AUTH");
	      check_init_port (INIT_PORT_PROC, "PROC");
	      check_init_port (INIT_PORT_CTTYID, "CTTYID");
	      check_init_port (INIT_PORT_BOOTSTRAP, "BOOTSTRAP");

	      mach_port_deallocate (mach_task_self (), msgport);
	    }

	  /* Then try to use the ports to see what they are.  */
	  if (type & MACH_PORT_TYPE_RECEIVE)
	    {
	      if (port == msgport)
		fprintf (stream, " msg");
	    }
	  else if (type & MACH_PORT_TYPE_SEND)
	    {
	      /* auth_t */
	      {
		uid_t *euids = 0, *auids = 0, *egids = 0, *agids = 0;
		mach_msg_type_number_t neuids = 0, nauids = 0, negids = 0, nagids = 0, i;

		err = auth_getids (port,
				   &euids, &neuids,
				   &auids, &nauids,
				   &egids, &negids,
				   &agids, &nagids);

		if (!err)
		  {
		    fprintf (stream, " auth([");
		    subfirst = 1;
		    for (i = 0; i < neuids; i++)
		      {
			subcomma ();
			fprintf (stream, "%d", euids[i]);
		      }
		    fprintf (stream, "],[");
		    subfirst = 1;
		    for (i = 0; i < nauids; i++)
		      {
			subcomma ();
			fprintf (stream, "%d", auids[i]);
		      }
		    fprintf (stream, "],[");
		    subfirst = 1;
		    for (i = 0; i < negids; i++)
		      {
			subcomma ();
			fprintf (stream, "%d", egids[i]);
		      }
		    fprintf (stream, "],[");
		    subfirst = 1;
		    for (i = 0; i < nagids; i++)
		      {
			subcomma ();
			fprintf (stream, "%d", agids[i]);
		      }
		    fprintf (stream, "])");
		    munmap (euids, neuids * sizeof (*euids));
		    munmap (auids, nauids * sizeof (*auids));
		    munmap (egids, negids * sizeof (*egids));
		    munmap (agids, nagids * sizeof (*agids));
		  }
	      }

	      /* file_t */
	      {
		int allowed;

		err = file_check_access (port, &allowed);

		if (!err)
		  {
		    int printbar = 0;
		    void bar (void)
		      {
			if (printbar)
			  fprintf (stream, "|");
			else
			  printbar = 1;
		      }

		    fprintf (stream, " file(");
		    if (allowed & O_READ)
		      {
			bar ();
			fprintf (stream, "READ");
		      }
		    if (allowed & O_WRITE)
		      {
			bar ();
			fprintf (stream, "WRITE");
		      }
		    if (allowed & O_EXEC)
		      {
			bar ();
			fprintf (stream, "EXEC");
		      }
		    fprintf (stream, ")");
		  }
	      }

	      /* fsys_t */
	      {
		string_t source;

		err = fsys_get_source (port, source);

		if (!err)
		  fprintf (stream, " fsys(%s)", source);
	      }

	      /* io_t */
	      {
		struct stat st;

		err = io_stat (port, &st);

		if (!err)
		  fprintf (stream, " io(%llu,%llu)",
			   (unsigned long long) st.st_ino,
			   (unsigned long long) st.st_dev);
	      }

	      /* msg */
	      {
		mach_port_t proc;

		err = msg_get_init_port (port, task, INIT_PORT_PROC, &proc);
		if (!err)
		  {
		    pid_t pid, ppid;
		    int orphaned;

		    fprintf (stream, " msg");
		    err = proc_getpids (port, &pid, &ppid, &orphaned);
		    if (!err)
		      fprintf (stream, "(%d)", pid);

		    mach_port_deallocate (mach_task_self (), proc);
		  }
	      }

	      /* pci_t */
	      {
		vm_size_t ndevs;

		err = pci_get_ndevs (port, &ndevs);

		if (!err)
		  fprintf (stream, " pci(%d)", ndevs);
	      }

	      /* process_t */
	      {
		pid_t pid, ppid;
		int orphaned;

		err = proc_getpids (port, &pid, &ppid, &orphaned);

		if (!err)
		  fprintf (stream, " proc(%d)", pid);
	      }

	      /* socket_t */
	      {
		mach_port_t addr = MACH_PORT_NULL;
		int type;
		struct sockaddr_storage sa;
		char *psa = (char*) &sa;
		mach_msg_type_number_t salen = sizeof (sa);

		err = socket_name (port, &addr);

		if (!err)
		  fprintf (stream, " socket");
		else
		  addr = port;

		err = socket_whatis_address (addr, &type, &psa, &salen);
		if (!err)
		  {
		    if (addr == port)
		      fprintf (stream, " addr_port");

		    if (type == AF_UNIX)
		      {
			struct sockaddr_un *sa_un = (struct sockaddr_un *) psa;

			fprintf (stream, "(UNIX:%s)", sa_un->sun_path);
		      }
		    else if (type == AF_INET)
		      {
			struct sockaddr_in *sa_in = (struct sockaddr_in *) psa;

			fprintf (stream, "(INET:%s:%d)",
				 inet_ntoa (sa_in->sin_addr),
				 ntohs (sa_in->sin_port));
		      }
		    else if (type == AF_INET6)
		      {
			struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *) psa;
			char buf[INET6_ADDRSTRLEN];
			const char *s;

			s = inet_ntop (type, &sa_in6->sin6_addr, buf, sizeof (buf));
			fprintf (stream, "(INET6:%s:%d)", s,
				 ntohs (sa_in6->sin6_port));
		      }
		    else
		      fprintf (stream, "(%d)", type);
		  }
		if (psa != (char*) &sa)
		  vm_deallocate (mach_task_self (), (vm_address_t) psa, salen);

		if (addr != port)
		  mach_port_deallocate (mach_task_self (), addr);
	      }

	      /* terminal */
	      {
		string_t name;

		err = term_get_nodename (port, name);

		if (!err)
		  fprintf (stream, " term(%s)", name);
	      }

	      /* default pager */
	      {
		struct default_pager_info def_pager_info;

		err = default_pager_info (port, &def_pager_info);

		if (!err)
		  fprintf (stream, " default_pager(%llu)",
		      (unsigned long long) def_pager_info.dpi_total_space);
	      }

	      /* host_t */
	      {
		struct host_basic_info info;
		mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;

		err = host_info (port, HOST_BASIC_INFO,
		    (host_info_t) &info, &count);

		if (!err)
		  fprintf (stream, " host");
		if (port == mach_host_self ())
		  fprintf (stream, "(self)");
	      }

	      /* memory_object */
	      {
		boolean_t ready, may_cache;
		memory_object_copy_strategy_t strategy;

		err = memory_object_get_attributes (port, &ready, &may_cache, &strategy);

		if (!err)
		  fprintf (stream, " memory_object");
	      }

	      /* processor_t */
	      {
		struct processor_basic_info info;
		host_t host;
		mach_msg_type_number_t count = PROCESSOR_BASIC_INFO_COUNT;

		err = processor_info (port, PROCESSOR_BASIC_INFO,
		    &host, (processor_info_t) &info, &count);

		if (!err)
		  {
		    fprintf (stream, " processor(%d)", info.slot_num);
		    mach_port_deallocate (mach_task_self (), host);
		  }
	      }

	      /* task_t */
	      {
		pid_t pid;

		err = proc_task2pid (proc, port, &pid);
		if (!err)
		  fprintf (stream, " task(%d)", pid);
		if (port == task)
		  {
		    if (err)
		      fprintf (stream, " task");
		    fprintf (stream, "(self)");
		  }
	      }

	      /* thread_t */
	      {
		struct thread_basic_info info;
		mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;

		err = thread_info (port, THREAD_BASIC_INFO,
				   (thread_info_t) &info, &count);

		if (!err)
		  fprintf (stream, " thread");
	      }
	    }

	  mach_port_deallocate (mach_task_self (), proc);
	  mach_port_deallocate (mach_task_self (), port);
	}

      if (type & MACH_PORT_TYPE_RECEIVE)
	{
	  struct mach_port_status status;
	  error_t err = mach_port_get_receive_status (task, name, &status);
	  if (! err)
	    {
	      fprintf (stream, " (");
	      if (status.mps_pset != MACH_PORT_NULL)
		fprintf (stream,
			 hex_names ? "port-set: %#x, " : "port-set: %u, ",
			 status.mps_pset);
	      fprintf (stream, "seqno: %zu", status.mps_seqno);
	      if (status.mps_mscount)
		fprintf (stream, ", ms-count: %zu", status.mps_mscount);
	      if (status.mps_qlimit != MACH_PORT_QLIMIT_DEFAULT)
		fprintf (stream, ", qlimit: %zu", status.mps_qlimit);
	      if (status.mps_msgcount)
		fprintf (stream, ", msgs: %zu", status.mps_msgcount);
	      fprintf (stream, "%s%s%s)",
		       status.mps_srights ? ", send-rights" : "",
		       status.mps_pdrequest ? ", pd-req" : "",
		       status.mps_nsrequest ? ", ns-req" : "");
	    }
	}

      if (type & MACH_PORT_TYPE_SEND)
	prefs (MACH_PORT_RIGHT_SEND);

      if (type & MACH_PORT_TYPE_DEAD_NAME)
	prefs (MACH_PORT_RIGHT_DEAD_NAME);

      if (type & MACH_PORT_TYPE_PORT_SET)
	{
	  mach_port_t *members = 0;
	  mach_msg_type_number_t members_len = 0, i;
	  error_t err =
	    mach_port_get_set_status (task, name, &members, &members_len);
	  if (! err)
	    {
	      if (members_len == 0)
		fprintf (stream, " (empty)");
	      else
		{
		  fprintf (stream, hex_names ? " (%#x" : " (%u", members[0]);
		  for (i = 1; i < members_len; i++)
		    fprintf (stream, hex_names ? ", %#x" : ", %u",
			     members[i]);
		  fprintf (stream, ")");
		  munmap ((caddr_t) members, members_len * sizeof *members);
		}
	    }
	}
    }

  putc ('\n', stream);

  return 0;
}

/* Prints info about every port in TASK that has a type in ONLY to STREAM. */
error_t
print_task_ports_info (task_t task, mach_port_type_t only,
		       unsigned show, FILE *stream)
{
  mach_port_t *names = 0;
  mach_port_type_t *types = 0;
  mach_msg_type_number_t names_len = 0, types_len = 0, i;
  error_t err = mach_port_names (task, &names, &names_len, &types, &types_len);

  if (err)
    return err;

  for (i = 0; i < names_len; i++)
    if (types[i] & only)
      print_port_info (names[i], types[i], task, show, stream);

  munmap ((caddr_t) names, names_len * sizeof *names);
  munmap ((caddr_t) types, types_len * sizeof *types);

  return 0;
}
