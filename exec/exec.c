/* GNU Hurd standard exec server.
   Copyright (C) 1992, 1993, 1994 Free Software Foundation, Inc.
   Written by Roland McGrath.

   #ifdef BFD
   Can exec any executable format the BFD library understands
   to be for this flavor of machine. [requires nonexistent BFD support]
   #endif
   #ifdef AOUT
   Can exec a.out format.
   #endif

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

#include <errno.h>
#include <mach.h>
#include <mach/notify.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <hurd.h>
#include <hurd/startup.h>
#include <hurd/shared.h>
#include <hurd/fsys.h>
#include <hurd/exec.h>
#include "exec_S.h"
#include "fsys_S.h"
#include "notify_S.h"

/* Default: BFD or a.out?  */
#if	!defined (BFD) && !defined (AOUT)
#define	AOUT
#endif


#ifdef	BFD
#include <bfd.h>

/* Uses the following nonexistent functions: */
bfd *bfd_openstream (FILE *);

extern error_t bfd_mach_host_arch_mach (host_t host,
					bfd_architecture *arch,
					bfd_machine *machine);
#else
#include A_OUT_H

extern error_t aout_mach_host_machine (host_t host, int *host_machine);
#endif


/* Data shared between check, check_section,
   load, load_section, and finish.  */
struct execdata
  {
    /* Passed out to caller.  */
    error_t error;

    /* Set by check.  */
    vm_address_t entry;
    FILE stream;
#ifdef	BFD
    bfd *bfd;
#else
    file_t file;
    struct exec *header;
#endif
    memory_object_t filemap, cntlmap;
    struct shared_io *cntl;
    off_t file_size;

    /* Set by caller of load.  */
    task_t task;

#ifdef	BFD
    /* Vector indexed by section index,
       information passed from check_section to load_section.
       Set by caller of check_section and load.  */
    vm_offset_t *locations;
#endif
  };

#ifdef	BFD
static bfd host_bfd;		/* A BFD whose architecture and machine type
				   reflect those of the running system.  */
#else
static enum machine_type host_machine; /* a.out machine_type of the host.  */
#endif

static file_t realnode;
static mach_port_t execserver;	/* Port doing exec protocol.  */
static mach_port_t fsys;	/* Port doing fsys protocol.  */
static mach_port_t request_portset; /* Portset we receive on.  */
static mach_port_t procserver;	/* our proc port */
char *exec_version = "0.0 pre-alpha";
char **save_argv;

/* Standard exec data for secure execs.  */
static mach_port_t *std_ports;
static int *std_ints;
static size_t std_nports, std_nints;


#ifdef	BFD
/* Return a Hurd error code corresponding to the most recent BFD error.  */
static error_t
b2he (error_t deflt)
{
  switch (bfd_error)
    {
    case system_call_error:
      return a2he (errno);

    case no_memory:
      return ENOMEM;

    default:
      return deflt;
    }
}
#else
#define	b2he()	a2he (errno)
#endif

#ifdef	BFD

/* Check a section, updating the `locations' vector [BFD].  */
static void
check_section (bfd *bfd, asection *sec, void *userdata)
{
  struct execdata *u = userdata;
  vm_address_t addr;

  if (u->error)
    return;

  if (!(sec->flags & SEC_ALLOC|SEC_LOAD) ||
      (sec->flags & SEC_NEVER_LOAD))
    /* Nothing to do for this section.  */
    return;

  if (sec->flags & SEC_RELOC)
    {
      u->error = EINVAL;
      return;
    }

  addr = (vm_address_t) sec->bfd_vma;

  if (sec->flags & SEC_LOAD)
    {
      file_ptr section_offset;

      u->locations[sec->index] = sec->filepos;
      if ((off_t) sec->filepos < 0 || (off_t) sec->filepos > e->file_size)
	u->error = EINVAL;
    }
}
#endif

enum section { text, data, bss };

/* Load or allocate a section.  */
static void
#ifdef	BFD
load_section (bfd *bfd, asection *sec, void *userdata)
#else
load_section (enum section section, struct execdata *u)
#endif
{
#ifdef	BFD
  struct execdata *u = userdata;
#endif
  vm_address_t addr = 0;
  vm_offset_t filepos = 0;
  vm_size_t secsize = 0;
  vm_prot_t vm_prot;

  if (u->error)
    return;

#ifdef	BFD
  if (sec->flags & SEC_NEVER_LOAD)
    /* Nothing to do for this section.  */
    return;
#endif

  vm_prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;

#ifdef	BFD
  addr = (vm_address_t) sec->bfd_vma;
  filepos = u->locations[sec->index];
  secsize = sec->size;
  if (sec->flags & (SEC_READONLY|SEC_ROM))
    vm_prot &= ~VM_PROT_WRITE;
#else
  switch (section)
    {
    case text:
      addr = (vm_address_t) N_TXTADDR (*u->header);
      filepos = (vm_offset_t) N_TXTOFF (*u->header);
      secsize = N_TXTLEN (*u->header);
      vm_prot &= ~VM_PROT_WRITE;
      break;
    case data:
      addr = (vm_address_t) N_DATADDR (*u->header);
      filepos = (vm_offset_t) N_DATOFF (*u->header);
      secsize = N_DATLEN (*u->header);
      break;
    case bss:
      addr = (vm_address_t) N_BSSADDR (*u->header);
      secsize = N_BSSLEN (*u->header);
      break;
    }
#endif

  if (
#ifdef	BFD
      sec->flags & SEC_LOAD
#else
      section != bss
#endif
      )
    {
      vm_address_t mapstart = round_page (addr);

      if (mapstart - addr < secsize)
	{
	  /* MAPSTART is the first page that starts inside the section.
	     Map all the pages that start inside the section.  */

#ifdef	BFD
	  if (sec->flags & SEC_IN_MEMORY)
	    u->error = vm_write (u->task, mapstart,
				 contents + (mapstart - addr),
				 secsize - (mapstart - addr));
	  else
#endif
	    u->error = vm_map (u->task,
			       &mapstart, secsize - (mapstart - addr), 0, 0,
			       u->filemap, filepos + (mapstart - addr), 1, 
			       vm_prot,
			       VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE,
			       VM_INHERIT_COPY);
	  if (u->error)
	    return;
	}

      if (mapstart > addr)
	{
	  /* We must read and copy in the space in the section before the
             first page boundary.  */
	  vm_address_t overlap_page = trunc_page (addr);
	  vm_address_t ourpage = 0;
	  vm_size_t size = 0;
	  void *readaddr;
	  size_t readsize;

#ifdef	AOUT
	  if (N_MAGIC (*u->header) == NMAGIC || N_MAGIC (*u->header) == ZMAGIC)
	    {
	      u->error = ENOEXEC;
	      goto maplose;
	    }
#endif

	  if (u->error = vm_read (u->task, overlap_page, vm_page_size,
				  &ourpage, &size))
	    {
	    maplose:
	      vm_deallocate (u->task, mapstart, secsize);
	      return;
	    }

	  readaddr = (void *) (ourpage + (addr - overlap_page));
	  readsize = size - (addr - overlap_page);

#ifdef	BFD
	  if (sec->flags & SEC_IN_MEMORY)
	    bcopy (sec->contents, readaddr, readsize);
	  else
#endif
#if	1
	    if (fread (readaddr, readsize, 1, &u->stream) != 1)
	      {
		u->error = errno;
		goto maplose;
	      }
#else
	  if (u->cntl)
	    {
	      /* We cannot call io_read while holding the conch,
		 so we must read by mapping the file ourselves.  */
	      vm_address_t data;
	      if (u->error = vm_map (mach_task_self (), &data, readsize, 0, 1,
				     u->filemap, filepos, 1,
				     VM_PROT_READ, VM_PROT_READ,
				     VM_INHERIT_COPY))
		goto maplose;
	      bcopy ((void *) data, readaddr, readsize);
	      vm_deallocate (mach_task_self (), data, readsize);
	    }
	  else
	    do
	      {
		char *data;
		unsigned int nread;
		data = readaddr;
		if (u->error = io_read (u->file, &data, &nread,
					filepos, readsize))
		  goto maplose;
		if (data != readaddr)
		  {
		    bcopy (data, readaddr, nread);
		    vm_deallocate (mach_task_self (), (vm_address_t) data, 
				   nread);
		  }
		readaddr += nread;
		readsize -= nread;
	      } while (readsize > 0);
#endif
	  u->error = vm_write (u->task, overlap_page, ourpage, size);
	  if (u->error == KERN_PROTECTION_FAILURE)
	    {
	      /* The overlap page is not writable; the section
		 that appears in preceding memory is read-only.
		 Change the page's protection so we can write it.  */
	      u->error = vm_protect (u->task, overlap_page, size,
				     0, vm_prot | VM_PROT_WRITE);
	      if (!u->error)
		u->error = vm_write (u->task, overlap_page, ourpage, size);
	      /* If this section is not supposed to be writable either,
		 restore the page's protection to read-only.  */
	      if (!u->error && !(vm_prot & VM_PROT_WRITE))
		u->error = vm_protect (u->task, overlap_page, size,
				       0, vm_prot);
	    }
	  vm_deallocate (mach_task_self (), ourpage, size);
	  if (u->error)
	    goto maplose;
	}

      if (u->cntl)
	u->cntl->accessed = 1;
    }
#ifdef	BFD
  else if (sec->flags & SEC_ALLOC)
#else
  else
#endif
    {
      /* SEC_ALLOC: Allocate zero-filled memory for the section.  */

      vm_address_t mapstart = round_page (addr);

      if (mapstart - addr < secsize)
	{
	  /* MAPSTART is the first page that starts inside the section.
	     Allocate all the pages that start inside the section.  */

	  if (u->error = vm_allocate (u->task, &mapstart, 
				      secsize - (mapstart - addr), 0))
	    return;
	}

      if (mapstart > addr
#ifdef	BFD
	  && (sec->flags & SEC_HAS_CONTENTS)
#endif
	  )
	{
	  /* Zero space in the section before the first page boundary.  */
	  vm_address_t overlap_page = trunc_page (addr);
	  vm_address_t ourpage = 0;
	  vm_size_t size = 0;
	  if (u->error = vm_read (u->task, overlap_page, vm_page_size,
				  &ourpage, &size))
	    {
	      vm_deallocate (u->task, mapstart, secsize);
	      return;
	    }
	  bzero ((void *) (ourpage + (addr - overlap_page)),
		 size - (addr - overlap_page));
	  u->error = vm_write (u->task, overlap_page, ourpage, size);
	  vm_deallocate (mach_task_self (), ourpage, size);
	}
    }
}

/* Do post-loading processing for a section.  This consists of peeking the
   pages of non-demand-paged executables.  */

static void
#ifdef	BFD
postload_section (bfd *bfd, asection *sec, void *userdata)
#else
postload_section (enum section section, struct execdata *u)
#endif
{
#ifdef	BFD
  struct execdata *u = userdata;
#endif
  vm_address_t addr = 0;
  vm_size_t secsize = 0;

#ifdef	BFD
  addr = (vm_address_t) sec->bfd_vma;
  secsize = sec->size;
#else
  switch (section)
    {
    case text:
      addr = (vm_address_t) N_TXTADDR (*u->header);
      secsize = N_TXTLEN (*u->header);
      break;
    case data:
      addr = (vm_address_t) N_DATADDR (*u->header);
      secsize = N_DATLEN (*u->header);
      break;
    case bss:
      addr = (vm_address_t) N_BSSADDR (*u->header);
      secsize = N_BSSLEN (*u->header);
      break;
    }
#endif

  if (
#ifdef	AOUT
      section != bss && N_MAGIC (*u->header) == NMAGIC
#else
      (sec->flags & SEC_LOAD) && !(bfd->flags & D_PAGED)
#endif
      )
    {
      /* Pre-load the section by peeking every mapped page.  */
      vm_address_t myaddr, a;
      vm_size_t mysize;
      myaddr = 0;
	  
      /* We have already mapped the file into the task in load_section.
	 Now read from the task's memory into our own address space so we
	 can peek each page and cause it to be paged in.  */
      if (u->error = vm_read (u->task, trunc_page (addr), round_page (secsize),
			      &myaddr, &mysize))
	return;

      /* Peek at the first word of each page.  */
      for (a = ((myaddr + mysize) & ~(vm_page_size - 1));
	   a >= myaddr; a -= vm_page_size)
	/* Force it to be paged in.  */
	(void) *(volatile int *) a;

      vm_deallocate (mach_task_self (), myaddr, mysize);
    }
}



/* stdio input-room function.  */
static int
input_room (FILE *f)
{
  struct execdata *e = f->__cookie;
  const size_t size = e->file_size;

  if (f->__buffer != NULL)
    vm_deallocate (mach_task_self (), (vm_address_t) f->__buffer,
		   f->__bufsize);

  if (f->__target > size)
    {
      f->__eof = 1;
      return EOF;
    }

  f->__buffer = NULL;
  if (vm_map (mach_task_self (),
	      (vm_address_t *) &f->__buffer, vm_page_size, 0, 1,
	      e->filemap, f->__target, 1, VM_PROT_READ, VM_PROT_READ,
	      VM_INHERIT_NONE))
    {
      errno = EIO;
      f->__error = 1;
      return EOF;
    }
  f->__bufsize = vm_page_size;
  f->__offset = f->__target;

  if (f->__target + f->__bufsize > size)
    f->__get_limit = f->__buffer + (size - f->__target);
  else
    f->__get_limit = f->__buffer + f->__bufsize;

  if (e->cntl)
    e->cntl->accessed = 1;

  f->__bufp = f->__buffer;
  return (unsigned char) *f->__bufp++;
}

static int
close_exec_stream (void *cookie)
{
  struct execdata *e = cookie;

  if (e->stream.__buffer != NULL)
    vm_deallocate (mach_task_self (), (vm_address_t) e->stream.__buffer,
		   e->stream.__bufsize);

  return 0;
}


/* Prepare to load FILE.
   On successful return, the caller must allocate the
   E->locations vector, and map check_section over the BFD.  */
static inline void
check (file_t file, struct execdata *e)
{
  e->file = file;

#ifdef	BFD
  e->bfd = NULL;
#endif
  e->cntl = NULL;
  e->filemap = MACH_PORT_NULL;
  e->cntlmap = MACH_PORT_NULL;

  {
    memory_object_t rd, wr;
    if (e->error = io_map (file, &rd, &wr))
      return;
    if (wr)
      mach_port_deallocate (mach_task_self (), wr);
    if (rd == MACH_PORT_NULL)
      {
	e->error = EBADF;	/* ? XXX */
	return;
      }
    e->filemap = rd;

    e->error = /* io_map_cntl (file, &e->cntlmap) */ EOPNOTSUPP; /* XXX */
    if (e->error)
      {
	/* No shared page.  Do a stat to find the file size.  */
	struct stat st;
	if (e->error = io_stat (file, &st))
	  return;
	e->file_size = st.st_size;
      }
    else
      e->error = vm_map (mach_task_self (), (vm_address_t *) &e->cntl,
			 vm_page_size, 0, 1, e->cntlmap, 0, 0,
			 VM_PROT_READ|VM_PROT_WRITE, 
			 VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);

    if (e->cntl)
      while (1)
	{
	  spin_lock (&e->cntl->lock);
	  switch (e->cntl->conch_status)
	    {
	    case USER_COULD_HAVE_CONCH:
	      e->cntl->conch_status = USER_HAS_CONCH;
	    case USER_HAS_CONCH:
	      spin_unlock (&e->cntl->lock);
	      /* Break out of the loop.  */
	      break;
	    case USER_RELEASE_CONCH:
	    case USER_HAS_NOT_CONCH:
	    default:		/* Oops.  */
	      spin_unlock (&e->cntl->lock);
	      if (e->error = io_get_conch (e->file))
		return;
	      /* Continue the loop.  */
	      continue;
	    }

	  /* Get here if we are now IT.  */
	  e->file_size = 0;
	  if (e->cntl->use_file_size)
	    e->file_size = e->cntl->file_size;
	  if (e->cntl->use_read_size && e->cntl->read_size > e->file_size)
	    e->file_size = e->cntl->read_size;
	  break;
	}
  }

  /* Open a stdio stream to do mapped i/o to the file.  */
  memset (&e->stream, 0, sizeof (e->stream));
  e->stream.__magic = _IOMAGIC;
  e->stream.__mode.__read = 1;
  e->stream.__userbuf = 1;
  e->stream.__room_funcs.__input = input_room;
  e->stream.__io_funcs.close = close_exec_stream;
  e->stream.__cookie = e;

#ifdef	BFD
  e->bfd = bfd_openstream (&e->stream);
  if (e->bfd == NULL)
    {
      e->error = b2he (ENOEXEC);
      return;
    }

  bfd_error = no_error;
  if (!bfd_check_format (e->bfd, bfd_object))
    {
      e->error = b2he (ENOEXEC);
      return;
    }
  else if (!bfd_arch_compatible (e->bfd, &host_bfd, NULL, NULL) ||
	   !(bfd->flags & EXEC_P))
    {
      e->error = b2he (EINVAL);
      return;
    }

  e->entry = bfd->start_address;
#else
  /* Map in the a.out header.  */
  if (e->file_size < sizeof (*e->header))
    {
      e->error = EINVAL;
      return;
    }
  e->header = NULL;
  if (e->error = vm_map (mach_task_self (),
			 (vm_address_t *) &e->header, sizeof (*e->header),
			 0, 1, e->filemap, 0, 1, VM_PROT_READ, VM_PROT_READ,
			 VM_INHERIT_NONE))
    return;
  if (N_BADMAG (*e->header))
    {
      e->error = ENOEXEC;
      return;
    }
  if (N_MACHTYPE (*e->header) && N_MACHTYPE (*e->header) != host_machine)
    {
      e->error = EINVAL;
      return;
    }

  e->entry = e->header->a_entry;
#endif
}


/* Load the file.  */
static inline void
load (task_t usertask, struct execdata *e)
{
  if (e->error)
    return;

  e->task = usertask;
#ifdef	BFD
  bfd_map_over_sections (e->bfd, load_section, e);
#else
  load_section (text, e);
  load_section (data, e);
  load_section (bss, e);
#endif
}

/* Do post-loading processing on the task.  */
static inline void
postload (struct execdata *e)
{
  if (e->error)
    return;

#ifdef	BFD
  bfd_map_over_sections (e->bfd, postload_section, e);
#else
  postload_section (text, e);
  postload_section (data, e);
  postload_section (bss, e);
#endif
}


/* Release the conch and clean up mapping the file and control page.  */
static inline void
finish_mapping (struct execdata *e)
{
  if (e->cntl != NULL)
    {
      spin_lock (&e->cntl->lock);
      if (e->cntl->conch_status == USER_RELEASE_CONCH)
	{
	  spin_unlock (&e->cntl->lock);
	  io_release_conch (e->file);
	}
      else
	{
	  e->cntl->conch_status = USER_HAS_NOT_CONCH;
	  spin_unlock (&e->cntl->lock);
	}
      vm_deallocate (mach_task_self (), (vm_address_t) e->cntl, vm_page_size);
    }
  if (e->filemap != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), e->filemap);
  if (e->cntlmap != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), e->cntlmap);
}
      
/* Clean up after reading the file (need not be completed).  */
static inline void
finish (struct execdata *e)
{
  finish_mapping (e);
#ifdef	BFD
  if (e->bfd != NULL)
    (void) bfd_close (e->bfd);
#else
  if (e->header != NULL)
    vm_deallocate (mach_task_self (),
		   (vm_address_t) e->header, sizeof (*e->header));
  mach_port_deallocate (mach_task_self (), e->file);
#endif
}

static int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  extern int notify_server (), exec_server (), fsys_server ();

  return (notify_server (inp, outp) ||
	  exec_server (inp, outp) ||
	  fsys_server (inp, outp));
}


/* Allocate SIZE bytes of storage, and make the
   resulting pointer a name for a new receive right.  */
static void *
alloc_recv (size_t size)
{
  void *obj = malloc (size);
  if (obj == NULL)
    return NULL;

  if (mach_port_allocate_name (mach_task_self (),
			       MACH_PORT_RIGHT_RECEIVE,
			       (mach_port_t) obj)
      == KERN_NAME_EXISTS)
    {
      void *new = alloc_recv (size); /* Bletch.  */
      free (obj);
      return new;
    }

  return obj;
}

/* Information kept around to be given to a new task
   in response to a message on the task's bootstrap port.  */
struct bootinfo
  {
    vm_address_t stack_base;
    vm_size_t stack_size;
    int flags;
    char *argv, *envp;
    size_t argvlen, envplen, dtablesize, nports, nints;
    mach_port_t *dtable, *portarray;
    int *intarray;
  };

static inline error_t
servercopy (void **arg, u_int argsize, boolean_t argcopy)
{
  if (argcopy)
    {
      /* ARG came in-line, so we must copy it.  */
      error_t error;
      void *copy;
      if (error = vm_allocate (mach_task_self (),
			       (vm_address_t *) &copy, argsize, 1))
	return error;
      bcopy (*arg, copy, argsize);
      *arg = copy;
    }
  return 0;
}

/* Put PORT into *SLOT.  If *SLOT isn't already null, then 
   mach_port_deallocate it first.  If AUTHENTICATE is not null, then
   do an io_reauthenticate transaction to determine the port to put in
   *SLOT.  If CONSUME is set, then don't create a new send right;
   otherwise do.  (It is an error for both CONSUME and AUTHENTICATE to be 
   set.) */
static void
set_init_port (mach_port_t port, mach_port_t *slot, auth_t authenticate,
	       int consume)
{
  mach_port_t ref;

  if (*slot != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), *slot);
  if (authenticate != MACH_PORT_NULL && port != MACH_PORT_NULL)
    {
      ref = mach_reply_port ();
      io_reauthenticate (port, ref, MACH_MSG_TYPE_MAKE_SEND);
      auth_user_authenticate (authenticate, port, 
			      ref, MACH_MSG_TYPE_MAKE_SEND, slot);
      mach_port_destroy (mach_task_self (), ref);
    }
  else
    {
      *slot = port;
      if (!consume && port != MACH_PORT_NULL)
	mach_port_mod_refs (mach_task_self (), port, MACH_PORT_RIGHT_SEND, 1);
    }
}

static error_t
do_exec (mach_port_t execserver,
	 file_t file,
	 task_t oldtask,
	 int flags,
	 char *argv, u_int argvlen, boolean_t argv_copy,
	 char *envp, u_int envplen, boolean_t envp_copy,
	 mach_port_t *dtable, u_int dtablesize, boolean_t dtable_copy,
	 mach_port_t *portarray, u_int nports, boolean_t portarray_copy,
	 int *intarray, u_int nints, boolean_t intarray_copy,
	 mach_port_t *deallocnames, u_int ndeallocnames,
	 mach_port_t *destroynames, u_int ndestroynames)
{
  struct execdata e;
  int finished = 0;
  task_t newtask = MACH_PORT_NULL;
  thread_t thread = MACH_PORT_NULL;
  struct bootinfo *boot = 0;
  int secure, defaults;

  /* Catch this error now, rather than later.  */
  if ((!std_ports || !std_ints) && (flags & (EXEC_SECURE|EXEC_DEFAULTS)))
    return EIEIO;

  /* Check the file for validity first.  */
  check (file, &e);
#if 0
  if (e.error == ENOEXEC)
    /* Check for a #! executable file.  */
    check_hashbang (&e, replyport,
		    file, oldtask, flags,
		    argv, argvlen, argv_copy,
		    envp, envplen, envp_copy,
		    dtable, dtablesize, dtable_copy,
		    portarray, nports, portarray_copy,
		    intarray, nints, intarray_copy,
		    deallocnames, ndeallocnames,
		    destroynames, ndestroynames);
#endif
#ifdef	BFD
  if (! e.error)
    {
      e.locations = alloca (e.bfd->section_count * sizeof (vm_offset_t));
      bfd_map_over_sections (e.bfd, check_section, e);
    }
#endif
  if (e.error)
    {
      finish (&e);
      return e.error;
    }

  /* Suspend the existing task before frobnicating it.  */
  if (oldtask != MACH_PORT_NULL && (e.error = task_suspend (oldtask)))
    return e.error;

  if (oldtask == MACH_PORT_NULL)
    flags |= EXEC_NEWTASK;

  if (flags & (EXEC_NEWTASK|EXEC_SECURE))
    {
      /* Create the new task.  If we are not being secure, then use OLDTASK
	 for the task_create RPC, in case it is something magical.  */	 
      if (e.error = task_create (flags & EXEC_SECURE || !oldtask ?
				 mach_task_self () : oldtask,
				 0, &newtask))
	goto out;
    }
  else
    {
      thread_array_t threads;
      mach_msg_type_number_t nthreads, i;

      /* Terminate all the threads of the old task.  */

      if (e.error = task_threads (oldtask, &threads, &nthreads))
	goto out;
      for (i = 0; i < nthreads; ++i)
	{
	  thread_terminate (threads[i]);
	  mach_port_deallocate (mach_task_self (), threads[i]);
	}
      vm_deallocate (mach_task_self (),
		     (vm_address_t) threads, nthreads * sizeof (thread_t));

      /* Deallocate the entire virtual address space of the task.  */

      vm_deallocate (oldtask,
		     VM_MIN_ADDRESS, VM_MAX_ADDRESS - VM_MIN_ADDRESS);

      /* Deallocate and destroy the ports requested by the caller.
	 These are ports the task wants not to lose if the exec call
	 fails, but wants removed from the new program task.  */

      for (i = 0; i < ndeallocnames; ++i)
	mach_port_deallocate (oldtask, deallocnames[i]);

      for (i = 0; i < ndestroynames; ++i)
	mach_port_destroy (oldtask, destroynames[i]);

      newtask = oldtask;
    }

  /* Load the file into the task.  */
  load (newtask, &e);
  if (e.error)
    goto out;

  /* Release the conch for the file.  */
  finish_mapping (&e);

  /* Further frobnicate the task after loading from the file.  */
  postload (&e);
  if (e.error)
    goto out;

  /* Clean up.  */
  finish (&e);
  finished = 1;

  /* Create the initial thread.  */
  if (e.error = thread_create (newtask, &thread))
    goto out;

  /* Store the data that we will give in response
     to the RPC on the new task's bootstrap port.  */

  boot = alloc_recv (sizeof (*boot));
  if (boot == NULL)
    {
      e.error = ENOMEM;
      goto out;
    }

  if (nports <= INIT_PORT_BOOTSTRAP)
    {
      mach_port_t *new;
      vm_allocate (mach_task_self (),
		   (vm_address_t *) &new,
		   INIT_PORT_MAX * sizeof (mach_port_t), 1);
      memcpy (new, portarray, nports * sizeof (mach_port_t));
      bzero (&new[nports], (INIT_PORT_MAX - nports) * sizeof (mach_port_t));
    }
  if (portarray[INIT_PORT_BOOTSTRAP] == MACH_PORT_NULL &&
      oldtask != MACH_PORT_NULL)
    task_get_bootstrap_port (oldtask, &portarray[INIT_PORT_BOOTSTRAP]);

  if (e.error = mach_port_insert_right (mach_task_self (),
					(mach_port_t) boot,
					(mach_port_t) boot,
					MACH_MSG_TYPE_MAKE_SEND))
    goto out;
  if (e.error = task_set_bootstrap_port (newtask, (mach_port_t) boot))
    goto out;
  mach_port_deallocate (mach_task_self (), (mach_port_t) boot);

  if (e.error = servercopy ((void **) &argv, argvlen, argv_copy))
    goto bootout;
  boot->argv = argv;
  boot->argvlen = argvlen;
  if (e.error = servercopy ((void **) &envp, envplen, envp_copy))
    goto bootout;
  boot->envp = envp;
  boot->envplen = envplen;
  if (e.error = servercopy ((void **) &dtable,
			    dtablesize * sizeof (mach_port_t),
			    dtable_copy))
    goto bootout;
  boot->dtable = dtable;
  boot->dtablesize = dtablesize;
  if (e.error = servercopy ((void **) &portarray,
			    nports * sizeof (mach_port_t),
			    portarray_copy))
    goto bootout;
  boot->portarray = portarray;
  boot->nports = nports;
  if (e.error = servercopy ((void **) &intarray,
			    nints * sizeof (mach_port_t),
			    intarray_copy))
    goto bootout;
  boot->intarray = intarray;
  boot->nints = nints;
  boot->flags = flags;

  {
    mach_port_t unused;
    mach_port_request_notification (mach_task_self (),
				    (mach_port_t) boot,
				    MACH_NOTIFY_NO_SENDERS, 0,
				    (mach_port_t) boot,
				    MACH_MSG_TYPE_MAKE_SEND_ONCE,
				    &unused);
  }

  secure = (flags & EXEC_SECURE);
  
  defaults = (flags & EXEC_DEFAULTS);
  
  if ((secure || defaults) && nports < INIT_PORT_MAX)
    {
      /* Allocate a new vector that is big enough.  */
      vm_allocate (mach_task_self (),
		   (vm_address_t *) &boot->portarray,
		   INIT_PORT_MAX * sizeof (mach_port_t),
		   1);
      memcpy (boot->portarray, portarray,
	      nports * sizeof (mach_port_t));
      vm_deallocate (mach_task_self (), (vm_address_t) portarray,
		     nports * sizeof (mach_port_t));
      nports = INIT_PORT_MAX;
    }

  /* Note that the paretheses on this first test are different from the others
     below it. */
  if ((secure || defaults)
      && boot->portarray[INIT_PORT_AUTH] == MACH_PORT_NULL)
    /* Q: Doesn't this let anyone run a program and make it
       get a root auth port? 
       A: No; the standard port for INIT_PORT_AUTH has no UID's at all.
       See init.trim/init.c (init_stdarrays).  */
    set_init_port (std_ports[INIT_PORT_AUTH], 
		   &boot->portarray[INIT_PORT_AUTH], 0, 0);
  if (secure || (defaults 
		 && boot->portarray[INIT_PORT_PROC] == MACH_PORT_NULL))
    {
      mach_port_t new;
      if (e.error = proc_task2proc (procserver, newtask, &new))
	goto bootout;

      set_init_port (new, &boot->portarray[INIT_PORT_PROC], 0, 1);

      /* XXX We should also call proc_setowner at this point. */
    }
  else if (oldtask != newtask && oldtask != MACH_PORT_NULL 
	   && nports > INIT_PORT_PROC
	   && boot->portarray[INIT_PORT_PROC] != MACH_PORT_NULL)
    {
      mach_port_t new;
      /* This task port refers to the old task; use it to fetch a new
	 one for the new task.  */
      if (e.error = proc_task2proc (boot->portarray[INIT_PORT_PROC], 
				    newtask, &new))
	goto bootout;
      set_init_port (new, &boot->portarray[INIT_PORT_PROC], 0, 1);
    }
  if (secure || (defaults
		 && boot->portarray[INIT_PORT_CRDIR] == MACH_PORT_NULL))
    set_init_port (std_ports[INIT_PORT_CRDIR],
		   &boot->portarray[INIT_PORT_CRDIR], 
		   boot->portarray[INIT_PORT_AUTH], 0);
  if (secure || (defaults && 
		 boot->portarray[INIT_PORT_CWDIR] == MACH_PORT_NULL))
    set_init_port (std_ports[INIT_PORT_CWDIR],
		   &boot->portarray[INIT_PORT_CWDIR],
		   boot->portarray[INIT_PORT_AUTH], 0);
  
  if ((secure || defaults) && nints < INIT_INT_MAX)
    {
      /* Allocate a new vector that is big enough.  */
      vm_allocate (mach_task_self (),
		   (vm_address_t *) &boot->intarray,
		   INIT_INT_MAX * sizeof (int),
		   1);
      memcpy (boot->intarray, intarray, nints * sizeof (int));
      vm_deallocate (mach_task_self (), (vm_address_t) intarray,
		     nints * sizeof (int));
      nints = INIT_INT_MAX;
    }

  if (secure)
    boot->intarray[INIT_UMASK] = std_ints ? std_ints[INIT_UMASK] : CMASK;
      
  if (nports > INIT_PORT_PROC)
    proc_mark_exec (boot->portarray[INIT_PORT_PROC]);

  /* Start up the initial thread at the entry point.  */
  boot->stack_base = 0, boot->stack_size = 0; /* Don't care about values.  */
  if (e.error = mach_setup_thread (newtask, thread, (void *) e.entry,
				   &boot->stack_base, &boot->stack_size))
    goto bootout;

  if (oldtask != newtask && oldtask != MACH_PORT_NULL)
    {
      /* The program is on its way.  The old task can be nuked.  */
      process_t proc;
      process_t psrv;

      /* Use the canonical proc server if secure, or there is none other.
	 When not secure, it is nice to let processes associate with
	 whatever proc server turns them on, regardless of which exec
	 itself is using.  */
      if ((flags & EXEC_SECURE)
	  || nports <= INIT_PORT_PROC
	  || boot->portarray[INIT_PORT_PROC] == MACH_PORT_NULL)
	psrv = procserver;
      else
	psrv = boot->portarray[INIT_PORT_PROC];

      /* XXX there is a race here for SIGKILLing the process. -roland
         I don't think it matters.  -mib */
      if (! proc_task2proc (psrv, oldtask, &proc))
	{
	  proc_reassign (proc, newtask);
	  mach_port_deallocate (mach_task_self (), proc);
	}
    }

  newtask = MACH_PORT_NULL;

 bootout:
  if (e.error)
    {
      mach_port_deallocate (mach_task_self (), (vm_address_t) boot);
      if (boot->portarray != portarray)
	vm_deallocate (mach_task_self (),
		       (vm_address_t) boot->portarray,
		       boot->nports * sizeof (mach_port_t));
      free (boot);
    }

 out:
  if (newtask != MACH_PORT_NULL)
    {
      task_terminate (newtask);
      mach_port_deallocate (mach_task_self (), newtask);
    }
  if (!finished)
    finish (&e);
  
  task_resume (oldtask);
  thread_resume (thread);

  if (thread != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), thread);

  mach_port_deallocate (mach_task_self (), oldtask);
  
  mach_port_move_member (mach_task_self (),
			 (mach_port_t) boot, request_portset);

  return e.error;
}

kern_return_t
S_exec_exec (mach_port_t execserver,
	     file_t file,
	     task_t oldtask,
	     int flags,
	     char *argv, u_int argvlen, boolean_t argv_copy,
	     char *envp, u_int envplen, boolean_t envp_copy,
	     mach_port_t *dtable, u_int dtablesize, boolean_t dtable_copy,
	     mach_port_t *portarray, u_int nports, boolean_t portarray_copy,
	     int *intarray, u_int nints, boolean_t intarray_copy,
	     mach_port_t *deallocnames, u_int ndeallocnames,
	     mach_port_t *destroynames, u_int ndestroynames)
{
  if (!(flags & EXEC_SECURE))
    {
      const char envar[] = "\0EXECSERVERS=";
      char *p = NULL;
      if (envplen >= sizeof (envar) &&
	  !memcmp (&envar[1], envp, sizeof (envar) - 2))
	p = envp - 1;
      else
	p = memmem (envp, envplen, envar, sizeof (envar) - 1);
      if (p != NULL)
	{
	  size_t len;
	  char *list;
	  int tried = 0;
	  p += sizeof (envar) - 1;
	  len = strlen (p) + 1;
	  list = alloca (len);
	  memcpy (list, p, len);
	  while (p = strsep (&list, ":"))
	    {
	      file_t server;
	      if (!hurd_file_name_lookup (portarray[INIT_PORT_CRDIR],
					  portarray[INIT_PORT_CWDIR],
					  p, 0, 0, &server))
		{
		  error_t err = (server == execserver ?
				 do_exec (server, file, oldtask, 0,
					  argv, argvlen, argv_copy,
					  envp, envplen, envp_copy,
					  dtable, dtablesize, dtable_copy,
					  portarray, nports, portarray_copy,
					  intarray, nints, intarray_copy,
					  deallocnames, ndeallocnames,
					  destroynames, ndestroynames) :
				 exec_exec (server,
					    file, MACH_MSG_TYPE_MOVE_SEND,
					    oldtask, 0,
					    argv, argvlen,
					    envp, envplen,
					    dtable, MACH_MSG_TYPE_MOVE_SEND,
					    dtablesize,
					    portarray, MACH_MSG_TYPE_MOVE_SEND,
					    nports,
					    intarray, nints,
					    deallocnames, ndeallocnames,
					    destroynames, ndestroynames));
		  mach_port_deallocate (mach_task_self (), server);
		  if (err != ENOEXEC)
		    return err;
		  tried = 1;
		}
	    }
	  if (tried)
	    /* At least one exec server got a crack at it and gave up.  */
	    return ENOEXEC;
	}
    }

  /* There were no user-specified exec servers,
     or none of them could be found.  */

  return do_exec (execserver, file, oldtask, flags,
		  argv, argvlen, argv_copy,
		  envp, envplen, envp_copy,
		  dtable, dtablesize, dtable_copy,
		  portarray, nports, portarray_copy,
		  intarray, nints, intarray_copy,
		  deallocnames, ndeallocnames,
		  destroynames, ndestroynames);
}

kern_return_t
S_exec_setexecdata (mach_port_t me,
		    mach_port_t *ports, u_int nports, int ports_copy,
		    int *ints, u_int nints, int ints_copy)
{
  error_t err;

  /* XXX needs authentication */

  if (nports < INIT_PORT_MAX || nints < INIT_INT_MAX)
    return EINVAL;

  if (std_ports)
    vm_deallocate (mach_task_self (), (vm_address_t)std_ports, 
		   std_nports * sizeof (mach_port_t));
  if (err = servercopy ((void **) &ports, nports * sizeof (mach_port_t),
			ports_copy))
    return err;

  std_ports = ports;
  std_nports = nports;

  if (std_ints)
    vm_deallocate (mach_task_self (), (vm_address_t)std_ints,
		   std_nints * sizeof (int));
  if (err = servercopy ((void **) &ints, nints * sizeof (int), ints_copy))
    return err;

  std_ints = ints;
  std_nints = nints;

  return 0;
}



/* fsys server.  */

kern_return_t
S_fsys_getroot (fsys_t fsys,
		mach_port_t dotdot,
		uid_t *uids, u_int nuids,
		gid_t *gids, u_int ngids,
		int flags,
		retry_type *retry,
		char *retry_name,
		file_t *rootfile,
		mach_msg_type_name_t *rootfilePoly)
{
  /* XXX eventually this should return a user-specific port which has an
     associated access-restricted realnode port which file ops get
     forwarded to.  */



  *rootfile = execserver;
  *rootfilePoly = MACH_MSG_TYPE_MAKE_SEND;

  *retry = FS_RETRY_NORMAL;
  *retry_name = '\0';
  return 0;
}

kern_return_t
S_fsys_goaway (fsys_t fsys, int flags)
{
  if (!(flags & FSYS_GOAWAY_FORCE))
    {
      mach_port_t *serving;
      mach_msg_type_number_t nserving, i;
      mach_port_get_set_status (mach_task_self (), request_portset,
				&serving, &nserving);
      for (i = 0; i < nserving; ++i)
	mach_port_deallocate (mach_task_self (), serving[i]);
      if (nserving > 2)
	/* Not just fsys and execserver.
	   We are also waiting on some bootstrap ports.  */
	return EBUSY;
    }
  mach_port_mod_refs (mach_task_self (), request_portset,
		      MACH_PORT_TYPE_RECEIVE, -1);
  return 0;
}

kern_return_t
S_fsys_startup (mach_port_t bootstrap,
		fsys_t control,
		mach_port_t *node,
		mach_msg_type_name_t *realnodePoly)
{
  return EOPNOTSUPP;
}

kern_return_t
S_fsys_syncfs (fsys_t fsys,
	       int wait,
	       int dochildren)
{
  return EOPNOTSUPP;
}

kern_return_t
S_fsys_mod_readonly (fsys_t fsys,
		     int readonly,
		     int force)
{
  return EOPNOTSUPP;
}

kern_return_t
S_fsys_getfile (fsys_t fsys,
		uid_t *uids,
		u_int nuids,
		uid_t *gids,
		u_int ngids,
		char *filehandle,
		u_int filehandlelen,
		mach_port_t *file,
		mach_msg_type_name_t *filetype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_fsys_getpriv (fsys_t fsys,
		mach_port_t *hp,
		mach_port_t *dm,
		mach_port_t *tk)
{
  return EOPNOTSUPP;
}

kern_return_t
S_fsys_init (fsys_t fsys,
	     mach_port_t reply, mach_msg_type_name_t replytype,
	     mach_port_t ps,
	     mach_port_t ah)
{
  return EOPNOTSUPP;
}




/* RPC sent on the bootstrap port.  */

kern_return_t
S_exec_startup (mach_port_t port,
		vm_address_t *stack_base, vm_size_t *stack_size,
		int *flags,
		char **argvp, u_int *argvlen,
		char **envpp, u_int *envplen,
		mach_port_t **dtable, mach_msg_type_name_t *dtablepoly,
		u_int *dtablesize,
		mach_port_t **portarray, mach_msg_type_name_t *portpoly,
		u_int *nports,
		int **intarray, u_int *nints)
{
  struct bootinfo *boot = (struct bootinfo *)port;
  if ((mach_port_t) boot == execserver || (mach_port_t) boot == fsys)
    return EOPNOTSUPP;

  *stack_base = boot->stack_base;
  *stack_size = boot->stack_size;

  *argvp = boot->argv;
  *argvlen = boot->argvlen;

  *envpp = boot->envp;
  *envplen = boot->envplen;

  *dtable = boot->dtable;
  *dtablesize = boot->dtablesize;
  *dtablepoly = MACH_MSG_TYPE_MOVE_SEND;

  *intarray = boot->intarray;
  *nints = boot->nints;

  *portarray = boot->portarray;
  *nports = boot->nports;
  *portpoly = MACH_MSG_TYPE_MOVE_SEND;

  *flags = boot->flags;

  mach_port_move_member (mach_task_self (), (mach_port_t) boot, 
			 MACH_PORT_NULL); /* XXX what is this XXX here for? */
  
  mach_port_mod_refs (mach_task_self (), (mach_port_t) boot,
		      MACH_PORT_TYPE_RECEIVE, -1);
  free (boot);

  return 0;
}

/* Notice when a receive right has no senders.  Either this is the
   bootstrap port of a stillborn task, or it is the execserver port itself. */

kern_return_t
do_mach_notify_no_senders (mach_port_t port, mach_port_mscount_t mscount)
{
  if (port != execserver && port != fsys)
    {
      /* Free the resources we were saving to give the task
	 which can no longer ask for them.  */

      struct bootinfo *boot = (struct bootinfo *) port;
      size_t i;

      vm_deallocate (mach_task_self (),
		     (vm_address_t) boot->argv, boot->argvlen);
      vm_deallocate (mach_task_self (),
		     (vm_address_t) boot->envp, boot->envplen);

      for (i = 0; i < boot->dtablesize; ++i)
	mach_port_deallocate (mach_task_self (), boot->dtable[i]);
      for (i = 0; i < boot->nports; ++i)
	mach_port_deallocate (mach_task_self (), boot->portarray[i]);
      vm_deallocate (mach_task_self (),
		     (vm_address_t) boot->portarray,
		     boot->nports * sizeof (mach_port_t));
      vm_deallocate (mach_task_self (),
		     (vm_address_t) boot->intarray,
		     boot->nints * sizeof (int));

      free (boot);
    }

  /* Deallocate the request port.  */
  mach_port_mod_refs (mach_task_self (), port, MACH_PORT_TYPE_RECEIVE, -1);

  mach_port_mod_refs (mach_task_self (), request_portset,
		      MACH_PORT_TYPE_RECEIVE, -1);

  return KERN_SUCCESS;
}

/* Attempt to set the active translator for the exec server so that
   filesystems other than the bootstrap can find it. */
set_active_trans ()
{
  file_t execnode;
  
  execnode = file_name_lookup (_SERVERS_EXEC, O_NOTRANS | O_CREAT, 0666);
  if (execnode == MACH_PORT_NULL)
    return;
  
  file_set_translator (execnode, 0, FS_TRANS_SET, 0, 0, 0, fsys,
		       MACH_MSG_TYPE_MAKE_SEND);
  mach_port_deallocate (mach_task_self (), execnode);
}


/* Sent by the bootstrap filesystem after the other essential
   servers have been started up.  */

kern_return_t
S_exec_init (mach_port_t server, auth_t auth, process_t proc)
{
  mach_port_t host_priv, dev_master, startup;
  error_t err;

  if (_hurd_ports[INIT_PORT_PROC].port != MACH_PORT_NULL)
    /* Can only be done once.  */
    return EPERM;

  mach_port_mod_refs (mach_task_self (), proc, MACH_PORT_RIGHT_SEND, 1);
  procserver = proc;
  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], proc);
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], auth);

  /* Do initial setup with the proc server.  */
  _hurd_proc_init (save_argv);

  /* Set the active translator on /hurd/exec. */
  set_active_trans ();

  err = get_privileged_ports (&host_priv, &dev_master);
  if (!err)
    {
      proc_register_version (proc, host_priv, "exec", HURD_RELEASE,
			     exec_version);
      mach_port_deallocate (mach_task_self (), dev_master);
      err = proc_getmsgport (proc, 1, &startup);
      if (err)
	{
	  mach_port_deallocate (mach_task_self (), host_priv);
	  host_priv = MACH_PORT_NULL;
	}
    }
  else
    host_priv = MACH_PORT_NULL;
      
  /* Have the proc server notify us when the canonical ints and ports change.
     The notification comes as a normal RPC on the message port, which
     the C library's signal thread handles.  */
  proc_execdata_notify (procserver, execserver, MACH_MSG_TYPE_MAKE_SEND);

  /* Call startup_essential task last; init assumes we are ready to
     run once we call it. */
  if (host_priv != MACH_PORT_NULL)
    {
      startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
			      "exec", host_priv);
      mach_port_deallocate (mach_task_self (), startup);
      mach_port_deallocate (mach_task_self (), host_priv);
    }
  
  return 0;
}


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t boot;

  /* XXX */
  stdout = mach_open_devstream (getdport (1), "w");
  stderr = mach_open_devstream (getdport (2), "w");
  /* End XXX */

  save_argv = argv;

#ifdef	BFD
  /* Put the Mach kernel's idea of what flavor of machine this is into the
     fake BFD against which architecture compatibility checks are made.  */
  err = bfd_mach_host_arch_mach (mach_host_self (),
				 &host_bfd.obj_machine,
				 &host_bfd.obj_arch);
#else
  err = aout_mach_host_machine (mach_host_self (), (int *)&host_machine);
#endif
  if (err)
    return err;

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &fsys);
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &execserver);

  task_get_bootstrap_port (mach_task_self (), &boot);
  fsys_startup (boot, fsys, MACH_MSG_TYPE_MAKE_SEND, &realnode);

  mach_port_allocate (mach_task_self (),
		      MACH_PORT_RIGHT_PORT_SET, &request_portset);
  mach_port_move_member (mach_task_self (), fsys, request_portset);
  mach_port_move_member (mach_task_self (), execserver, request_portset);

  while (1)
    {
      err = mach_msg_server (request_server, vm_page_size, request_portset);
      fprintf (stderr, "%s: mach_msg_server: %s\n",
	       (argv && argv[0]) ? argv[0] : "exec server",
	       strerror (err));
    }
}

/* Nops */
kern_return_t 
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t rights)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_send_once (mach_port_t notify)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
  return EOPNOTSUPP;
}

