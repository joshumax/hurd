/* Determine the ELF architecture and machine flavor
   from a Mach host port.  Used by the exec and core servers.
   Copyright (C) 1992,93,95,96,99,2000,02 Free Software Foundation, Inc.
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

#include "priv.h"
#include <mach.h>
#include <hurd/hurd_types.h>
#include <errno.h>
#include <elf.h>

error_t
elf_machine_matches_host (ElfW(Half) e_machine)
{
  static void *host_type;	/* Cached entry into the switch below.  */
  struct host_basic_info hostinfo;

  if (host_type)
    goto *host_type;
  else
    {
      error_t err;
      mach_msg_type_number_t hostinfocnt = HOST_BASIC_INFO_COUNT;

      err = host_info (mach_host_self (), HOST_BASIC_INFO,
		       (host_info_t) &hostinfo, &hostinfocnt);
      if (err)
	return err;
      assert_backtrace (hostinfocnt == HOST_BASIC_INFO_COUNT);
    }

#define CACHE(test) ({ __label__ here; host_type = &&here; \
		      here: return (test) ? 0 : ENOEXEC; })
  switch (hostinfo.cpu_type)
    {
    case CPU_TYPE_MC68020:
    case CPU_TYPE_MC68030:
    case CPU_TYPE_MC68040:
      CACHE (e_machine == EM_68K);

    case CPU_TYPE_I860:
      CACHE (e_machine == EM_860);

    case CPU_TYPE_MIPS:
      CACHE (e_machine == EM_MIPS);

    case CPU_TYPE_MC88000:
      CACHE (e_machine == EM_88K);

    case CPU_TYPE_SPARC:
      CACHE (e_machine == EM_SPARC);

    case CPU_TYPE_I386:
    case CPU_TYPE_I486:
    case CPU_TYPE_PENTIUM:
    case CPU_TYPE_PENTIUMPRO:
      CACHE (e_machine == EM_386);

    case CPU_TYPE_POWERPC:
      CACHE (e_machine == EM_PPC);

    case CPU_TYPE_ALPHA:
      CACHE (e_machine == EM_ALPHA);

    case CPU_TYPE_HPPA:
      CACHE (e_machine == EM_PARISC);

    default:
      return EGRATUITOUS;	/* XXX */
    }

  return 0;
}
