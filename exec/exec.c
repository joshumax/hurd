/* GNU Hurd standard exec server.
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation, Inc.
   Written by Roland McGrath.

   Can exec ELF format directly.
   #ifdef GZIP
   Can gunzip executables into core on the fly.
   #endif
   #ifdef BFD
   Can exec any executable format the BFD library understands
   to be for this flavor of machine.
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
#include <hurd/paths.h>
#include <fcntl.h>
#include "exec_S.h"
#include "fsys_S.h"
#include "notify_S.h"

#include <bfd.h>
#include <elf.h>


extern error_t bfd_mach_host_arch_mach (host_t host,
					enum bfd_architecture *bfd_arch,
					long int *bfd_machine,
					Elf32_Half *elf_machine);


/* Data shared between check, check_section,
   load, load_section, and finish.  */
struct execdata
  {
    /* Passed out to caller.  */
    error_t error;

    /* Set by check.  */
    vm_address_t entry;
    FILE stream;
    file_t file;

#ifdef BFD
    bfd *bfd;
#endif
    union			/* Interpreter section giving name of file.  */
      {
	asection *section;
	const Elf32_Phdr *phdr;
      } interp;
    memory_object_t filemap, cntlmap;
    struct shared_io *cntl;
    char *file_data;		/* File data if already copied in core.  */
    off_t file_size;
    size_t optimal_block;	/* Optimal size for io_read from file.  */

    /* Set by caller of load.  */
    task_t task;

    union
      {
	/* Vector indexed by section index,
	   information passed from check_section to load_section.
	   Set by caller of check_section and load.  */
	vm_offset_t *bfd_locations;
	struct
	  {
	    /* Program header table read from the executable.
	       After `check' this is a pointer into the mapping window.
	       By `load' it is local alloca'd storage.  */
	    Elf32_Phdr *phdr;
	    Elf32_Word phnum;	/* Number of program header table elements.  */
	    int anywhere;	/* Nonzero if image can go anywhere.  */
	    vm_address_t loadbase; /* Actual mapping location.  */
	  } elf;
      } info;
  };


/* A BFD whose architecture and machine type are those of the host system.  */
static bfd_arch_info_type host_bfd_arch_info;
static bfd host_bfd = { arch_info: &host_bfd_arch_info };
static Elf32_Half elf_machine;	/* ELF e_machine for the host.  */

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
  switch (bfd_get_error ())
    {
    case bfd_error_system_call:
      return errno;

    case bfd_error_no_memory:
      return ENOMEM;

    default:
      return deflt;
    }
}
#else
#define	b2he()	a2he (errno)
#endif

#ifdef GZIP
static void check_gzip (struct execdata *);
#endif

#ifdef	BFD

/* Check a section, updating the `locations' vector [BFD].  */
static void
check_section (bfd *bfd, asection *sec, void *userdata)
{
  struct execdata *u = userdata;
  vm_address_t addr;
  static const union
    {
      char string[8];
      unsigned int quadword __attribute__ ((mode (DI)));
    } interp = { string: ".interp" };

  if (u->error)
    return;

  /* Fast strcmp for this 8-byte constant string.  */
  if (*(const __typeof (interp.quadword) *) sec->name == interp.quadword)
    u->interp.section = sec;

  if (!(sec->flags & (SEC_ALLOC|SEC_LOAD)) ||
      (sec->flags & SEC_NEVER_LOAD))
    /* Nothing to do for this section.  */
    return;

  addr = (vm_address_t) sec->vma;

  if (sec->flags & SEC_LOAD)
    {
      u->info.bfd_locations[sec->index] = sec->filepos;
      if ((off_t) sec->filepos < 0 || (off_t) sec->filepos > u->file_size)
	u->error = EINVAL;
    }
}
#endif


/* Load or allocate a section.  */
static void
load_section (void *section, struct execdata *u)
{
  vm_address_t addr = 0;
  vm_offset_t filepos = 0;
  vm_size_t filesz = 0, memsz = 0;
  vm_prot_t vm_prot;
  int anywhere;
  vm_address_t mask = 0;
#ifdef BFD
  asection *const sec = section;
#endif
  const Elf32_Phdr *const ph = section;

  if (u->error)
    return;

#ifdef BFD
  if (u->bfd && sec->flags & SEC_NEVER_LOAD)
    /* Nothing to do for this section.  */
    return;
#endif

  vm_prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;

#ifdef	BFD
  if (u->bfd)
    {
      addr = (vm_address_t) sec->vma;
      filepos = u->info.bfd_locations[sec->index];
      memsz = sec->_raw_size;
      filesz = (sec->flags & SEC_LOAD) ? memsz : 0;
      if (sec->flags & (SEC_READONLY|SEC_ROM))
	vm_prot &= ~VM_PROT_WRITE;
      anywhere = 0;
    }
  else
#endif
    {
      addr = ph->p_vaddr & ~(ph->p_align - 1);
      memsz = ph->p_vaddr + ph->p_memsz - addr;
      filepos = ph->p_offset & ~(ph->p_align - 1);
      filesz = ph->p_offset + ph->p_filesz - filepos;
      if ((ph->p_flags & PF_R) == 0)
	vm_prot &= ~VM_PROT_READ;
      if ((ph->p_flags & PF_W) == 0)
	vm_prot &= ~VM_PROT_WRITE;
      if ((ph->p_flags & PF_X) == 0)
	vm_prot &= ~VM_PROT_EXECUTE;
      anywhere = u->info.elf.anywhere;
      if (! anywhere)
	addr += u->info.elf.loadbase;
      else
	switch (elf_machine)
	  {
	  case EM_386:
	  case EM_486:
	    /* On the i386, programs normally load at 0x08000000, and
	       expect their data segment to be able to grow dynamically
	       upward from its start near that address.  We need to make
	       sure that the dynamic linker is not mapped in a conflicting
	       address.  */
	    /* mask = 0xf8000000UL; */ /* XXX */
	    break;
	  default:
	    break;
	  }
    }

  if (memsz == 0)
    /* This section is empty; ignore it.  */
    return;

  if (filesz != 0)
    {
      vm_address_t mapstart = round_page (addr);

      /* Allocate space in the task and write CONTENTS into it.  */
      void write_to_task (vm_address_t mapstart, vm_size_t size,
			  vm_prot_t vm_prot, vm_address_t contents)
	{
	  vm_size_t off = size % vm_page_size;
	  /* Allocate with vm_map to set max protections.  */
	  u->error = vm_map (u->task,
			     &mapstart, size, mask, anywhere,
			     MACH_PORT_NULL, 0, 1,
			     vm_prot|VM_PROT_WRITE,
			     VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE,
			     VM_INHERIT_COPY);
	  if (! u->error && size >= vm_page_size)
	    u->error = vm_write (u->task, mapstart, contents, size - off);
	  if (! u->error && off != 0)
	    {
	      vm_address_t page = 0;
	      u->error = vm_allocate (mach_task_self (),
				      &page, vm_page_size, 1);
	      if (! u->error)
		{
		  memcpy ((void *) page,
			  (void *) (contents + (size - off)),
			  off);
		  u->error = vm_write (u->task, mapstart + (size - off),
				       page, vm_page_size);
		  vm_deallocate (mach_task_self (), page, vm_page_size);
		}
	    }
	  /* Reset the current protections to the desired state.  */
	  if (! u->error && (vm_prot & VM_PROT_WRITE) == 0)
	    u->error = vm_protect (u->task, mapstart, size, 0, vm_prot);
	}

      if (mapstart - addr < filesz)
	{
	  /* MAPSTART is the first page that starts inside the section.
	     Map all the pages that start inside the section.  */

#define SECTION_IN_MEMORY_P	(u->file_data != NULL)
#define SECTION_CONTENTS	(u->file_data + filepos)
	  if (SECTION_IN_MEMORY_P)
	    /* Data is already in memory; write it into the task.  */
	    write_to_task (mapstart, filesz - (mapstart - addr), vm_prot,
			   (vm_address_t) SECTION_CONTENTS
			   + (mapstart - addr));
	  else if (u->filemap != MACH_PORT_NULL)
	    /* Map the data into the task directly from the file.  */
	    u->error = vm_map (u->task,
			       &mapstart, filesz - (mapstart - addr),
			       mask, anywhere,
			       u->filemap, filepos + (mapstart - addr), 1, 
			       vm_prot,
			       VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE,
			       VM_INHERIT_COPY);
	  else
	    {
	      /* Cannot map the data.  Read it into a buffer and vm_write
		 it into the task.  */
	      void *buf;
	      const vm_size_t size = filesz - (mapstart - addr);
	      u->error = vm_allocate (mach_task_self (),
				      (vm_address_t *) &buf, size, 1);
	      if (! u->error)
		{
		  if (fseek (&u->stream,
			     filepos + (mapstart - addr), SEEK_SET) ||
		      fread (buf, size, 1, &u->stream) != 1)
		    u->error = errno;
		  else
		    write_to_task (mapstart, size, vm_prot,
				   (vm_address_t) buf);
		  vm_deallocate (mach_task_self (), (vm_address_t) buf, size);
		}
	    }
	  if (u->error)
	    return;

	  if (anywhere)
	    {
	      /* We let the kernel choose the location of the mapping.
		 Now record where it ended up.  Later sections cannot
		 be mapped anywhere, they must come after this one.  */
	      u->info.elf.loadbase = mapstart;
	      addr = mapstart + (addr % vm_page_size);
	      anywhere = u->info.elf.anywhere = 0;
	      mask = 0;
	    }
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

	  if (u->error = vm_read (u->task, overlap_page, vm_page_size,
				  &ourpage, &size))
	    {
	      if (u->error == KERN_INVALID_ADDRESS)
		{
		  /* The space is unallocated.  */
		  u->error = vm_allocate (u->task,
					  &overlap_page, vm_page_size, 0);
		  size = vm_page_size;
		  if (!u->error)
		    u->error = vm_allocate (mach_task_self (),
					    &ourpage, vm_page_size, 1);
		}
	      if (u->error)
		{
		maplose:
		  vm_deallocate (u->task, mapstart, filesz);
		  return;
		}
	    }

	  readaddr = (void *) (ourpage + (addr - overlap_page));
	  readsize = size - (addr - overlap_page);
	  if (readsize > filesz)
	    readsize = filesz;

	  if (SECTION_IN_MEMORY_P)
	    bcopy (SECTION_CONTENTS, readaddr, readsize);
	  else
	    if (fseek (&u->stream, filepos, SEEK_SET) ||
		fread (readaddr, readsize, 1, &u->stream) != 1)
	      {
		u->error = errno;
		goto maplose;
	      }
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

      /* Tell the code below to zero-fill the remaining area.  */
      addr += filesz;
      memsz -= filesz;
    }

  if (memsz != 0)
    {
      /* SEC_ALLOC: Allocate zero-filled memory for the section.  */

      vm_address_t mapstart = round_page (addr);

      if (mapstart - addr < memsz)
	{
	  /* MAPSTART is the first page that starts inside the section.
	     Allocate all the pages that start inside the section.  */

	  if (u->error = vm_map (u->task, &mapstart, memsz - (mapstart - addr),
				 mask, anywhere, MACH_PORT_NULL, 0, 1,
				 vm_prot, VM_PROT_ALL, VM_INHERIT_COPY))
	    return;
	}

      if (anywhere)
	{
	  /* We let the kernel choose the location of the zero space.
	     Now record where it ended up.  Later sections cannot
	     be mapped anywhere, they must come after this one.  */
	  u->info.elf.loadbase = mapstart;
	  addr = mapstart + (addr % vm_page_size);
	  anywhere = u->info.elf.anywhere = 0;
	  mask = 0;
	}

      if (mapstart > addr)
	{
	  /* Zero space in the section before the first page boundary.  */
	  vm_address_t overlap_page = trunc_page (addr);
	  vm_address_t ourpage = 0;
	  vm_size_t size = 0;
	  if (u->error = vm_read (u->task, overlap_page, vm_page_size,
				  &ourpage, &size))
	    {
	      vm_deallocate (u->task, mapstart, memsz);
	      return;
	    }
	  bzero ((void *) (ourpage + (addr - overlap_page)),
		 size - (addr - overlap_page));
	  if (!(vm_prot & VM_PROT_WRITE))
	    u->error = vm_protect (u->task, overlap_page, size,
				   0, VM_PROT_WRITE);
	  if (! u->error)
	    u->error = vm_write (u->task, overlap_page, ourpage, size);
	  if (! u->error && !(vm_prot & VM_PROT_WRITE))
	    u->error = vm_protect (u->task, overlap_page, size, 0, vm_prot);
	  vm_deallocate (mach_task_self (), ourpage, size);
	}
    }
}

/* Make sure our mapping window (or read buffer) covers
   LEN bytes of the file starting at POSN.  */

static void *
map (struct execdata *e, off_t posn, size_t len)
{
  FILE *f = &e->stream;
  const size_t size = e->file_size;
  size_t offset = 0;

  f->__target = posn;

  if (e->filemap == MACH_PORT_NULL)
    {
      char *buffer = f->__buffer;
      mach_msg_type_number_t nread = f->__bufsize;
      while (nread < len)
	nread += __vm_page_size;
      if (e->error = io_read (e->file, &buffer, &nread,
			      f->__target, e->optimal_block))
	{
	  errno = e->error;
	  f->__error = 1;
	  return NULL;
	}
      if (buffer != f->__buffer)
	{
	  /* The data was returned out of line.  Discard the old buffer.  */
	  vm_deallocate (mach_task_self (), (vm_address_t) f->__buffer,
			 f->__bufsize);
	  f->__buffer = buffer;
	  f->__bufsize = round_page (nread);
	}

      f->__get_limit = f->__buffer + nread;

      if (nread < len)
	{
	  f->__eof = 1;
	  return NULL;
	}
    }
  else
    {
      /* Deallocate the old mapping area.  */
      if (f->__buffer != NULL)
	vm_deallocate (mach_task_self (), (vm_address_t) f->__buffer,
		       f->__bufsize);
      f->__buffer = NULL;

      /* Make sure our mapping is page-aligned in the file.  */
      offset = f->__target % vm_page_size;
      if (offset != 0)
	f->__target -= offset;
      f->__bufsize = round_page (posn + len) - f->__target;

      /* Map the data from the file.  */
      if (vm_map (mach_task_self (),
		  (vm_address_t *) &f->__buffer, f->__bufsize, 0, 1,
		  e->filemap, f->__target, 1, VM_PROT_READ, VM_PROT_READ,
		  VM_INHERIT_NONE))
	{
	  errno = e->error = EIO;
	  f->__error = 1;
	  return NULL;
	}

      if (e->cntl)
	e->cntl->accessed = 1;

      if (f->__target + f->__bufsize > size)
	f->__get_limit = f->__buffer + (size - f->__target);
      else
	f->__get_limit = f->__buffer + f->__bufsize;
    }

  f->__offset = f->__target;
  f->__bufp = f->__buffer + offset;

  if (f->__bufp + len >= f->__get_limit)
    {
      f->__eof = 1;
      return NULL;
    }

  return f->__bufp;
}

/* stdio input-room function.  */
static int
input_room (FILE *f)
{
  struct execdata *e = f->__cookie;
  if (f->__target >= e->file_size)
    {
      f->__eof = 1;
      return EOF;
    }

  return (map (e, f->__target, 1) == NULL ? EOF :
	  (unsigned char) *f->__bufp++);
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


/* Prepare to check and load FILE.  */
static void
prepare (file_t file, struct execdata *e)
{
  e->file = file;

#ifdef	BFD
  e->bfd = NULL;
#endif
  e->file_data = NULL;
  e->cntl = NULL;
  e->filemap = MACH_PORT_NULL;
  e->cntlmap = MACH_PORT_NULL;

  {
    memory_object_t rd, wr;
    if (e->error = io_map (file, &rd, &wr))
      return;
    if (wr != MACH_PORT_NULL)
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
	e->optimal_block = st.st_blksize;
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
  /* This never gets called, but fseek returns ESPIPE if it's null.  */
  e->stream.__io_funcs.seek = __default_io_functions.seek;
  e->stream.__io_funcs.close = close_exec_stream;
  e->stream.__cookie = e;
  e->stream.__seen = 1;

  e->interp.section = NULL;
}

/* Check the magic number, etc. of the file.
   On successful return, the caller must allocate the
   E->locations vector, and map check_section over the BFD.  */

#ifdef BFD
static void
check_bfd (struct execdata *e)
{
  bfd_set_error (bfd_error_no_error);

  e->bfd = bfd_openstreamr (NULL, NULL, &e->stream);
  if (e->bfd == NULL)
    {
      e->error = b2he (ENOEXEC);
      return;
    }

  if (!bfd_check_format (e->bfd, bfd_object))
    {
      e->error = b2he (ENOEXEC);
      return;
    }
  else if (/* !(e->bfd->flags & EXEC_P) || XXX */
	   (host_bfd.arch_info->compatible = e->bfd->arch_info->compatible,
	    bfd_arch_get_compatible (&host_bfd, e->bfd)) != host_bfd.arch_info)
    {
      /* This file is of a recognized binary file format, but it is not
	 executable on this machine.  */
      e->error = b2he (EINVAL);
      return;
    }

  e->entry = e->bfd->start_address;
}
#endif

#include <endian.h>
#if BYTE_ORDER == BIG_ENDIAN
#define host_ELFDATA ELFDATA2MSB
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
#define host_ELFDATA ELFDATA2LSB
#endif

static void
check_elf (struct execdata *e)
{
  Elf32_Ehdr *ehdr = map (e, 0, sizeof (Elf32_Ehdr));
  Elf32_Phdr *phdr;

  if (! ehdr)
    {
      if (! ferror (&e->stream))
	e->error = ENOEXEC;
      return;
    }

  if (*(Elf32_Word *) ehdr != ((union { Elf32_Word word;
				       unsigned char string[SELFMAG]; })
			       { string: ELFMAG }).word)
    {
      e->error = ENOEXEC;
      return;
    }

  if (ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
      ehdr->e_ident[EI_DATA] != host_ELFDATA ||
      ehdr->e_ident[EI_VERSION] != EV_CURRENT ||
      ehdr->e_version != EV_CURRENT ||
      ehdr->e_machine != elf_machine ||
      ehdr->e_ehsize < sizeof *ehdr ||
      ehdr->e_phentsize != sizeof (Elf32_Phdr))
    {
      e->error = EINVAL;
      return;
    }

  e->entry = ehdr->e_entry;
  e->info.elf.phnum = ehdr->e_phnum;
  phdr = map (e, ehdr->e_phoff, ehdr->e_phnum * sizeof (Elf32_Phdr));
  if (! phdr)
    {
      if (! ferror (&e->stream))
	e->error = EINVAL;
      return;
    }
  e->info.elf.phdr = phdr;

  e->info.elf.anywhere = (ehdr->e_type == ET_DYN ||
			  ehdr->e_type == ET_REL);
  e->info.elf.loadbase = 0;
}

static void
check_elf_phdr (struct execdata *e, const Elf32_Phdr *mapped_phdr,
		vm_address_t *phdr_addr, vm_size_t *phdr_size)
{
  const Elf32_Phdr *phdr;

  memcpy (e->info.elf.phdr, mapped_phdr,
	  e->info.elf.phnum * sizeof (Elf32_Phdr));

  for (phdr = e->info.elf.phdr;
       phdr < &e->info.elf.phdr[e->info.elf.phnum];
       ++phdr)
    switch (phdr->p_type)
      {
      case PT_INTERP:
	e->interp.phdr = phdr;
	break;
      case PT_PHDR:
	if (phdr_addr)
	  *phdr_addr = phdr->p_vaddr & ~(phdr->p_align - 1);
	if (phdr_size)
	  *phdr_size = phdr->p_memsz;
	break;
      case PT_LOAD:
	/* Sanity check.  */
	if (e->file_size <= (off_t) (phdr->p_offset +
				     phdr->p_filesz))
	  e->error = EINVAL;
	break;
      }	      
}


static void
check (struct execdata *e)
{
  check_elf (e);
#ifdef BFD
  if (e->error == ENOEXEC)
    {
      e->error = 0;
      check_bfd (e);
    }
#endif
}


/* Release the conch and clean up mapping the file and control page.  */
static void
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
      e->cntl = NULL;
    }
  if (e->filemap != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), e->filemap);
      e->filemap = MACH_PORT_NULL;
    }
  if (e->cntlmap != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), e->cntlmap);
      e->cntlmap = MACH_PORT_NULL;
    }
}
      
/* Clean up after reading the file (need not be completed).  */
static void
finish (struct execdata *e, int dealloc_file)
{
  finish_mapping (e);
#ifdef	BFD
  if (e->bfd != NULL)
    {
      bfd_close (e->bfd);
      e->bfd = NULL;
    }
  else
#endif
    fclose (&e->stream);
  if (dealloc_file && e->file != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), e->file);
      e->file = MACH_PORT_NULL;
    }
}


/* Load the file.  */
static void
load (task_t usertask, struct execdata *e)
{
  e->task = usertask;

  if (! e->error)
    {
#ifdef	BFD
      if (e->bfd)
	{
	  void load_bfd_section (bfd *bfd, asection *sec, void *userdata)
	    {
	      load_section (sec, userdata);
	    }
	  bfd_map_over_sections (e->bfd, &load_bfd_section, e);
	}
      else
#endif
	{
	  Elf32_Word i;
	  for (i = 0; i < e->info.elf.phnum; ++i)
	    if (e->info.elf.phdr[i].p_type == PT_LOAD)
	      load_section (&e->info.elf.phdr[i], e);

	  /* The entry point address is relative to whereever we loaded the
	     program text.  */
	  e->entry += e->info.elf.loadbase;
	}
    }

  /* Release the conch for the file.  */
  finish_mapping (e);

  if (! e->error)
    {
      /* Do post-loading processing on the task.  */

#ifdef	BFD
      if (e->bfd)
	{
	  /* Do post-loading processing for a section.  This consists of
	     peeking the pages of non-demand-paged executables.  */

	  void postload_section (bfd *bfd, asection *sec, void *userdata)
	    {
	      struct execdata *u = userdata;
	      vm_address_t addr = 0;
	      vm_size_t secsize = 0;

	      addr = (vm_address_t) sec->vma;
	      secsize = sec->_raw_size;

	      if ((sec->flags & SEC_LOAD) && !(bfd->flags & D_PAGED))
		{
		  /* Pre-load the section by peeking every mapped page.  */
		  vm_address_t myaddr, a;
		  vm_size_t mysize;
		  myaddr = 0;
	  
		  /* We have already mapped the file into the task in
		     load_section.  Now read from the task's memory into our
		     own address space so we can peek each page and cause it to
		     be paged in.  */
		  if (u->error = vm_read (u->task,
					  trunc_page (addr), round_page (secsize),
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

	  bfd_map_over_sections (e->bfd, postload_section, e);
	}
    }
#endif

}

#ifdef GZIP
/* Check the file for being a gzip'd image.  Return with ENOEXEC means not
   a valid gzip file; return with another error means lossage in decoding;
   return with zero means the file was uncompressed into memory which E now
   points to, and `check' can be run again.  */

static void
check_gzip (struct execdata *earg)
{
  struct execdata *e = earg;
  /* Entry points to unzip engine.  */
  int get_method (int);
  void unzip (int, int);
  extern long int bytes_out;
  /* Callbacks from unzip for I/O and error interface.  */
  extern int (*unzip_read) (char *buf, size_t maxread);
  extern void (*unzip_write) (const char *buf, size_t nwrite);
  extern void (*unzip_read_error) (void);
  extern void (*unzip_error) (const char *msg);

  char *zipdata = NULL;
  size_t zipdatasz = 0;
  FILE *zipout = NULL;
  jmp_buf ziperr;
  int zipread (char *buf, size_t maxread)
    {
      return fread (buf, 1, maxread, &e->stream);
    }
  void zipwrite (const char *buf, size_t nwrite)
    {
      if (fwrite (buf, nwrite, 1, zipout) != 1)
	longjmp (ziperr, 1);
    }
  void ziprderr (void)
    {
      longjmp (ziperr, 2);
    }
  void ziperror (const char *msg)
    {
      errno = ENOEXEC;
      longjmp (ziperr, 2);
    }

  unzip_read = zipread;
  unzip_write = zipwrite;
  unzip_read_error = ziprderr;
  unzip_error = ziperror;

  if (setjmp (ziperr))
    {
      /* Error in unzipping jumped out.  */
      if (zipout)
	{
	  fclose (zipout);
	  free (zipdata);
	}
      e->error = errno;
      return;
    }

  rewind (&e->stream);
  if (get_method (0) != 0)
    {
      /* Not a happy gzip file.  */
      e->error = ENOEXEC;
      return;
    }

  /* Matched gzip magic number.  Ready to unzip.
     Set up the output stream and let 'er rip.  */

  zipout = open_memstream (&zipdata, &zipdatasz);
  if (! zipout)
    {
      e->error = errno;
      return;
    }

  /* Call the gunzip engine.  */
  bytes_out = 0;
  unzip (17, 23);		/* Arguments ignored.  */

  /* The output is complete.  Clean up the stream and store its resultant
     buffer and size in the execdata as the file contents.  */
  fclose (zipout);
  e->file_data = zipdata;
  e->file_size = zipdatasz;

  /* Clean up the old exec file stream's state.  */
  finish (e, 0);

  /* Point the stream at the buffer of file data.  */
  memset (&e->stream, 0, sizeof (e->stream));
  e->stream.__magic = _IOMAGIC;
  e->stream.__mode.__read = 1;
  e->stream.__buffer = e->file_data;
  e->stream.__bufsize = e->file_size;
  e->stream.__get_limit = e->stream.__buffer + e->stream.__bufsize;
  e->stream.__bufp = e->stream.__buffer;
  e->stream.__seen = 1;
}
#endif

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
  struct execdata e, interp;
  task_t newtask = MACH_PORT_NULL;
  thread_t thread = MACH_PORT_NULL;
  struct bootinfo *boot = 0;
  int secure, defaults;
  vm_address_t phdr_addr = 0;
  vm_size_t phdr_size = 0;

  /* Prime E for executing FILE and check its validity.  This must be an
     inline function because it stores pointers into alloca'd storage in E
     for later use in `load'.  */
  void prepare_and_check (file_t file, struct execdata *e)
    {
      /* Prepare E to read the file.  */
      prepare (file, e);

      /* Check the file for validity first.  */

      check (e);

#ifdef GZIP
      if (e->error == ENOEXEC)
	{
	  /* See if it is a compressed image.  */
	  check_gzip (e);
	  if (e->error == 0)
	    /* The file was uncompressed into memory, and now E describes the
	       uncompressed image rather than the actual file.  Check it again
	       for a valid magic number.  */
	    check (e);
	}
#endif

#if 0
      if (e->error == ENOEXEC)
	/* Check for a #! executable file.  */
	check_hashbang (e, replyport,
			file, oldtask, flags,
			argv, argvlen, argv_copy,
			envp, envplen, envp_copy,
			dtable, dtablesize, dtable_copy,
			portarray, nports, portarray_copy,
			intarray, nints, intarray_copy,
			deallocnames, ndeallocnames,
			destroynames, ndestroynames);
#endif
    }


  /* Here is the main body of the function.  */


  /* Catch this error now, rather than later.  */
  if ((!std_ports || !std_ints) && (flags & (EXEC_SECURE|EXEC_DEFAULTS)))
    return EIEIO;


  /* Prime E for executing FILE and check its validity.  */
  prepare_and_check (file, &e);
  if (! e.error)
    {
#ifdef	BFD
      if (e.bfd)
	{
	  e.info.bfd_locations = alloca (e.bfd->section_count *
					 sizeof (vm_offset_t));
	  bfd_map_over_sections (e.bfd, check_section, &e);
	}
      else
#endif
	{
	  const Elf32_Phdr *phdr = e.info.elf.phdr;
	  e.info.elf.phdr = alloca (e.info.elf.phnum * sizeof (Elf32_Phdr));
	  check_elf_phdr (&e, phdr, &phdr_addr, &phdr_size);
	}
    }


  interp.file = MACH_PORT_NULL;
  if (! e.error && e.interp.section)
    {
      /* There is an interpreter section specifying another file to load
	 along with this executable.  Find the name of the file and open
	 it.  */

#ifdef BFD
      char namebuf[e.bfd ? e.interp.section->_raw_size : 0];
#endif
      char *name;

#ifdef BFD
      if (e.bfd)
	{
	  if (! bfd_get_section_contents (e.bfd, e.interp.section,
					  namebuf, 0,
					  e.interp.section->_raw_size))
	    {
	      e.error = b2he (errno);
	      name = NULL;
	    }
	  else
	    name = namebuf;
	}
      else
#endif
	{
	  name = map (&e, (e.interp.phdr->p_offset
			   & ~(e.interp.phdr->p_align - 1)),
		      e.interp.phdr->p_filesz);
	  if (! name && ! ferror (&e.stream))
	    e.error = EINVAL;
	}

      if (! name)
	e.interp.section = NULL;
      else
	{
	  /* Open the named file using the appropriate directory ports for
	     the user.  */
	  inline mach_port_t user_port (unsigned int idx)
	    {
	      return (((flags & (EXEC_SECURE|EXEC_DEFAULTS)) || idx >= nports)
		      ? std_ports : portarray)
		[idx];
	    }

	  e.error = hurd_file_name_lookup (user_port (INIT_PORT_CRDIR),
					   user_port (INIT_PORT_CWDIR),
					   name, O_READ, 0, &interp.file);
	}
    }
  if (interp.file != MACH_PORT_NULL)
    {
      /* We opened an interpreter file.  Prepare it for loading too.  */
      prepare_and_check (interp.file, &interp);
      if (! interp.error)
	{
#ifdef	BFD
	  if (interp.bfd)
	    {
	      interp.info.bfd_locations = alloca (interp.bfd->section_count *
						  sizeof (vm_offset_t));
	      bfd_map_over_sections (interp.bfd, check_section, &e);
	    }
	  else
#endif
	    {
	      const Elf32_Phdr *phdr = interp.info.elf.phdr;
	      interp.info.elf.phdr = alloca (interp.info.elf.phnum *
					     sizeof (Elf32_Phdr));
	      check_elf_phdr (&interp, phdr, NULL, NULL);
	    }
	}
      e.error = interp.error;
    }

  if (e.error)
    {
      if (e.interp.section)
	finish (&interp, 1);
      finish (&e, 0);
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

/* XXX this should be below
   it is here to work around a vm_map kernel bug. */
  if (interp.file != MACH_PORT_NULL)
    {
      /* Load the interpreter file.  */
      load (newtask, &interp);
      if (interp.error)
	{
	  e.error = interp.error;
	  goto out;
	}
      finish (&interp, 1);
    }


  /* Load the file into the task.  */
  load (newtask, &e);
  if (e.error)
    goto out;

  /* XXX loading of interp belongs here */

  /* Clean up.  */
  finish (&e, 0);

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

#ifdef XXX
  boot->phdr_addr = phdr_addr;
  boot->phdr_size = phdr_size;
  boot->user_entry = e.entry;
#endif

  if (e.error = servercopy ((void **) &argv, argvlen, argv_copy))
    goto bootallocout;
  boot->argv = argv;
  boot->argvlen = argvlen;
  if (e.error = servercopy ((void **) &envp, envplen, envp_copy))
    goto argvout;
  boot->envp = envp;
  boot->envplen = envplen;
  if (e.error = servercopy ((void **) &dtable,
			    dtablesize * sizeof (mach_port_t),
			    dtable_copy))
    goto envpout;
  boot->dtable = dtable;
  boot->dtablesize = dtablesize;
  if (e.error = servercopy ((void **) &portarray,
			    nports * sizeof (mach_port_t),
			    portarray_copy))
    goto portsout;
  boot->portarray = portarray;
  boot->nports = nports;
  if (e.error = servercopy ((void **) &intarray,
			    nints * sizeof (int),
			    intarray_copy))
    {
    bootout:
      vm_deallocate (mach_task_self (),
		     (vm_address_t) intarray, nints * sizeof (int));
    portsout:
      vm_deallocate (mach_task_self (),
		     (vm_address_t) portarray, nports * sizeof (mach_port_t));
    envpout:
      vm_deallocate (mach_task_self (), (vm_address_t) envp, envplen);
    argvout:
      vm_deallocate (mach_task_self (), (vm_address_t) argv, argvlen);
    bootallocout:
      free (boot);
      mach_port_destroy (mach_task_self (), (mach_port_t) boot);
    }

  boot->intarray = intarray;
  boot->nints = nints;
  boot->flags = flags;

  /* Adjust the information in BOOT for the secure and/or defaults flags.  */

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

  /* Note that the parentheses on this first test are different from the
     others below it. */
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
  if (e.error = mach_setup_thread (newtask, thread, 
				   (void *) (e.interp.section ? interp.entry :
					     e.entry),
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

  /* Request no-senders notification on BOOT, so we can release
     its resources if the task dies before calling exec_startup.  */
  {
    mach_port_t unused;
    mach_port_request_notification (mach_task_self (),
				    (mach_port_t) boot,
				    MACH_NOTIFY_NO_SENDERS, 0,
				    (mach_port_t) boot,
				    MACH_MSG_TYPE_MAKE_SEND_ONCE,
				    &unused);
  }

  /* Add BOOT to the server port-set so we will respond to RPCs there.  */
  mach_port_move_member (mach_task_self (),
			 (mach_port_t) boot, request_portset);


 out:
  if (newtask != MACH_PORT_NULL)
    {
      task_terminate (newtask);
      mach_port_deallocate (mach_task_self (), newtask);
    }
  if (e.interp.section)
    finish (&interp, 1);
  finish (&e, !e.error);

  if (oldtask != newtask)
    task_resume (oldtask);
  if (thread != MACH_PORT_NULL)
    {
      thread_resume (thread);
      mach_port_deallocate (mach_task_self (), thread);
    }

  if (! e.error)
    mach_port_deallocate (mach_task_self (), oldtask);

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
S_fsys_set_options (fsys_t fsys,
		    char *data, mach_msg_type_number_t len,
		    int do_children)
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

/* Clean up the storage in BOOT, which was never used.  */
void
deadboot (struct bootinfo *boot)
{
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
      deadboot (boot);
    }

  /* Deallocate the request port.  */
  mach_port_mod_refs (mach_task_self (), port, MACH_PORT_TYPE_RECEIVE, -1);

  /* XXX what is this for??? */
  mach_port_mod_refs (mach_task_self (), request_portset,
		      MACH_PORT_TYPE_RECEIVE, -1);

  return KERN_SUCCESS;
}

/* Attempt to set the active translator for the exec server so that
   filesystems other than the bootstrap can find it. */
void
set_active_trans ()
{
  file_t execnode;
  
  execnode = file_name_lookup (_SERVERS_EXEC, O_NOTRANS | O_CREAT, 0666);
  if (execnode == MACH_PORT_NULL)
    return;
  
  file_set_translator (execnode, 0, FS_TRANS_SET, 0, 0, 0, fsys,
		       MACH_MSG_TYPE_MAKE_SEND);
  /* Don't deallocate EXECNODE here.  If we drop the last reference,
     a bug in ufs might throw away the active translator. XXX */
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

  /* Put the Mach kernel's idea of what flavor of machine this is into the
     fake BFD against which architecture compatibility checks are made.  */
  err = bfd_mach_host_arch_mach (mach_host_self (),
				 &host_bfd.arch_info->arch,
				 &host_bfd.arch_info->mach,
				 &elf_machine);
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

