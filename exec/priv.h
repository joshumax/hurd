/* GNU Hurd standard exec server, private declarations.
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation, Inc.
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
#include <hurd/trivfs.h>
#include <hurd/ports.h>
#include <bfd.h>
#include <elf.h>
#include <fcntl.h>
#include "exec_S.h"


#ifndef exec_priv_h
#define exec_priv_h

/* A BFD whose architecture and machine type are those of the host system.  */
extern bfd_arch_info_type host_bfd_arch_info;
extern bfd host_bfd;
extern Elf32_Half elf_machine;	/* ELF e_machine for the host.  */


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
    vm_address_t phdr_addr, phdr_size, user_entry;
  };


/* Where to put the service ports. */
struct port_bucket *port_bucket;
struct port_class *execboot_portclass;


typedef struct trivfs_protid *trivfs_protid_t; /* For MiG.  */

extern mach_port_t procserver;	/* Our proc port.  */

#endif /* exec_priv_h */
