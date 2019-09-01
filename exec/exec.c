/* GNU Hurd standard exec server.
   Copyright (C) 1992 ,1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001,
   2002, 2004, 2010 Free Software Foundation, Inc.
   Written by Roland McGrath.

   Can exec ELF format directly.

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



#include "priv.h"
#include <mach/gnumach.h>
#include <mach/vm_param.h>
#include <hurd.h>
#include <hurd/exec.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdbool.h>

mach_port_t procserver;	/* Our proc port.  */

/* Standard exec data for secure execs.  */
mach_port_t *std_ports;
int *std_ints;
size_t std_nports, std_nints;
pthread_rwlock_t std_lock = PTHREAD_RWLOCK_INITIALIZER;


#define	b2he()	a2he (errno)

/* Zero the specified region but don't crash the server if it faults.  */

#include <hurd/sigpreempt.h>

/* Load or allocate a section.
   Returns the address of the end of the section.  */
static vm_address_t
load_section (void *section, struct execdata *u)
{
  vm_address_t addr = 0, end = 0;
  vm_offset_t filepos = 0;
  vm_size_t filesz = 0, memsz = 0;
  vm_prot_t vm_prot;
  int anywhere;
  vm_address_t mask = 0;
  const ElfW(Phdr) *const ph = section;

  if (u->error)
    return 0;

  vm_prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;

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
    {
#if 0
      /* XXX: gnumach currently does not support high bits set in mask to prevent
       * loading at high addresses.
       * Instead, in rtld we prevent mappings there through a huge mapping done by
       * fmh().
       */
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
#endif
    }
  if (anywhere && addr < vm_page_size)
    addr = vm_page_size;

  end = addr + memsz;

  if (memsz == 0)
    /* This section is empty; ignore it.  */
    return 0;

  if (filesz != 0)
    {
      vm_address_t mapstart = round_page (addr);

      /* Allocate space in the task and write CONTENTS into it.  */
      void write_to_task (vm_address_t * mapstart, vm_size_t size,
			  vm_prot_t vm_prot, vm_address_t contents)
	{
	  vm_size_t off = size % vm_page_size;
	  /* Allocate with vm_map to set max protections.  */
	  u->error = vm_map (u->task,
			     mapstart, size, mask, anywhere,
			     MACH_PORT_NULL, 0, 1,
			     vm_prot|VM_PROT_WRITE,
			     VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE,
			     VM_INHERIT_COPY);
	  /* vm_write only works on integral multiples of vm_page_size */
	  if (! u->error && size >= vm_page_size)
	    u->error = vm_write (u->task, *mapstart, contents, size - off);
	  if (! u->error && off != 0)
	    {
	      vm_address_t page = 0;
	      page = (vm_address_t) mmap (0, vm_page_size,
					  PROT_READ|PROT_WRITE, MAP_ANON,
					  0, 0);
	      u->error = (page == -1) ? errno : 0;
	      if (! u->error)
		{
		  u->error = hurd_safe_copyin ((void *) page, /* XXX/fault */
			  (void *) (contents + (size - off)),
			  off);
		  if (! u->error)
		    u->error = vm_write (u->task, *mapstart + (size - off),
				         page, vm_page_size);
		  munmap ((caddr_t) page, vm_page_size);
		}
	    }
	  /* Reset the current protections to the desired state.  */
	  if (! u->error && (vm_prot & VM_PROT_WRITE) == 0)
	    u->error = vm_protect (u->task, *mapstart, size, 0, vm_prot);
	}

      if (mapstart - addr < filesz)
	{
	  /* MAPSTART is the first page that starts inside the section.
	     Map all the pages that start inside the section.  */

#define SECTION_IN_MEMORY_P	(u->file_data != NULL)
#define SECTION_CONTENTS	(u->file_data + filepos)
	  if (SECTION_IN_MEMORY_P)
	    /* Data is already in memory; write it into the task.  */
	    write_to_task (&mapstart, filesz - (mapstart - addr), vm_prot,
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
	      const vm_size_t size = filesz - (mapstart - addr);
	      void *buf = map (u, filepos + (mapstart - addr), size);
	      if (buf)
		write_to_task (&mapstart, size, vm_prot, (vm_address_t) buf);
	    }
	  if (u->error)
	    return 0;

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

      /* If this segment is executable, adjust start_code and end_code
	 so that this mapping is within that range.  */
      if (vm_prot & VM_PROT_EXECUTE)
	{
	  if (u->start_code == 0 || u->start_code > addr)
	    u->start_code = addr;

	  if (u->end_code < addr + memsz)
	    u->end_code = addr + memsz;
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

	  u->error = vm_read (u->task, overlap_page, vm_page_size,
			      &ourpage, &size);
	  if (u->error)
	    {
	      if (u->error == KERN_INVALID_ADDRESS)
		{
		  /* The space is unallocated.  */
		  u->error = vm_allocate (u->task,
					  &overlap_page, vm_page_size, 0);
		  size = vm_page_size;
		  if (!u->error)
		    {
		      ourpage = (vm_address_t) mmap (0, vm_page_size,
						     PROT_READ|PROT_WRITE,
						     MAP_ANON, 0, 0);
		      u->error = (ourpage == -1) ? errno : 0;
		    }
		}
	      if (u->error)
		{
		maplose:
		  vm_deallocate (u->task, mapstart, filesz);
		  return 0;
		}
	    }

	  readaddr = (void *) (ourpage + (addr - overlap_page));
	  readsize = size - (addr - overlap_page);
	  if (readsize > filesz)
	    readsize = filesz;

	  if (SECTION_IN_MEMORY_P)
	    memcpy (readaddr, SECTION_CONTENTS, readsize);
	  else
	    {
	      const void *contents = map (u, filepos, readsize);
	      if (!contents)
		goto maplose;
	      u->error = hurd_safe_copyin (readaddr, contents,
	                                   readsize); /* XXX/fault */
	      if (u->error)
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
	  munmap ((caddr_t) ourpage, size);
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
	  u->error = vm_map (u->task, &mapstart, memsz - (mapstart - addr),
			     mask, anywhere, MACH_PORT_NULL, 0, 1,
			     vm_prot, VM_PROT_ALL, VM_INHERIT_COPY);
	  if (u->error)
	    return 0;
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
	  u->error = vm_read (u->task, overlap_page, vm_page_size,
			      &ourpage, &size);
	  if (u->error)
	    {
	      vm_deallocate (u->task, mapstart, memsz);
	      return 0;
	    }
	  u->error = hurd_safe_memset (
				 (void *) (ourpage + (addr - overlap_page)),
				 0,
				 size - (addr - overlap_page));
	  if (! u->error && !(vm_prot & VM_PROT_WRITE))
	    u->error = vm_protect (u->task, overlap_page, size,
				   0, VM_PROT_WRITE);
	  if (! u->error)
	    u->error = vm_write (u->task, overlap_page, ourpage, size);
	  if (! u->error && !(vm_prot & VM_PROT_WRITE))
	    u->error = vm_protect (u->task, overlap_page, size, 0, vm_prot);
	  munmap ((caddr_t) ourpage, size);
	}
    }
  return end;
}

/* XXX all accesses of the mapped data need to use fault handling
   to abort the RPC when mapped file data generates bad page faults.
   I've marked some accesses with XXX/fault comments.
   --roland  */

void *
map (struct execdata *e, off_t posn, size_t len)
{
  const size_t size = e->file_size;
  size_t offset;

  if ((map_filepos (e) & ~(map_vsize (e) - 1)) == (posn & ~(map_vsize (e) - 1))
      && posn + len - map_filepos (e) <= map_fsize (e))
    /* The current mapping window covers it.  */
    offset = posn & (map_vsize (e) - 1);
  else if (posn + len > size)
    /* The requested data wouldn't fit in the file.  */
    return NULL;
  else if (e->file_data != NULL) {
    return e->file_data + posn;
  } else if (e->filemap == MACH_PORT_NULL)
    {
      /* No mapping for the file.  Read the data by RPC.  */
      char *buffer = map_buffer (e);
      mach_msg_type_number_t nread = map_vsize (e);

      assert_backtrace (e->file_data == NULL); /* Must be first or second case.  */

      /* Read as much as we can get into the buffer right now.  */
      e->error = io_read (e->file, &buffer, &nread, posn, round_page (len));
      if (e->error)
	return NULL;
      if (buffer != map_buffer (e))
	{
	  /* The data was returned out of line.  Discard the old buffer.  */
	  if (map_vsize (e) != 0)
	    munmap (map_buffer (e), map_vsize (e));
	  map_buffer (e) = buffer;
	  map_vsize (e) = round_page (nread);
	}

      map_filepos (e) = posn;
      map_set_fsize (e, nread);
      offset = 0;
    }
  else
    {
      /* Deallocate the old mapping area.  */
      if (map_buffer (e) != NULL)
	munmap (map_buffer (e), map_vsize (e));
      map_buffer (e) = NULL;

      /* Make sure our mapping is page-aligned in the file.  */
      offset = posn & (vm_page_size - 1);
      map_filepos (e) = trunc_page (posn);
      map_vsize (e) = round_page (posn + len) - map_filepos (e);

      /* Map the data from the file.  */
      if (vm_map (mach_task_self (),
		  (vm_address_t *) &map_buffer (e), map_vsize (e), 0, 1,
		  e->filemap, map_filepos (e), 1, VM_PROT_READ, VM_PROT_READ,
		  VM_INHERIT_NONE))
	{
	  e->error = EIO;
	  return NULL;
	}

      if (e->cntl)
	e->cntl->accessed = 1;

      map_set_fsize (e, MIN (map_vsize (e), size - map_filepos (e)));
    }

  return map_buffer (e) + offset;
}

/* We don't have a stdio stream, but we have a mapping window
   we need to initialize.  */
static void
prepare_stream (struct execdata *e)
{
  e->map_buffer = NULL;
  e->map_vsize = e->map_fsize = 0;
  e->map_filepos = 0;
}

/* Prepare to check and load FILE.  */
static void
prepare (file_t file, struct execdata *e)
{
  memory_object_t rd, wr;

  e->file = file;

  e->file_data = NULL;
  e->cntl = NULL;
  e->filemap = MACH_PORT_NULL;
  e->cntlmap = MACH_PORT_NULL;

  e->interp.section = NULL;

  e->start_code = 0;
  e->end_code = 0;

  /* Initialize E's stdio stream.  */
  prepare_stream (e);

  /* Try to mmap FILE.  */
  e->error = io_map (file, &rd, &wr);
  if (! e->error)
    /* Mapping is O.K.  */
    {
      if (wr != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), wr);
      if (rd == MACH_PORT_NULL)
	{
	  e->error = EBADF;	/* ? XXX */
	  return;
	}
      e->filemap = rd;

      e->error = /* io_map_cntl (file, &e->cntlmap) */ EOPNOTSUPP; /* XXX */
      if (!e->error)
	e->error = vm_map (mach_task_self (), (vm_address_t *) &e->cntl,
			   vm_page_size, 0, 1, e->cntlmap, 0, 0,
			   VM_PROT_READ|VM_PROT_WRITE,
			   VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);

      if (e->cntl)
	while (1)
	  {
	    pthread_spin_lock (&e->cntl->lock);
	    switch (e->cntl->conch_status)
	      {
	      case USER_COULD_HAVE_CONCH:
		e->cntl->conch_status = USER_HAS_CONCH;
	      case USER_HAS_CONCH:
		pthread_spin_unlock (&e->cntl->lock);
		/* Break out of the loop.  */
		break;
	      case USER_RELEASE_CONCH:
	      case USER_HAS_NOT_CONCH:
	      default:		/* Oops.  */
		pthread_spin_unlock (&e->cntl->lock);
		e->error = io_get_conch (e->file);
		if (e->error)
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

  if (!e->cntl && (!e->error || e->error == EOPNOTSUPP))
    {
      /* No shared page.  Do a stat to find the file size.  */
      struct stat st;
      e->error = io_stat (file, &st);
      if (e->error)
	return;
      e->file_size = st.st_size;
      e->optimal_block = st.st_blksize;
    }
}

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
  ElfW(Ehdr) *ehdr = map (e, 0, sizeof (ElfW(Ehdr)));
  ElfW(Phdr) *phdr;

  if (! ehdr)
    {
      if (!e->error)
	e->error = ENOEXEC;
      return;
    }

  if (*(ElfW(Word) *) ehdr != ((union { ElfW(Word) word;
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
      ehdr->e_ehsize < sizeof *ehdr ||
      ehdr->e_phentsize != sizeof (ElfW(Phdr)))
    {
      e->error = ENOEXEC;
      return;
    }
  e->error = elf_machine_matches_host (ehdr->e_machine);
  if (e->error)
    return;

  /* Extract all this information now, while EHDR is mapped.
     The `map' call below for the phdrs may reuse the mapping window.  */
  e->entry = ehdr->e_entry;
  e->info.elf.anywhere = (ehdr->e_type == ET_DYN ||
			  ehdr->e_type == ET_REL);
  e->info.elf.loadbase = 0;
  e->info.elf.phnum = ehdr->e_phnum;

  phdr = map (e, ehdr->e_phoff, ehdr->e_phnum * sizeof (ElfW(Phdr)));
  if (! phdr)
    {
      if (!e->error)
	e->error = ENOEXEC;
      return;
    }
  e->info.elf.phdr = phdr;
  e->info.elf.phdr_addr = ehdr->e_phoff;
}

/* Copy MAPPED_PHDR into E->info.elf.phdr, filling in E->interp.phdr
   in the process.  */
static void
check_elf_phdr (struct execdata *e, const ElfW(Phdr) *mapped_phdr)
{
  const ElfW(Phdr) *phdr;
  bool seen_phdr = false;

  memcpy (e->info.elf.phdr, mapped_phdr,
	  e->info.elf.phnum * sizeof (ElfW(Phdr)));

  /* Default state if we do not see PT_GNU_STACK telling us what to do.
     Executable stack is the compatible default.
     (XXX should be machine-dependent??)
  */
  e->info.elf.execstack = 1;

  for (phdr = e->info.elf.phdr;
       phdr < &e->info.elf.phdr[e->info.elf.phnum];
       ++phdr)
    switch (phdr->p_type)
      {
      case PT_INTERP:
	e->interp.phdr = phdr;
	break;
      case PT_LOAD:
	if (e->file_size <= (off_t) (phdr->p_offset +
				     phdr->p_filesz))
	  {
	    e->error = ENOEXEC;
	    return;
	  }
	/* Check if this is the segment that contains the phdr image.  */
	if (!seen_phdr
	    && (phdr->p_offset & -phdr->p_align) == 0 /* Sanity check.  */
	    && phdr->p_offset <= e->info.elf.phdr_addr
	    && e->info.elf.phdr_addr - phdr->p_offset < phdr->p_filesz)
	  {
	    e->info.elf.phdr_addr += phdr->p_vaddr - phdr->p_offset;
	    seen_phdr = true;
	  }
	break;
      case PT_GNU_STACK:
	e->info.elf.execstack = phdr->p_flags & PF_X;
	break;
      }

  if (!seen_phdr)
    e->info.elf.phdr_addr = 0;
}


static void
check (struct execdata *e)
{
  check_elf (e);		/* XXX/fault */
}


/* Release the conch and clean up mapping the file and control page.  */
static void
finish_mapping (struct execdata *e)
{
  if (e->cntl != NULL)
    {
      pthread_spin_lock (&e->cntl->lock);
      if (e->cntl->conch_status == USER_RELEASE_CONCH)
	{
	  pthread_spin_unlock (&e->cntl->lock);
	  io_release_conch (e->file);
	}
      else
	{
	  e->cntl->conch_status = USER_HAS_NOT_CONCH;
	  pthread_spin_unlock (&e->cntl->lock);
	}
      munmap (e->cntl, vm_page_size);
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

/* Clean up after reading the file (need not be completed).
   Note: this may be called several times for the E, so it must take care
   of checking what was already freed.  */
void
finish (struct execdata *e, int dealloc_file)
{
  finish_mapping (e);
    {
      if (e->file_data != NULL) {
	free (e->file_data);
	e->file_data = NULL;
      } else if (map_buffer (e) != NULL) {
	munmap (map_buffer (e), map_vsize (e));
	map_buffer (e) = NULL;
      }
    }
  if (dealloc_file && e->file != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), e->file);
      e->file = MACH_PORT_NULL;
    }
}

/* Set the name of the new task so that the kernel can use it in error
   messages.  If PID is not zero, it will be included the name.  */
static void
set_name (task_t task, const char *exec_name, pid_t pid)
{
  char *name;
  int size;

  if (pid)
    size = asprintf (&name, "%s(%d)", exec_name, pid);
  else
    size = asprintf (&name, "%s", exec_name);

  if (size == 0)
    return;

  /* This is an internal implementational detail of the GNU Mach kernel.  */
#define TASK_NAME_SIZE	32
  if (size < TASK_NAME_SIZE)
    task_set_name (task, name);
  else
    {
      char *abbr = name + size - TASK_NAME_SIZE + 1;
      abbr[0] = abbr[1] = abbr[2] = '.';
      task_set_name (task, abbr);
    }
#undef TASK_NAME_SIZE

  free (name);
}

/* Load the file.  Returns the address of the end of the load.  */
static vm_offset_t
load (task_t usertask, struct execdata *e, vm_offset_t anywhere_start)
{
  int anywhere = e->info.elf.anywhere;
  vm_offset_t end;
  e->task = usertask;

  if (! e->error)
    {
      ElfW(Word) i;

      if (anywhere && anywhere_start)
	{
	  /* Make sure this anywhere-load will go at the end of the previous
	     anywhere-load.  */
	  /* TODO: Rather compute how much contiguous room is needed, allocate
	     the area from the kernel, and then map memory sections.  */
	  /* TODO: Possibly implement Adresse Space Layout Randomization.  */
	  e->info.elf.loadbase = anywhere_start;
	  e->info.elf.anywhere = 0;
	}

      for (i = 0; i < e->info.elf.phnum; ++i)
	if (e->info.elf.phdr[i].p_type == PT_LOAD)
	  {
	    end = load_section (&e->info.elf.phdr[i], e);
	    if (anywhere && end > anywhere_start)
	      /* This section pushes the next anywhere-load further */
	      anywhere_start = end;
	  }

      /* The entry point address is relative to wherever we loaded the
	 program text.  */
      e->entry += e->info.elf.loadbase;
    }

  /* Release the conch for the file.  */
  finish_mapping (e);

  /* Return potentially-new start for anywhere-loads.  */
  return round_page (anywhere_start);
}


static inline void *
servercopy (void *arg, mach_msg_type_number_t argsize, boolean_t argcopy,
	    error_t *errorp)
{
  if (! argcopy)
    return arg;
  if (! argsize)
    return NULL;

  /* ARG came in-line, so we must copy it.  */
  void *copy;
  copy = mmap (0, argsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  if (copy == MAP_FAILED)
    {
      *errorp = errno;
      return NULL;
    }
  memcpy (copy, arg, argsize);
  return copy;
}


static error_t
do_exec (file_t file,
	 task_t oldtask,
	 int flags,
	 char *path,
	 char *abspath,
	 char *argv, mach_msg_type_number_t argvlen, boolean_t argv_copy,
	 char *envp, mach_msg_type_number_t envplen, boolean_t envp_copy,
	 mach_port_t *dtable, mach_msg_type_number_t dtablesize,
	 boolean_t dtable_copy,
	 mach_port_t *portarray, mach_msg_type_number_t nports,
	 boolean_t portarray_copy,
	 int *intarray, mach_msg_type_number_t nints, boolean_t intarray_copy,
	 mach_port_t *deallocnames, mach_msg_type_number_t ndeallocnames,
	 mach_port_t *destroynames, mach_msg_type_number_t ndestroynames)
{
  struct execdata e, interp;
  task_t newtask = MACH_PORT_NULL;
  thread_t thread = MACH_PORT_NULL;
  struct bootinfo *boot = 0;
  int *ports_replaced;
  int secure, defaults;
  mach_msg_type_number_t i;
  int intarray_dealloc = 0;	/* Dealloc INTARRAY before returning?  */
  int oldtask_trashed = 0;	/* Have we trashed the old task?  */
  vm_address_t anywhere_start = 0;

  /* Prime E for executing FILE and check its validity.  This must be an
     inline function because it stores pointers into alloca'd storage in E
     for later use in `load'.  */
  void prepare_and_check (file_t file, struct execdata *e)
    {
      /* Prepare E to read the file.  */
      prepare (file, e);
      if (e->error)
	return;

      /* Check the file for validity first.  */
      check (e);
    }


  /* Here is the main body of the function.  */

  interp.file = MACH_PORT_NULL;

  /* Catch this error now, rather than later.  */
  /* XXX For EXEC_DEFAULTS, this is only an error if one of the user's
     ports is null; if they are all provided, then EXEC_DEFAULTS would
     have no effect, and the lack of installed standard ports should
     not cause an error.  -mib */
  if ((!std_ports || !std_ints) && (flags & (EXEC_SECURE|EXEC_DEFAULTS)))
    return EIEIO;

  /* Suspend the existing task before frobnicating it.  */
  if (oldtask != MACH_PORT_NULL && (e.error = task_suspend (oldtask)))
    return e.error;

  /* Prime E for executing FILE and check its validity.  */
  prepare_and_check (file, &e);

  if (e.error == ENOEXEC)
    {
      /* Check for a #! executable file.  */
      check_hashbang (&e,
		      file, oldtask, flags, path,
		      argv, argvlen, argv_copy,
		      envp, envplen, envp_copy,
		      dtable, dtablesize, dtable_copy,
		      portarray, nports, portarray_copy,
		      intarray, nints, intarray_copy,
		      deallocnames, ndeallocnames,
		      destroynames, ndestroynames);
      if (! e.error)
	/* The #! exec succeeded; nothing more to do.  */
	return 0;
    }

  if (e.error)
    /* The file is not a valid executable.  */
    goto out;

  const ElfW(Phdr) *phdr = e.info.elf.phdr;
  e.info.elf.phdr = alloca (e.info.elf.phnum * sizeof (ElfW(Phdr)));
  check_elf_phdr (&e, phdr);

  if (oldtask == MACH_PORT_NULL)
    flags |= EXEC_NEWTASK;

  if (flags & (EXEC_NEWTASK|EXEC_SECURE))
    {
      /* Create the new task.  If we are not being secure, then use OLDTASK
	 for the task_create RPC, in case it is something magical.  */
      e.error = task_create (((flags & EXEC_SECURE) ||
			      oldtask == MACH_PORT_NULL) ?
			     mach_task_self () : oldtask,
#ifdef KERN_INVALID_LEDGER
			       NULL, 0,	/* OSF Mach */
#endif
			     0, &newtask);
      if (e.error)
	goto out;
    }
  else
    newtask = oldtask;


  pthread_rwlock_rdlock (&std_lock);
  {
    /* Store the data that we will give in response
       to the RPC on the new task's bootstrap port.  */

    /* Set boot->portarray[IDX] to NEW.  If REAUTH is nonzero,
       io_reauthenticate NEW and set it to the authenticated port.
       If CONSUME is nonzero, a reference on NEW is consumed;
       it is invalid to give nonzero values to both REAUTH and CONSUME.  */
#define use(idx, new, reauth, consume) \
  do { use1 (idx, new, reauth, consume); \
       if (e.error) goto stdout; } while (0)
    void use1 (unsigned int idx, mach_port_t new,
	       int reauth, int consume)
      {
	if (new != MACH_PORT_NULL && reauth)
	  {
	    mach_port_t ref = mach_reply_port (), authed;
	    /* MAKE_SEND is safe here because we destroy REF ourselves. */
	    e.error = io_reauthenticate (new, ref, MACH_MSG_TYPE_MAKE_SEND);
	    if (! e.error)
	      e.error = auth_user_authenticate
		(boot->portarray[INIT_PORT_AUTH],
		 ref, MACH_MSG_TYPE_MAKE_SEND, &authed);
	    mach_port_destroy (mach_task_self (), ref);
	    if (e.error)
	      return;
	    new = authed;
	  }
	else
	  {
	    if (!consume && new != MACH_PORT_NULL)
	      mach_port_mod_refs (mach_task_self (),
				  new, MACH_PORT_RIGHT_SEND, 1);
	  }

	boot->portarray[idx] = new;
	ports_replaced[idx] = 1;
      }

    e.error = ports_create_port (execboot_portclass, port_bucket,
				 sizeof *boot, &boot);
    if (boot == NULL)
      {
      stdout:
	pthread_rwlock_unlock (&std_lock);
	goto out;
      }
    memset (&boot->pi + 1, 0, (char *) &boot[1] - (char *) (&boot->pi + 1));

    /* These flags say the information we pass through to the new program
       may need to be modified.  */
    secure = (flags & EXEC_SECURE);
    defaults = (flags & EXEC_DEFAULTS);

    /* Now record the big blocks of data we shuffle around.
       Whatever arrived inline, we must allocate space for so it can
       survive after this RPC returns.  */

    boot->flags = flags;

    argv = servercopy (argv, argvlen, argv_copy, &e.error);
    if (e.error)
      goto stdout;
    boot->argv = argv;
    boot->argvlen = argvlen;

    if (abspath && abspath[0] == '/')
      {
	/* Explicit absolute filename, put its dirname in the LD_ORIGIN_PATH
	   environment variable for $ORIGIN rpath expansion. */
	const char *end = strrchr (abspath, '/');
	size_t pathlen;
	const char ld_origin_s[] = "\0LD_ORIGIN_PATH=";
	const char *existing;
	size_t existing_len = 0;
	size_t new_envplen;
	char *new_envp;

	/* Drop trailing slashes.  */
	while (end > abspath && end[-1] == '/')
	  end--;

	if (end == abspath)
	  /* Root, keep explicit heading/trailing slash.   */
	  end++;

	pathlen = end - abspath;

	if (memcmp (envp, ld_origin_s + 1, sizeof (ld_origin_s) - 2) == 0)
	  /* Existing variable at the beginning of envp.  */
	  existing = envp - 1;
	else
	  /* Look for the definition.  */
	  existing = memmem (envp, envplen, ld_origin_s, sizeof (ld_origin_s) - 1);

	if (existing)
	  {
	    /* Definition already exists, just replace the content.  */
	    existing += sizeof (ld_origin_s) - 1;
	    existing_len = strnlen (existing, envplen - (existing - envp));

	    /* Allocate room for the new content.  */
	    new_envplen = envplen - existing_len + pathlen;
	    new_envp = mmap (0, new_envplen,
			     PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	    if (new_envp == MAP_FAILED)
	      {
		e.error = errno;
		goto stdout;
	      }

	    /* And copy.  */
	    memcpy (new_envp, envp, existing - envp);
	    memcpy (new_envp + (existing - envp), abspath, pathlen);
	    memcpy (new_envp + (existing - envp) + pathlen,
		    existing + existing_len,
		    envplen - ((existing - envp) + existing_len));
	  }
	else
	  {
	    /* No existing definition, prepend one.  */
	    new_envplen = sizeof (ld_origin_s) - 1 + pathlen + envplen;
	    new_envp = mmap (0, new_envplen,
			     PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

	    memcpy (new_envp, ld_origin_s + 1, sizeof (ld_origin_s) - 2);
	    memcpy (new_envp + sizeof (ld_origin_s) - 2, abspath, pathlen);
	    new_envp [sizeof (ld_origin_s) - 2 + pathlen] = 0;
	    memcpy (new_envp + sizeof (ld_origin_s) - 2 + pathlen + 1, envp, envplen);
	  }

	if (! envp_copy)
	  /* Deallocate original environment */
	  munmap (envp, envplen);

	envp = new_envp;
	envplen = new_envplen;
      }
    else
      {
	/* No explicit abspath, just copy the existing environment */
	envp = servercopy (envp, envplen, envp_copy, &e.error);
	if (e.error)
	  goto stdout;
      }

    boot->envp = envp;
    boot->envplen = envplen;

    dtable = servercopy (dtable, dtablesize * sizeof (mach_port_t),
			 dtable_copy, &e.error);
    if (e.error)
      goto stdout;
    boot->dtable = dtable;
    boot->dtablesize = dtablesize;

    if ((secure || defaults) && nints < INIT_INT_MAX)
      {
	/* Make sure the intarray is at least big enough.  */
	if (intarray_copy || (round_page (nints * sizeof (int)) <
			      round_page (INIT_INT_MAX * sizeof (int))))
	  {
	    /* Allocate a new vector that is big enough.  */
	    boot->intarray = mmap (0, INIT_INT_MAX * sizeof (int),
				   PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	    memcpy (boot->intarray, intarray, nints * sizeof (int));
	    intarray_dealloc = !intarray_copy;
	  }
	else
	  boot->intarray = intarray;
	boot->nints = INIT_INT_MAX;
      }
    else
      {
	intarray = servercopy (intarray, nints * sizeof (int), intarray_copy,
			       &e.error);
	if (e.error)
	  goto stdout;
	boot->intarray = intarray;
	boot->nints = nints;
      }

    if (secure)
      boot->intarray[INIT_UMASK] = std_ints ? std_ints[INIT_UMASK] : CMASK;

    /* Now choose the ports to give the new program.  */

    boot->nports = nports < INIT_PORT_MAX ? INIT_PORT_MAX : nports;
    boot->portarray = mmap (0, boot->nports * sizeof (mach_port_t),
			    PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
    /* Start by copying the array as passed.  */
    for (i = 0; i < nports; ++i)
      boot->portarray[i] = portarray[i];
    if (MACH_PORT_NULL != 0)
      for (; i < boot->nports; ++i)
	boot->portarray[i] = MACH_PORT_NULL;
    /* Keep track of which ports in BOOT->portarray come from the original
       PORTARRAY, and which we replace.  */
    ports_replaced = alloca (boot->nports * sizeof *ports_replaced);
    memset (ports_replaced, 0, boot->nports * sizeof *ports_replaced);

    if (portarray[INIT_PORT_BOOTSTRAP] == MACH_PORT_NULL &&
	oldtask != MACH_PORT_NULL)
      {
	if (! task_get_bootstrap_port (oldtask,
				       &boot->portarray[INIT_PORT_BOOTSTRAP]))
	  ports_replaced[INIT_PORT_BOOTSTRAP] = 1;
      }

    /* Note that the parentheses on this first test are different from the
       others below it. */
    if ((secure || defaults)
	&& boot->portarray[INIT_PORT_AUTH] == MACH_PORT_NULL)
      /* Q: Doesn't this let anyone run a program and make it
	 get a root auth port?
	 A: No; the standard port for INIT_PORT_AUTH has no UID's at all.
	 See init.trim/init.c (init_stdarrays).  */
      use (INIT_PORT_AUTH, std_ports[INIT_PORT_AUTH], 0, 0);
    if (secure || (defaults
		   && boot->portarray[INIT_PORT_PROC] == MACH_PORT_NULL))
      {
	/* Ask the proc server for the proc port for this task.  */
	mach_port_t new;

	e.error = proc_task2proc (procserver, newtask, &new);
	if (e.error)
	  goto stdout;
	use (INIT_PORT_PROC, new, 0, 1);
      }
    else if (oldtask != newtask && oldtask != MACH_PORT_NULL
	     && boot->portarray[INIT_PORT_PROC] != MACH_PORT_NULL)
      {
	mach_port_t new;
	/* This task port refers to the old task; use it to fetch a new
	   one for the new task.  */
	e.error = proc_task2proc (boot->portarray[INIT_PORT_PROC],
				  newtask, &new);
	if (e.error)
	  goto stdout;
	use (INIT_PORT_PROC, new, 0, 1);
      }
    if (secure || (defaults
		   && boot->portarray[INIT_PORT_CRDIR] == MACH_PORT_NULL))
      use (INIT_PORT_CRDIR, std_ports[INIT_PORT_CRDIR], 1, 0);
    if ((secure || defaults)
	&& boot->portarray[INIT_PORT_CWDIR] == MACH_PORT_NULL)
      use (INIT_PORT_CWDIR, std_ports[INIT_PORT_CWDIR], 1, 0);
  }
  pthread_rwlock_unlock (&std_lock);


  /* We have now concocted in BOOT the complete Hurd context (ports and
     ints) that the new program image will run under.  We will use these
     ports for looking up the interpreter file if there is one.  */

  if (! e.error && e.interp.section)
    {
      /* There is an interpreter section specifying another file to load
	 along with this executable.  Find the name of the file and open
	 it.  */

      char *name = map (&e, (e.interp.phdr->p_offset
			     & ~(e.interp.phdr->p_align - 1)),
			e.interp.phdr->p_filesz);
      if (! name && ! e.error)
	e.error = ENOEXEC;

      if (! name)
	e.interp.section = NULL;
      else
	{
	  /* Open the named file using the appropriate directory ports for
	     the user.  */
	  error_t user_port (int which, error_t (*operate) (mach_port_t))
	    {
	      return (*operate) (boot->nports > which ?
				 boot->portarray[which] :
				 MACH_PORT_NULL);
	    }
	  file_t user_fd (int fd)
	    {
	      if (fd < 0 || fd >= boot->dtablesize ||
		  boot->dtable[fd] == MACH_PORT_NULL)
		{
		  errno = EBADF;
		  return MACH_PORT_NULL;
		}
	      mach_port_mod_refs (mach_task_self (), boot->dtable[fd],
				  MACH_PORT_RIGHT_SEND, +1);
	      return boot->dtable[fd];
	    }
				/* XXX/fault */
	  e.error = hurd_file_name_lookup (&user_port, &user_fd, 0,
					   name, O_READ, 0, &interp.file);
	}
    }

  if (interp.file != MACH_PORT_NULL)
    {
      /* We opened an interpreter file.  Prepare it for loading too.  */
      prepare_and_check (interp.file, &interp);
      if (! interp.error)
	{
	  const ElfW(Phdr) *phdr = interp.info.elf.phdr;
	  interp.info.elf.phdr = alloca (interp.info.elf.phnum *
					 sizeof (ElfW(Phdr)));
	  check_elf_phdr (&interp, phdr);
	}
      e.error = interp.error;
    }

  if (e.error)
    goto out;


  /* We are now committed to the exec.  It "should not fail".
     If it does fail now, the task will be hopelessly munged.  */

  if (newtask == oldtask)
    {
      thread_t *threads;
      mach_msg_type_number_t nthreads, i;

      /* Terminate all the threads of the old task.  */

      e.error = task_threads (oldtask, &threads, &nthreads);
      if (e.error)
	goto out;
      for (i = 0; i < nthreads; ++i)
	{
	  thread_terminate (threads[i]);
	  mach_port_deallocate (mach_task_self (), threads[i]);
	}
      munmap ((caddr_t) threads, nthreads * sizeof (thread_t));

      /* Deallocate the entire virtual address space of the task.  */

      vm_deallocate (oldtask,
		     VM_MIN_ADDRESS, VM_MAX_ADDRESS - VM_MIN_ADDRESS);

      /* Nothing is supposed to go wrong any more.  If anything does, the
	 old task is now in a hopeless state and must be killed.  */
      oldtask_trashed = 1;

      /* Deallocate and destroy the ports requested by the caller.
	 These are ports the task wants not to lose if the exec call
	 fails, but wants removed from the new program task.  */

      for (i = 0; i < ndeallocnames; ++i)
	mach_port_deallocate (oldtask, deallocnames[i]);

      for (i = 0; i < ndestroynames; ++i)
	mach_port_destroy (oldtask, destroynames[i]);
    }

  /* Map page zero redzoned.  */
  {
    vm_address_t addr = 0;
    e.error = vm_map (newtask,
		      &addr, vm_page_size, 0, 0, MACH_PORT_NULL, 0, 1,
		      VM_PROT_NONE, VM_PROT_NONE, VM_INHERIT_COPY);
    if (e.error)
      goto out;
  }

/* XXX this should be below
   it is here to work around a vm_map kernel bug. */
  if (interp.file != MACH_PORT_NULL)
    {
      /* Load the interpreter file.  */
      anywhere_start = load (newtask, &interp, anywhere_start);
      if (interp.error)
	{
	  e.error = interp.error;
	  goto out;
	}
      finish (&interp, 1);
    }


  /* Leave room for mmaps etc. before PIE binaries.
   * Could add address randomization here.  */
  anywhere_start += 128 << 20;
  /* Load the file into the task.  */
  anywhere_start = load (newtask, &e, anywhere_start);
  if (e.error)
    goto out;

  /* XXX loading of interp belongs here */

  /* Clean up.  */
  finish (&e, 0);

  /* Now record some essential addresses from the image itself that the
     program's startup code will need to know.  We do this after loading
     the image so that a load-anywhere image gets the adjusted addresses.  */
    if (e.info.elf.phdr_addr != 0)
      {
	e.info.elf.phdr_addr += e.info.elf.loadbase;
	boot->phdr_addr = e.info.elf.phdr_addr;
	boot->phdr_size = e.info.elf.phnum * sizeof (ElfW(Phdr));
      }
  boot->user_entry = e.entry;	/* already adjusted in `load' */

  /* /hurd/exec is used to start /hurd/proc, so at this point there is
     no proc server, so we need to be careful here.  */
  if (boot->portarray[INIT_PORT_PROC] != MACH_PORT_NULL)
    {
      /* Set the start_code and end_code values for this process.  */
      e.error = proc_set_code (boot->portarray[INIT_PORT_PROC],
			       e.start_code, e.end_code);
      if (e.error)
	goto out;

      pid_t pid;
      e.error = proc_task2pid (boot->portarray[INIT_PORT_PROC],
			       newtask, &pid);
      if (e.error)
	goto out;

      if (abspath)
	proc_set_exe (boot->portarray[INIT_PORT_PROC], abspath);

      set_name (newtask, argv, pid);

      e.error = proc_set_entry (boot->portarray[INIT_PORT_PROC],
			        e.entry);
      if (e.error)
	goto out;
    }
  else
    set_name (newtask, argv, 0);

  /* Create the initial thread.  */
  e.error = thread_create (newtask, &thread);
  if (e.error)
    goto out;

  /* Start up the initial thread at the entry point.  */
  boot->stack_base = 0, boot->stack_size = 0; /* Don't care about values.  */
  e.error = mach_setup_thread (newtask, thread,
			       (void *) (e.interp.section ? interp.entry :
					 e.entry),
			       &boot->stack_base, &boot->stack_size);
  if (e.error)
    goto out;

  /* It would probably be better to change mach_setup_thread so
     it does a vm_map with the right permissions to start with.  */
  if (!e.info.elf.execstack)
    e.error = vm_protect (newtask, boot->stack_base, boot->stack_size,
			  0, VM_PROT_READ | VM_PROT_WRITE);

  if (oldtask != newtask && oldtask != MACH_PORT_NULL)
    {
      /* The program is on its way.  The old task can be nuked.  */
      process_t proc;
      process_t psrv;

      /* Use the canonical proc server if secure, or there is none other.
	 When not secure, it is nice to let processes associate with
	 whatever proc server turns them on, regardless of which exec
	 itself is using.  */
      if (secure
	  || boot->nports <= INIT_PORT_PROC
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

  /* Make sure the proc server has the right idea of our identity. */
  if (secure)
    {
      uid_t euidbuf[10], egidbuf[10], auidbuf[10], agidbuf[10];
      uid_t *euids, *egids, *auids, *agids;
      size_t neuids, negids, nauids, nagids;
      error_t err;

      /* Find out what our UID is from the auth server. */
      neuids = negids = nauids = nagids = 10;
      euids = euidbuf, egids = egidbuf;
      auids = auidbuf, agids = agidbuf;
      err = auth_getids (boot->portarray[INIT_PORT_AUTH],
			     &euids, &neuids, &auids, &nauids,
			     &egids, &negids, &agids, &nagids);

      if (!err)
	{
	  /* Set the owner with the proc server */
	  /* Not much we can do about errors here; caller is responsible
	     for making sure that the provided proc port is correctly
	     authenticated anyhow. */
	  proc_setowner (boot->portarray[INIT_PORT_PROC],
			 neuids ? euids[0] : 0, !neuids);

	  /* Clean up */
	  if (euids != euidbuf)
	    munmap (euids, neuids * sizeof (uid_t));
	  if (egids != egidbuf)
	    munmap (egids, negids * sizeof (uid_t));
	  if (auids != auidbuf)
	    munmap (auids, nauids * sizeof (uid_t));
	  if (agids != agidbuf)
	    munmap (agids, nagids * sizeof (uid_t));
	}
    }

  {
    mach_port_t btport = ports_get_send_right (boot);
    e.error = task_set_bootstrap_port (newtask, btport);
    mach_port_deallocate (mach_task_self (), btport);
  }

 out:
  if (interp.file != MACH_PORT_NULL)
    finish (&interp, 1);
  finish (&e, !e.error);

  if (!e.error && (flags & EXEC_SIGTRAP)) /* XXX && !secure ? */
    {
      /* This is a "traced" exec, i.e. the new task is to be debugged.  The
	 caller has requested that the new process stop with SIGTRAP before
	 it starts.  Since the process has no signal thread yet to do its
	 own POSIX signal mechanics, we simulate it by notifying the proc
	 server of the signal and leaving the initial thread with a suspend
	 count of one, as it would be if the process were stopped by a
	 POSIX signal.  */
      mach_port_t proc;
      if (boot->nports > INIT_PORT_PROC)
	proc = boot->portarray[INIT_PORT_PROC];
      else
	/* Ask the proc server for the proc port for this task.  */
	e.error = proc_task2proc (procserver, newtask, &proc);
      if (!e.error)
	/* Tell the proc server that the process has stopped with the
	   SIGTRAP signal.  Don't bother to check for errors from the RPC
	   here; for non-secure execs PROC may be the user's own proc
	   server its confusion shouldn't make the exec fail.  */
	proc_mark_stop (proc, SIGTRAP, 0);
    }

  if (boot)
    {
      /* Release the original reference.  Now there is only one
	 reference, which will be released on no-senders notification.
	 If we are bailing out due to error before setting the task's
	 bootstrap port, this will be the last reference and BOOT
	 will get cleaned up here.  */

      if (e.error)
	/* Kill the pointers to the argument information so the cleanup
	   of BOOT doesn't deallocate it.  It will be deallocated my MiG
	   when we return the error.  */
	memset (&boot->pi + 1, 0,
		(char *) &boot[1] - (char *) (&boot->pi + 1));
      else
	/* Do this before we release the last reference.  */
	if (boot->nports > INIT_PORT_PROC)
	  proc_mark_exec (boot->portarray[INIT_PORT_PROC]);

      ports_port_deref (boot);
    }

  if (thread != MACH_PORT_NULL)
    {
      if (!e.error && !(flags & EXEC_SIGTRAP))
	thread_resume (thread);
      mach_port_deallocate (mach_task_self (), thread);
    }

  if (e.error)
    {
      if (oldtask != newtask)
	{
	  /* We created a new task but failed to set it up.  Kill it.  */
	  task_terminate (newtask);
	  mach_port_deallocate (mach_task_self (), newtask);
	}
      if (oldtask_trashed)
	/* The old task is hopelessly trashed; there is no way it
	   can resume execution.  Coup de grace.  */
	task_terminate (oldtask);
      else
	/* Resume the old task, which we suspended earlier.  */
	task_resume (oldtask);
    }
  else
    {
      if (oldtask != newtask)
	{
	  /* We successfully set the new task up.
	     Terminate the old task and deallocate our right to it.  */
	  task_terminate (oldtask);
	  mach_port_deallocate (mach_task_self (), oldtask);
	}
      else
	/* Resume the task, it is ready to run the new program.  */
	task_resume (oldtask);
      /* Deallocate the right to the new task we created.  */
      mach_port_deallocate (mach_task_self (), newtask);

      for (i = 0; i < nports; ++i)
	if (ports_replaced[i] && portarray[i] != MACH_PORT_NULL)
	  /* This port was replaced, so the reference that arrived in the
	     original portarray is not being saved in BOOT for transfer to
	     the user task.  Deallocate it; we don't want it, and MiG will
	     leave it for us on successful return.  */
	  mach_port_deallocate (mach_task_self (), portarray[i]);

      /* If there is vm_allocate'd space for the original intarray and/or
	 portarray, and we are not saving those pointers in BOOT for later
	 transfer, deallocate the original space now.  */
      if (intarray_dealloc)
	munmap (intarray, nints * sizeof intarray[0]);
      if (!portarray_copy)
	munmap (portarray, nports * sizeof portarray[0]);
    }

  return e.error;
}

/* Deprecated.  */
kern_return_t
S_exec_exec (struct trivfs_protid *protid,
	     file_t file,
	     task_t oldtask,
	     int flags,
	     data_t argv, mach_msg_type_number_t argvlen, boolean_t argv_copy,
	     data_t envp, mach_msg_type_number_t envplen, boolean_t envp_copy,
	     mach_port_t *dtable, mach_msg_type_number_t dtablesize,
	     boolean_t dtable_copy,
	     mach_port_t *portarray, mach_msg_type_number_t nports,
	     boolean_t portarray_copy,
	     int *intarray, mach_msg_type_number_t nints,
	     boolean_t intarray_copy,
	     mach_port_t *deallocnames, mach_msg_type_number_t ndeallocnames,
	     mach_port_t *destroynames, mach_msg_type_number_t ndestroynames)
{
  return S_exec_exec_paths (protid,
				file,
				oldtask,
				flags,
				"",
				"",
				argv, argvlen, argv_copy,
				envp, envplen, envp_copy,
				dtable, dtablesize,
				dtable_copy,
				portarray, nports,
				portarray_copy,
				intarray, nints,
				intarray_copy,
				deallocnames, ndeallocnames,
				destroynames, ndestroynames);
}

kern_return_t
S_exec_exec_paths (struct trivfs_protid *protid,
		       file_t file,
		       task_t oldtask,
		       int flags,
		       char *path,
		       char *abspath,
		       char *argv, mach_msg_type_number_t argvlen,
		       boolean_t argv_copy,
		       char *envp, mach_msg_type_number_t envplen,
		       boolean_t envp_copy,
		       mach_port_t *dtable, mach_msg_type_number_t dtablesize,
		       boolean_t dtable_copy,
		       mach_port_t *portarray, mach_msg_type_number_t nports,
		       boolean_t portarray_copy,
		       int *intarray, mach_msg_type_number_t nints,
		       boolean_t intarray_copy,
		       mach_port_t *deallocnames,
		       mach_msg_type_number_t ndeallocnames,
		       mach_port_t *destroynames,
		       mach_msg_type_number_t ndestroynames)
{
  if (! protid)
    return EOPNOTSUPP;

  /* There were no user-specified exec servers,
     or none of them could be found.  */

  return do_exec (file, oldtask, flags, path, abspath,
		  argv, argvlen, argv_copy,
		  envp, envplen, envp_copy,
		  dtable, dtablesize, dtable_copy,
		  portarray, nports, portarray_copy,
		  intarray, nints, intarray_copy,
		  deallocnames, ndeallocnames,
		  destroynames, ndestroynames);
}

kern_return_t
S_exec_setexecdata (struct trivfs_protid *protid,
		    mach_port_t *ports, mach_msg_type_number_t nports, int ports_copy,
		    int *ints, mach_msg_type_number_t nints, int ints_copy)
{
  error_t err;

  if (! protid || (protid->realnode != MACH_PORT_NULL && ! protid->isroot))
    return EPERM;

  if (nports < INIT_PORT_MAX || nints < INIT_INT_MAX)
    return EINVAL;		/*  */

  err = 0;
  ports = servercopy (ports, nports * sizeof (mach_port_t), ports_copy, &err);
  if (err)
    return err;
  ints = servercopy (ints, nints * sizeof (int), ints_copy, &err);
  if (err)
    {
      munmap (ports, nports * sizeof (mach_port_t));
      return err;
    }

  pthread_rwlock_wrlock (&std_lock);

  if (std_ports)
    {
      mach_msg_type_number_t i;
      for (i = 0; i < std_nports; ++i)
	mach_port_deallocate (mach_task_self (), std_ports[i]);
      munmap (std_ports, std_nports * sizeof (mach_port_t));
    }

  std_ports = ports;
  std_nports = nports;

  if (std_ints)
    munmap (std_ints, std_nints * sizeof (int));

  std_ints = ints;
  std_nints = nints;

  pthread_rwlock_unlock (&std_lock);

  return 0;
}


#include "exec_startup_S.h"

/* RPC sent on the bootstrap port.  */

kern_return_t
S_exec_startup_get_info (struct bootinfo *boot,
			 vm_address_t *user_entry,
			 vm_address_t *phdr_data, vm_size_t *phdr_size,
			 vm_address_t *stack_base, vm_size_t *stack_size,
			 int *flags,
			 data_t *argvp, mach_msg_type_number_t *argvlen,
			 data_t *envpp, mach_msg_type_number_t *envplen,
			 mach_port_t **dtable,
			 mach_msg_type_name_t *dtablepoly,
			 mach_msg_type_number_t *dtablesize,
			 mach_port_t **portarray,
			 mach_msg_type_name_t *portpoly,
			 mach_msg_type_number_t *nports,
			 int **intarray, mach_msg_type_number_t *nints)
{
  if (! boot)
    return EOPNOTSUPP;

  /* Pass back all the information we are storing.  */

  *user_entry = boot->user_entry;
  *phdr_data = boot->phdr_addr;
  *phdr_size = boot->phdr_size;
  *stack_base = boot->stack_base;
  *stack_size = boot->stack_size;

  *argvp = boot->argv;
  *argvlen = boot->argvlen;
  boot->argvlen = 0;

  *envpp = boot->envp;
  *envplen = boot->envplen;
  boot->envplen = 0;

  *dtable = boot->dtable;
  *dtablesize = boot->dtablesize;
  *dtablepoly = MACH_MSG_TYPE_MOVE_SEND;
  boot->dtablesize = 0;

  *intarray = boot->intarray;
  *nints = boot->nints;
  boot->nints = 0;

  *portarray = boot->portarray;
  *nports = boot->nports;
  *portpoly = MACH_MSG_TYPE_MOVE_SEND;
  boot->nports = 0;

  *flags = boot->flags;

  return 0;
}
