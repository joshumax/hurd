/* Window management routines for buffered I/O using VM.

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <hurd.h>

#include "window.h"
#include "mem.h"

/* ---------------------------------------------------------------- */

/* Create a VM window onto the memory object MEMOBJ, and return it in WIN.
   MIN_SIZE and MAX_SIZE are the minimum and maximum sizes that the window
   will shrink/grow to (a value of 0 will use some default).  */
error_t
window_create(mach_port_t memobj, vm_offset_t max_pos,
	      vm_size_t min_size, vm_size_t max_size, int read_only,
	      struct window **win)
{
  *win = malloc(sizeof(struct window));
  if (*win == NULL)
    return ENOMEM;

  if (min_size < max_size)
    min_size = max_size;

  (*win)->pos = 0;
  (*win)->max_pos = max_pos;
  (*win)->size = 0;
  (*win)->memobj = memobj;
  (*win)->min_size = (min_size < vm_page_size ? vm_page_size : min_size);
  (*win)->max_size = (max_size < vm_page_size ? vm_page_size : max_size);
  (*win)->read_only = read_only;

  return 0;
}

/* Free WIN and any resources it holds.  */
void
window_free(struct window *win)
{
  if (win->size > 0)
    vm_deallocate(mach_task_self(), win->buffer, win->size);
  mach_port_destroy(mach_task_self(), win->memobj);
  free(win);
}

/* ---------------------------------------------------------------- */

/* Makes sure that WIN's memory window contains at least positions POS
   through POS + LEN on the device WIN's mapping.  If an error occurs in the
   process, the error code is returned (and WIN may not map the desired
   locations), otherwise 0.  WIN is assumed to already be locked when this is
   called.  */
static error_t
position(struct window *win, vm_offset_t pos, vm_size_t len)
{
  vm_offset_t end = pos + len;
  vm_offset_t win_beg = win->pos;
  vm_offset_t win_end = win_beg + win->size;

  if (pos >= win_beg && end <= win_end)
    /* The request is totally satisfied by our current position.  */
    return 0;
  else
#if 0				/* XXXXXXX */
  if (end < win_beg || pos >= win_end)
    /* The desired locations are entirely outside our current window, so just
       trash it, and map a new buffer anywhere.  */
#endif
    {
      int prot = VM_PROT_READ | (win->read_only ? 0 : VM_PROT_WRITE);

      if (win->size > 0)
	vm_deallocate(mach_task_self(), win->buffer, win->size);

      win->pos = trunc_page(pos);
      win->size = round_page(len + (pos - win->pos));
      win->buffer = 0;

      if (win->size < win->min_size)
	win->size = win->min_size;

      if (win->pos + win->size > win->max_pos)
	win->size = win->max_pos - win->pos;

      return
	vm_map(mach_task_self(), &win->buffer, win->size, 0, 1,
	       win->memobj, win->pos, 0, prot, prot, VM_INHERIT_NONE);
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Write up to BUF_LEN bytes from BUF to the device that WIN is a window on,
   at offset *OFFS, using memory-mapped buffered I/O.  If successful, 0 is
   returned, otherwise an error code is returned.  *OFFS is incremented by
   the amount sucessfully written.  */
error_t
window_write(struct window *win,
	     vm_address_t buf, vm_size_t buf_len, vm_size_t *amount,
	     vm_offset_t *offs)
{
  error_t err;

  mutex_lock(&win->lock);

  err = position(win, *offs, buf_len);
  if (!err)
    {
      bcopy((char *)buf,
	    (char *)win->buffer + (*offs - win->pos),
	    buf_len);
      *amount = buf_len;
      *offs += buf_len;
    }

  mutex_unlock(&win->lock);

  return err;
}

/* Read up to AMOUNT bytes from the device that WIN is a window on, at offset
   *OFFS, into BUF and BUF_LEN (using the standard mach out-array
   conventions), using memory-mapped buffered I/O.  If successful, 0 is
   returned, otherwise an error code is returned.  *OFFS is incremented by
   the amount sucessfully written.  */
error_t
window_read(struct window *win,
	    vm_address_t *buf, vm_size_t *buf_len,
	    vm_size_t amount, vm_offset_t *offs)
{
  error_t err;

  mutex_lock(&win->lock);

  err = position(win, *offs, amount);
  if (!err)
    {
      err = allocate(buf, buf_len, amount);
      if (!err)
	{
	  bcopy((char *)win->buffer + (*offs - win->pos),
		(char *)*buf,
		amount);
	  *offs += amount;
	}
    }

  mutex_unlock(&win->lock);

  return err;
}
