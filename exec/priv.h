/* GNU Hurd standard exec server, private declarations.
   Copyright (C) 1992,93,94,95,96,99,2000,02, 04 Free Software Foundation, Inc.
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <hurd/trivfs.h>
#include <hurd/ports.h>
#include <hurd/lookup.h>
#include <rwlock.h>

#ifdef BFD
#include <bfd.h>
#endif

#include <elf.h>
#include <link.h>		/* This gives us the ElfW macro.  */
#include <fcntl.h>
#include "exec_S.h"


#ifndef exec_priv_h
#define exec_priv_h

#ifdef BFD
/* A BFD whose architecture and machine type are those of the host system.  */
extern bfd_arch_info_type host_bfd_arch_info;
extern bfd host_bfd;
#endif

/* Information kept around to be given to a new task
   in response to a message on the task's bootstrap port.  */
struct bootinfo
  {
    struct port_info pi;
    vm_address_t stack_base;
    vm_size_t stack_size;
    int flags;
    char *argv, *envp;
    size_t argvlen, envplen, dtablesize, nports, nints;
    mach_port_t *dtable, *portarray;
    int *intarray;
    vm_address_t phdr_addr, user_entry;
    vm_size_t phdr_size;
  };


/* Where to put the service ports. */
struct port_bucket *port_bucket;
struct port_class *execboot_portclass;

extern mach_port_t procserver;	/* Our proc port.  */

#ifdef BFD
#define EXECDATA_STREAM		/* BFD uses stdio to access the executable.  */
#else
typedef void asection;
#endif


/* Data shared between check, check_section,
   load, load_section, and finish.  */
struct execdata
  {
    /* Passed out to caller.  */
    error_t error;

    /* Set by check.  */
    vm_address_t entry;
    file_t file;

#ifndef EXECDATA_STREAM

    /* Note that if `file_data' (below) is set, then these just point
       into that and should not be deallocated (file_data is malloc'd).  */
    char *map_buffer;		/* Our mapping window or read buffer.  */
    size_t map_vsize;		/* Page-aligned size allocated there.  */
    size_t map_fsize;		/* Bytes from there to end of mapped data.  */
    off_t map_filepos;		/* Position `map_buffer' maps to.  */
#define map_buffer(e)	((e)->map_buffer)
#define map_fsize(e)	((e)->map_fsize)
#define map_vsize(e)	((e)->map_vsize)
#define map_filepos(e)	((e)->map_filepos)
#define map_set_fsize(e, fsize) ((e)->map_fsize = (fsize))

#else

#ifdef _STDIO_USES_IOSTREAM
# error implement me for libio!
#else
    FILE stream;
#define map_buffer(e)	((e)->stream.__buffer)
#define map_fsize(e)	((e)->stream.__get_limit - (e)->stream.__buffer)
#define map_vsize(e)	((e)->stream.__bufsize)
#define map_filepos(e)	((e)->stream.__offset)
#define map_set_fsize(e, fsize)	\
  ((e)->stream.__get_limit = (e)->stream.__buffer + (fsize))
#endif

#endif

#ifdef BFD
    bfd *bfd;
#endif

    union			/* Interpreter section giving name of file.  */
      {
	asection *section;
	const ElfW(Phdr) *phdr;
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
	    ElfW(Phdr) *phdr;
	    ElfW(Addr) phdr_addr;
	    ElfW(Word) phnum;	/* Number of program header table elements.  */
	    int anywhere;	/* Nonzero if image can go anywhere.  */
	    vm_address_t loadbase; /* Actual mapping location.  */
	    int execstack;	/* Zero if stack can be nonexecutable.  */
	  } elf;
      } info;
  };

error_t elf_machine_matches_host (ElfW(Half) e_machine);

void finish (struct execdata *, int dealloc_file_port);

/* Make sure our mapping window (or read buffer) covers
   LEN bytes of the file starting at POSN, and return
   a pointer into the window corresponding to POSN.  */
void *map (struct execdata *e, off_t posn, size_t len);


void check_hashbang (struct execdata *e,
		     file_t file,
		     task_t oldtask,
		     int flags,
		     char *argv, u_int argvlen, boolean_t argv_copy,
		     char *envp, u_int envplen, boolean_t envp_copy,
		     mach_port_t *dtable, u_int dtablesize,
		     boolean_t dtable_copy,
		     mach_port_t *portarray, u_int nports,
		     boolean_t portarray_copy,
		     int *intarray, u_int nints, boolean_t intarray_copy,
		     mach_port_t *deallocnames, u_int ndeallocnames,
		     mach_port_t *destroynames, u_int ndestroynames);


/* Standard exec data for secure execs.  */
extern mach_port_t *std_ports;
extern int *std_ints;
extern size_t std_nports, std_nints;
extern struct rwlock std_lock;


#endif /* exec_priv_h */
