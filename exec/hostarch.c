/* Determine the BFD and ELF architecture and machine flavor
   from a Mach host port.  Used by the exec and core servers.
   Copyright (C) 1992, 1993, 1995, 1996 Free Software Foundation, Inc.
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


#include <mach.h>
#include <hurd/hurd_types.h>
#include <errno.h>
#include <elf.h>

error_t
mach_host_elf_machine (host_t host,
		       Elf32_Half *e_machine)
{
  error_t err;
  struct host_basic_info hostinfo;
  mach_msg_type_number_t hostinfocnt = HOST_BASIC_INFO_COUNT;

  err = host_info (host, HOST_BASIC_INFO, 
		   (natural_t *) &hostinfo, &hostinfocnt);
  if (err)
    return err;

  switch (hostinfo.cpu_type)
    {
    default:
      *e_machine = EM_NONE;
      break;

    case CPU_TYPE_MC68020:
    case CPU_TYPE_MC68030:
    case CPU_TYPE_MC68040:
      *e_machine = EM_68K;
      break;

    case CPU_TYPE_I860:
      *e_machine = EM_860;
      break;

    case CPU_TYPE_MIPS:
      *e_machine = EM_MIPS;
      break;

    case CPU_TYPE_MC88000:
      *e_machine = EM_88K;
      break;

    case CPU_TYPE_SPARC:
      *e_machine = EM_SPARC;
      break;

    case CPU_TYPE_I386:
      *e_machine = EM_386;
      break;
    }

  return 0;
}

#ifdef BFD
#include <bfd.h>

error_t
bfd_mach_host_arch_mach (host_t host,
			 enum bfd_architecture *arch,
			 long int *machine)
{
  error_t err;
  struct host_basic_info hostinfo;
  mach_msg_type_number_t hostinfocnt = HOST_BASIC_INFO_COUNT;

  err = host_info (host, HOST_BASIC_INFO, (natural_t *) &hostinfo, &hostinfocnt);
  if (err)
    return err;

  *machine = hostinfo.cpu_subtype;
  *e_machine = EM_NONE;
  switch (hostinfo.cpu_type)
    {
    case CPU_TYPE_MC68020:
      *arch = bfd_arch_m68k;
      *machine = 68020;
      *e_machine = EM_68K;
      break;
    case CPU_TYPE_MC68030:
      *arch = bfd_arch_m68k;
      *machine = 68030;
      *e_machine = EM_68K;
      break;
    case CPU_TYPE_MC68040:
      *arch = bfd_arch_m68k;
      *machine = 68040;
      *e_machine = EM_68K;
      break;

    case CPU_TYPE_NS32032:
      *arch = bfd_arch_ns32k;
      *machine = 32032;
      break;
    case CPU_TYPE_NS32332:
      *arch = bfd_arch_ns32k;
      *machine = 32332;
      break;
    case CPU_TYPE_NS32532:
      *arch = bfd_arch_ns32k;
      *machine = 32532;
      break;

    case CPU_TYPE_ROMP:
      *arch = bfd_arch_romp;
      break;

    case CPU_TYPE_I860:
      *arch = bfd_arch_i860;
      *e_machine = EM_860;
      break;

    case CPU_TYPE_MIPS:
      *arch = bfd_arch_mips;
      *e_machine = EM_MIPS;
      break;

    case CPU_TYPE_VAX:
      *arch = bfd_arch_vax;
      break;

    case CPU_TYPE_MC88000:
      *arch = bfd_arch_m88k;
      *e_machine = EM_88K;
      break;

    case CPU_TYPE_SPARC:
      *arch = bfd_arch_sparc;
      *e_machine = EM_SPARC;
      break;

    case CPU_TYPE_I386:
      *arch = bfd_arch_i386;
      *e_machine = EM_386;
      break;

#ifdef CPU_TYPE_ALPHA
    case CPU_TYPE_ALPHA:
      *arch = bfd_arch_alpha;
      break;
#endif

    default:
      return ENOEXEC;
    }

  return 0;
}

#endif /* BFD */
