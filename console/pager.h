/* pager.h - Interface to the pager for display component of a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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

#ifndef PAGER_H
#define PAGER_H

struct user_pager
{
  struct pager *pager;  
  memory_object_t memobj;
};

/* Initialize the pager for the display component.  */
void user_pager_init (void);

/* Create a new pager in USER_PAGER with NPAGES pages, and return a
   mapping to the memory in *USER.  */
error_t user_pager_create (struct user_pager *user_pager, unsigned int npages,
			   struct cons_display **user);

/* Destroy the pager USER_PAGER and the mapping at USER.  */
void user_pager_destroy (struct user_pager *user_pager,
			 struct cons_display *user);

/* Allocate a reference for the memory object backing the pager
   USER_PAGER with protection PROT and return it.  */
mach_port_t user_pager_get_filemap (struct user_pager *user_pager,
				    vm_prot_t prot);

#endif	/* PAGER_H_ */
