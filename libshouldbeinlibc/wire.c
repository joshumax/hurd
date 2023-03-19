/* Function to wire down text and data (including from shared libraries)
   Copyright (C) 1996,99,2000,01,02 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */


#include <link.h>
#include <dlfcn.h>
#include <hurd.h>
#include <error.h>
#include <elf.h>
#include <mach/gnumach.h>
#include <mach/vm_param.h>

/* Wire down all memory currently allocated at START for LEN bytes;
   host_priv is the privileged host port. */
static error_t
wire_segment_internal (vm_address_t start,
		       vm_size_t len,
		       host_priv_t host_priv)
{
  vm_address_t addr;
  vm_size_t size;
  vm_prot_t protection;
  vm_prot_t max_protection;
  vm_inherit_t inheritance;
  boolean_t shared;
  mach_port_t object_name;
  vm_offset_t offset;
  error_t err;
  volatile char *poke;

  do
    {
      addr = start;
      err = vm_region (mach_task_self (), &addr, &size, &protection,
		       &max_protection, &inheritance, &shared, &object_name,
		       &offset);
      if (err == KERN_NO_SPACE)
        return 0;	/* We're done.  */
      if (err)
	return err;
      mach_port_deallocate (mach_task_self (), object_name);

      if (protection != VM_PROT_NONE)
        {
          /* The VM system cannot cope with a COW fault on another
             unrelated virtual copy happening later when we have
             wired down the original page.  So we must touch all our
             pages before wiring to make sure that only we will ever
             use them.  */

          /* The current region begins at ADDR and is SIZE long.  If it
             extends beyond the LEN, prune it. */
          if (addr + size > start + len)
            size = len - (addr - start);

          /* Set protection to allow all access possible */
          if (!(protection & VM_PROT_WRITE))
            {
              err = vm_protect (mach_task_self (), addr, size, 0, max_protection);
              if (err)
                return err;
            }

          /* Generate write faults */
          for (poke = (char *) addr;
               (vm_address_t) poke < addr + size;
               poke += vm_page_size)
            *poke = *poke;

          /* Wire pages */
          err = vm_wire (host_priv, mach_task_self (), addr, size, protection);
          if (err)
            return err;

          /* Set protection back to what it was */
          if (!(protection & VM_PROT_WRITE))
            {
              err = vm_protect (mach_task_self (), addr, size, 0, protection);
              if (err)
                return err;
            }
        }

      len -= (addr - start) + size;
      start = addr + size;
    }
  while (len);

  return err;
}

/* Wire down all memory currently allocated at START for LEN bytes. */
error_t
wire_segment (vm_address_t start,
	      vm_size_t len)
{
  mach_port_t host, device;
  error_t err;

  err = get_privileged_ports (&host, &device);
  if (err)
    return err;

  err = wire_segment_internal (start, len, host);
  mach_port_deallocate (mach_task_self (), host);
  mach_port_deallocate (mach_task_self (), device);
  return err;
}

/* Wire down all the text and data (including from shared libraries)
   for the current program. */
error_t
wire_task_self (void)
{
  mach_port_t host, device;
  error_t err;

  err = get_privileged_ports (&host, &device);
  if (err)
    return err;

  err = wire_segment_internal (VM_MIN_ADDRESS, VM_MAX_ADDRESS, host);
  if (err)
    goto out;

  /* Automatically wire down future mappings, including those that are
     currently PROT_NONE but become accessible.  */
  err = vm_wire_all (host, mach_task_self (), VM_WIRE_ALL);

 out:
  mach_port_deallocate (mach_task_self (), host);
  mach_port_deallocate (mach_task_self (), device);
  return err;
}
