/* Determine the BFD (or a.out) architecture and machine flavor
   from a Mach host port.  Used by the exec and core servers.
   Copyright (C) 1992, 1993, 1995 Free Software Foundation, Inc.
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

#ifdef	BFD
#include <bfd.h>
#else
#include A_OUT_H
#endif

error_t
#ifdef	BFD
bfd_mach_host_arch_mach (host_t host,
			 enum bfd_architecture *arch,
			 long int *machine)
#else
aout_mach_host_machine (host_t host, int *host_machine)
#endif
{
  error_t err;
  struct host_basic_info hostinfo;
  mach_msg_type_number_t hostinfocnt = HOST_BASIC_INFO_COUNT;

  if (err = host_info (host, HOST_BASIC_INFO,
		       (natural_t *) &hostinfo, &hostinfocnt))
    return err;

#ifdef	BFD
  *machine = hostinfo.cpu_subtype;
#endif
  switch (hostinfo.cpu_type)
    {
    case CPU_TYPE_MC68020:
#ifdef	BFD
      *arch = bfd_arch_m68k;
      *machine = 68020;
#else
    case CPU_TYPE_MC68030:
    case CPU_TYPE_MC68040:
      *host_machine = M_68020;
#endif
      break;
#ifdef	BFD
    case CPU_TYPE_MC68030:
      *arch = bfd_arch_m68k;
      *machine = 68030;
      break;
    case CPU_TYPE_MC68040:
      *arch = bfd_arch_m68k;
      *machine = 68040;
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
      break;

    case CPU_TYPE_MIPS:
      *arch = bfd_arch_mips;
      break;

    case CPU_TYPE_VAX:
      *arch = bfd_arch_vax;
      break;

    case CPU_TYPE_MC88000:
      *arch = bfd_arch_m88k;
      break;
#endif

    case CPU_TYPE_SPARC:
#ifdef	BFD
      *arch = bfd_arch_sparc;
#else
      *host_machine = M_SPARC;
#endif
      break;

    case CPU_TYPE_I386:
#ifdef	BFD
      *arch = bfd_arch_i386;
#else
      *host_machine = M_386;
#endif
      break;

#ifdef CPU_TYPE_ALPHA
    case CPU_TYPE_ALPHA:
#ifdef	 BFD
      *arch = bfd_arch_alpha;
#else
#ifndef M_ALPHA
#define M_ALPHA 999		/* XXX */
#endif
      *host_machine = M_ALPHA;
#endif
      break;
#endif

    default:
#ifdef	BFD
      return ENOEXEC;
#else
      *host_machine = 0;
#endif
    }

  return 0;
}
