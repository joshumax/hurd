/* Pagemap manipulation for pager library
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

  
/* Grow the pagemap as necessary to deal with address OFF */
void
pagemap_resize (struct pager *p,
		int off)
{
  void *newaddr;
  int newsize;
  
  if (p->pagemapsize && !p->pagemap)
    panic ("pagemap failure");

  off /= __vm_page_size;
  if (p->pagemapsize >= off)
    return;
  
  newsize = round_page (off);
  vm_allocate (mach_task_self (), (u_int *)&newaddr, newsize, 1);
  bcopy (p->pagemap, newaddr, p->pagemapsize);
  vm_deallocate (mach_task_self (), (u_int)p->pagemap, p->pagemapsize);
  p->pagemap = newaddr;
  p->pagemapsize = newsize;
}
