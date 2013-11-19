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

#pragma weak _DYNAMIC
#pragma weak dlopen
#pragma weak dlclose
#pragma weak dlerror
#pragma weak dlsym
#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0
#endif

/* Find the list of shared objects */
static struct link_map *
loaded (void)
{
  ElfW(Dyn) *d;

  if (&_DYNAMIC == 0)		/* statically linked */
    return 0;

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
static void
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
      if (err)
	return;

      /* The current region begins at ADDR and is SIZE long.  If it
      	 extends beyond the LEN, prune it. */
      if (addr + size > start + len)
	size = len - (addr - start);

      /* Set protection to allow all access possible */
      vm_protect (mach_task_self (), addr, size, 0, max_protection);

      /* Generate write faults */
      for (poke = (char *) addr;
	   (vm_address_t) poke < addr + size;
	   poke += vm_page_size)
	*poke = *poke;

      /* Wire pages */
      vm_wire (host_priv, mach_task_self (), addr, size, max_protection);

      /* Set protection back to what it was */
      vm_protect (mach_task_self (), addr, size, 0, protection);


      mach_port_deallocate (mach_task_self (), object_name);

      len -= (addr - start) + size;
      start = addr + size;
    }
  while (len);
}

/* Wire down all memory currently allocated at START for LEN bytes. */
void
wire_segment (vm_address_t start,
	      vm_size_t len)
{
  mach_port_t host, device;
  error_t error;

  error = get_privileged_ports (&host, &device);
  if (!error)
    {
      wire_segment_internal (start, len, host);
      mach_port_deallocate (mach_task_self (), host);
      mach_port_deallocate (mach_task_self (), device);
    }
}


/* Wire down all the text and data (including from shared libraries)
   for the current program. */
void
wire_task_self ()
{
  struct link_map *map;
  mach_port_t host, device;
  error_t error;
  extern char _edata, _etext, __data_start;

  error = get_privileged_ports (&host, &device);
  if (error)
    return;

  map = loaded ();
  if (!map)
    {
      extern void _start ();
      vm_address_t text_start = (vm_address_t) &_start;
      wire_segment_internal (text_start,
			     (vm_size_t) (&_etext - text_start),
			     host);
      wire_segment_internal ((vm_address_t) &__data_start,
			     (vm_size_t) (&_edata - &__data_start),
			     host);
    }
  else
    while (map)
      wire_segment ((vm_address_t) map->l_addr, map_extent (map));

  mach_port_deallocate (mach_task_self (), host);
  mach_port_deallocate (mach_task_self (), device);
}
