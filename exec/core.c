/* GNU Hurd standard core server.
   Copyright (C) 1992, 1999 Free Software Foundation, Inc.
   Written by Roland McGrath.

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

#include <hurd.h>
#include "core_server.h"
#include <bfd.h>
#include <string.h>

/* Uses nonexistent bfd function: */
char *bfd_intuit_section_name (bfd_vma vma, bfd_size_type size,
			       flagword *flags);

/* Object file format to write core files in.  */
static char *core_target = NULL;

/* Dump a core from TASK into FILE.
   SIGNO and SIGCODE indicate the signal that killed the process.  */
   
error_t
core_dump_task (mach_port_t coreserver,
		task_t task,
		file_t file,
		int signo, int sigcode,
		const char *my_target)
{
  error_t err;

  processor_set_name_t pset;
  host_t host;
  processor_set_basic_info_data_t pinfo;

  thread_t *threads;
  size_t nthreads;

  vm_address_t addr;
  vm_size_t size;
  vm_prot_t prot, maxprot;
  vm_inherit_t inherit;
  boolean_t shared;
  memory_object_name_t objname;
  vm_offset_t offset;

  bfd *bfd;
  bfd_architecture arch;
  bfd_machine machine;
  asection *sec;

  /* The task is suspended while we examine it.
     In the case of a post-mortem dump, the only thread not suspended will
     be the signal thread, which will be blocked waiting for this RPC to
     return.  But for gcore, threads might be running.  And Leviticus
     specifies that only suspended threads be thread_info'd, anyway.  */
  if (err = task_suspend (task))
    goto lose;

  /* Figure out what flavor of machine the task is on.  */
  if (err = task_get_assignment (task, &pset))
    goto lose;
  err = processor_set_info (pset, PROCESSOR_SET_BASIC_INFO, &host,
			    &pinfo, PROCESSOR_SET_BASIC_INFO_COUNT);
  mach_port_deallocate (mach_task_self (), pset);
  if (err)
    goto lose;
  err = bfd_mach_host_arch_mach (host, &arch, &machine);
  mach_port_deallocate (mach_task_self (), host);
  if (err)
    goto lose;

  /* Open the BFD.  */
  bfd = NULL;
  {
    FILE *f = fopenport (file, "w");
    if (f == NULL)
      {
	err = errno;
	goto lose;
      }
    bfd = bfd_openstream (f, my_target ?: core_target);
    if (bfd == NULL)
      {
	err = errno;
	(void) fclose (f);
	errno = err;
	goto bfdlose;
      }
  }

  bfd_set_arch_mach (bfd, arch, machine);

  /* XXX How are thread states stored in bfd? */
  if (err = task_threads (task, &threads, &nthreads))
    goto lose;

  /* Create a BFD section to describe each contiguous chunk
     of the task's address space with the same stats.  */
  sec = NULL;
  addr = 0;
  while (!vm_region (task, &addr, &size, &prot, &maxprot,
		     &inherit, &shared, &objname, &offset))
    {
      mach_port_deallocate (mach_task_self (), objname);
      
      if (prot != VM_PROT_NONE)
	{
	  flagword flags = SEC_NO_FLAGS;
	  
	  if (!(prot & VM_PROT_WRITE))
	    flags |= SEC_READONLY;
	  if (!(prot & VM_PROT_EXECUTE))
	    flags |= SEC_DATA;
	  
	  if (sec != NULL &&
	      (vm_address_t) (bfd_section_vma (bfd, sec) +
			      bfd_section_size (bfd, sec)) == addr &&
	      flags == (bfd_get_section_flags (bfd, sec) &
			(SEC_READONLY|SEC_DATA)))
	    /* Coalesce with the previous section.  */
	    bfd_set_section_size (bfd, sec,
				  bfd_section_size (bfd, sec) + size);
	  else
	    {
	      /* Make a new section (which might grow by
		 the next region being coalesced onto it). */
	      char *name = bfd_intuit_section_name (addr, size, &flags);
	      if (name == NULL)
		{
		  /* No guess from BFD.  */
		  if (asprintf (&name, "[%p,%p) %c%c%c",
				(void *) addr, (void *) (addr + size),
				(prot & VM_PROT_READ) ? 'r' : '-',
				(prot & VM_PROT_WRITE) ? 'w' : '-',
				(prot & VM_PROT_EXECUTE) ? 'x' : '-') == -1)
		    goto lose;
		}
	      sec = bfd_make_section (name);
	      bfd_set_section_flags (bfd, sec, flags);
	      bfd_set_section_vma (bfd, sec, addr);
	      bfd_set_section_size (bfd, sec, size);
	    }
	}
    }

  /* Write all the sections' data.  */
  for (sec = bfd->sections; sec != NULL; sec = sec->next)
    {
      void *data;
      err = vm_read (task, bfd_section_vma (bfd, sec),
		     bfd_section_size (bfd, sec), &data);
      if (err)
	/* XXX What to do?
	  1. lose
	  2. remove this section
	  3. mark this section as having ungettable contents (how?)
	    */
	goto lose;
      err = bfd_set_section_contents (bfd, sec, data, 0,
				      bfd_section_size (bfd, sec));
      munmap ((caddr_t) data, bfd_section_size (bfd, sec));
      if (err)
	goto bfdlose;
    }

 bfdlose:
  switch (bfd_error)
    {
    case system_call_error:
      err = errno;
      break;

    case no_memory:
      err = ENOMEM;
      break;

    default:
      err = EGRATUITOUS;
      break;
    }

 lose:
  if (bfd != NULL)
    bfd_close (bfd);
  else
    mach_port_deallocate (mach_task_self (), file);
  task_resume (task);
  mach_port_deallocate (mach_task_self (), task);
  return err;
}

error_t
fsys_getroot (fsys_t fsys, idblock_t id, file_t realnode, file_t dotdot,
	      file_t *root)
{
  *root = core;
  mach_port_deallocate (mach_task_self (), realnode);
  mach_port_deallocate (mach_task_self (), dotdot);
  return POSIX_SUCCESS;
}

mach_port_t request_portset;

int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  if (inp->msgh_local_port == fsys)
    return fsys_server (inp, outp);
  else if (inp->msgh_local_port == core)
    return (core_server (inp, outp) ||
	    io_server (inp, outp) ||
	    fs_server (inp, outp));
}

int
main (int argc, char **argv)
{
  error_t err;
  fsys_t fsys;
  mach_port_t boot, dotdot;

  if ((err = mach_port_allocate (mach_task_self (),
				 MACH_PORT_RIGHT_RECEIVE, &fsys)) ||
      (err = mach_port_allocate (mach_task_self (),
				 MACH_PORT_RIGHT_RECEIVE, &core)))
    hurd_perror ("mach_port_allocate", err);
  else if (err = task_get_bootstrap_port (mach_task_self (), &boot))
    hurd_perror ("task_get_bootstrap_port", err);
  else if (err = fsys_startup (boot, fsys, &realnode, &dotdot))
    hurd_perror ("fsys_startup", err);
  mach_port_deallocate (mach_task_self (), dotdot);
  
  mach_port_allocate (mach_task_self (),
		      MACH_PORT_RIGHT_PORT_SET, &request_portset);

  mach_port_move_member (mach_task_self (), fsys, request_portset);
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &core);
  mach_port_move_member (mach_task_self (), core, request_portset);

  mach_port_mod_refs (mach_task_self (), realnode, MACH_PORT_RIGHT_SEND, 1);

  core_target = argv[1];

  do
    err = mach_msg_server (request_server, vm_page_size, request_portset);
  while (!err);
  hurd_perror ("mach_msg_server", err);
  return 1;
}
