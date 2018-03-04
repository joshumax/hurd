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

#pragma weak _DYNAMIC
#pragma weak dlopen
#pragma weak dlclose
#pragma weak dlerror
#pragma weak dlsym
#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0
#endif

static int
statically_linked (void)
{
  return &_DYNAMIC == 0;	/* statically linked */
}

/* Find the list of shared objects */
static struct link_map *
loaded (void)
{
  ElfW(Dyn) *d;

  for (d = _DYNAMIC; d->d_tag != DT_NULL; ++d)
    if (d->d_tag == DT_DEBUG)
      {
	struct r_debug *r = (void *) d->d_un.d_ptr;
	return r->r_map;
      }

  return 0;			/* ld broken */
}

/* Compute the extent of a particular shared object. */
static ElfW(Addr)
map_extent (struct link_map *map)
{
  /* In fact, LIB == MAP, but doing it this way makes it entirely kosher.  */
  void *lib = dlopen (map->l_name, RTLD_NOLOAD);
  if (lib == 0)
    {
      error (2, 0, "cannot dlopen %s: %s", map->l_name, dlerror ());
      /* NOTREACHED */
      return 0;
    }
  else
    {
      /* Find the _end symbol's runtime address and subtract the load base.  */
      void *end = dlsym (lib, "_end");
      if (end == 0)
	error (2, 0, "cannot wire library %s with no _end symbol: %s",
	       map->l_name, dlerror ());
      dlclose (lib);
      return (ElfW(Addr)) end - map->l_addr;
    }
}

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
wire_task_self ()
{
  mach_port_t host, device;
  error_t err;

  err = get_privileged_ports (&host, &device);
  if (err)
    return err;

  if (statically_linked ())
    {
      extern void _start ();
      extern char _edata, _etext, __data_start;
      vm_address_t text_start = (vm_address_t) &_start;
      err = wire_segment_internal (text_start,
                                   (vm_size_t) (&_etext - text_start),
                                   host);
      if (err)
        goto out;

      err = wire_segment_internal ((vm_address_t) &__data_start,
                                   (vm_size_t) (&_edata - &__data_start),
                                   host);
    }
  else
    {
      struct link_map *map;

      map = loaded ();
      if (map)
        for (err = 0; ! err && map; map = map->l_next)
          err = wire_segment_internal ((vm_address_t) map->l_addr,
                                       map_extent (map), host);
      else
        err = wire_segment_internal (VM_MIN_ADDRESS, VM_MAX_ADDRESS, host);
    }

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
