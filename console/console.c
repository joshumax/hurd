/* console.c - The device independant part of a console.
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

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <cthreads.h>

#include "console.h"
#include "display.h"
#include "input.h"


struct vcons
{
  /* Protected by cons_list_lock.  */
  vcons_t next;
  vcons_t prev;
  int refcnt;

  /* The following members remain constant over the lifetime of the
     object and don't need to be locked.  */
  cons_t cons;
  int id;
  void *display_console;

  struct mutex lock;
  /* Indicates if OWNER_ID is initialized.  */
  int has_owner;
  /* Specifies the ID of the process that should receive the WINCH
     signal for this virtual console.  */
  int owner_id;

  /* The output queue holds the characters that are to be outputted.
     The display driver might refuse to handle some incomplete
     multi-byte or composed character at the end of the buffer, so we
     have to keep them around.  */
  struct mutex output_lock;
  int output_stopped;
  struct condition output_resumed;
  char *output;
  size_t output_allocated;
  size_t output_size;

  struct mutex input_lock;
  /* XXX input queue.  */
  char *input;
  size_t input_allocated;
  size_t input_size;
};


struct cons
{
  /* Protected by cons_list_lock.  */
  cons_t prev;
  cons_t next;
  int refcnt;
  vcons_t vcons_list;
  size_t vcons_length;
  vcons_t vcons_active;

  /* The following members are static and don't need to be locked.  */
  char *name;
  display_ops_t display_ops;
};


/* The lock protects the console list, all virtual console lists of
   all consoles, and their reference counters.  */
struct mutex cons_list_lock;
cons_t cons_list;
size_t cons_length;


struct mutex config_lock;
/* The default encoding.  */
const char *config_encoding;
display_ops_t config_display;


/* Lookup the console with name NAME, acquire a reference for it, and
   return it in R_CONS.  If NAME doesn't exist, return ESRCH.  */
error_t
cons_lookup (const char *name, cons_t *r_cons)
{
  cons_t cons;

  mutex_lock (&cons_list_lock);
  cons = cons_list;
  while (cons)
    {
      if (!strcmp (name, cons->name))
	{
	  cons->refcnt++;
	  mutex_unlock (&cons_list_lock);
	  *r_cons = cons;
	  return 0;
	}
      cons = cons->next;
    }
  mutex_unlock (&cons_list_lock);
  return ESRCH;
}


/* Release a reference to CONS.  */
void
cons_release (cons_t cons)
{
  mutex_lock (&cons_list_lock);
  cons->refcnt--;
  mutex_unlock (&cons_list_lock);
}


/* Lookup the virtual console with number ID in the console CONS,
   acquire a reference for it, and return it in R_VCONS.  If CREATE is
   true, the virtual console will be created if it doesn't exist yet.
   If CREATE is true, and ID 0, the first free virtual console id is
   used.  */
error_t
vcons_lookup (cons_t cons, int id, int create, vcons_t *r_vcons)
{
  error_t err;
  vcons_t previous_cons = 0;
  vcons_t vcons;

  if (!id && !create)
    return EINVAL;

  mutex_lock (&cons_list_lock);
  if (id)
    {
      if (cons->vcons_list && cons->vcons_list->id <= id)
	{
	  previous_cons = cons->vcons_list;
	  while (previous_cons->next && previous_cons->next->id <= id)
	    previous_cons = previous_cons->next;
	  if (previous_cons->id == id)
	    {
	      previous_cons->refcnt++;
	      mutex_unlock (&cons_list_lock);
	      *r_vcons = previous_cons;
	      return 0;
	    }
	}
      else if (!create)
	{
	  mutex_unlock (&cons_list_lock);
	  return ESRCH;
	}
    }
  else
    {
      id = 1;
      if (cons->vcons_list && cons->vcons_list->id == 1)
	{
	  previous_cons = cons->vcons_list;
	  while (previous_cons && previous_cons->id == id)
	    {
	      id++;
	      previous_cons = previous_cons->next;
	    }
	}
    }

  vcons = calloc (1, sizeof (struct vcons));
  if (!vcons)
    {
      mutex_unlock (&cons_list_lock);
      return ENOMEM;
    }
  mutex_init (&vcons->lock);
  mutex_init (&vcons->output_lock);
  condition_init (&vcons->output_resumed);
  vcons->refcnt = 1;
  err = (*(cons->display_ops->create)) (&vcons->display_console,
					config_encoding);
  if (err)
    {
      mutex_unlock (&cons_list_lock);
      free (vcons);
      return err;
    }
  /* XXX Set up keyboard input etc.  */
  vcons->cons = cons;
  cons->refcnt++;
  cons->vcons_length++;
  /* Insert the virtual console into the doubly linked list.  */
  if (previous_cons)
    {
      vcons->prev = previous_cons;
      if (previous_cons->next)
	{
	  previous_cons->next->prev = vcons;
	  vcons->next = previous_cons->next;
	}
      previous_cons->next = vcons;
    }
  else
    {
      if (cons->vcons_list)
	{
	  cons->vcons_list->prev = vcons;
	  vcons->next = cons->vcons_list;
	}
      cons->vcons_list = vcons;
    }
  mutex_unlock (&cons_list_lock);
  *r_vcons = vcons;
  return 0;
}


/* Release a reference to the virtual console VCONS.  If this was the
   last reference the virtual console is destroyed.  */
void
vcons_release (vcons_t vcons)
{
  mutex_lock (&cons_list_lock);
  if (!--vcons->refcnt)
    {
      /* As we keep a reference for all input focus groups pointing to
	 the virtual console, and a reference for the active console,
	 we know that without references, this virtual console is
	 neither active nor used by any input group.  */

      (*vcons->cons->display_ops->destroy) (vcons->display_console);
      /* XXX Destroy the rest of the state.  */

      if (vcons->prev)
	vcons->prev->next = vcons->next;
      if (vcons->next)
	vcons->next->prev = vcons->prev;
      if (!vcons->prev && !vcons->next)
	vcons->cons->vcons_list = NULL;
      vcons->cons->vcons_length--;
      vcons->cons->refcnt--;
      free (vcons);
    }      
  mutex_unlock (&cons_list_lock);
}


/* Activate virtual console VCONS for WHO.  WHO is a unique identifier
   for the entity requesting the activation (which can be used by the
   display driver to group several activation requests together).  */
void
vcons_activate (vcons_t vcons, int who)
{
  mutex_lock (&cons_list_lock);
  if (vcons->cons->vcons_active != vcons)
    {
      (*vcons->cons->display_ops->activate) (vcons->display_console, who);
      vcons->cons->vcons_active->refcnt--;
      vcons->refcnt++;
      vcons->cons->vcons_active = vcons;
    }
  mutex_unlock (&cons_list_lock);
}


/* Resume the output on the virtual console VCONS.  */
void
vcons_start_output (vcons_t vcons)
{
  mutex_lock (&vcons->output_lock);
  if (vcons->output_stopped)
    {
      vcons->output_stopped = 0;
      condition_broadcast (&vcons->output_resumed);
    }
  mutex_unlock (&vcons->output_lock);
}


/* Stop all output on the virtual console VCONS.  */
void
vcons_stop_output (vcons_t vcons)
{
  mutex_lock (&vcons->output_lock);
  vcons->output_stopped = 1;
  mutex_unlock (&vcons->output_lock);
}


/* Return the number of pending output bytes for VCONS.  */
size_t
vcons_pending_output (vcons_t vcons)
{
  int output_size;
  mutex_lock (&vcons->output_lock);
  output_size = vcons->output_size;
  mutex_unlock (&vcons->output_lock);
  return output_size;
}


/* Fush the input buffer, discarding all pending data.  */
void
vcons_flush_input (vcons_t vcons)
{
  mutex_lock (&vcons->input_lock);
  vcons->input_size = 0;
  mutex_unlock (&vcons->input_lock);
}


/* Flush the output buffer, discarding all pending data.  */
void
vcons_discard_output (vcons_t vcons)
{
  mutex_lock (&vcons->output_lock);
  vcons->output_size = 0;
  mutex_unlock (&vcons->output_lock);
}


/* Output DATALEN characters from the buffer DATA on the virtual
   console VCONS.  The DATA must be supplied in the system encoding
   configured for VCONS.  The function returns the amount of bytes
   written (might be smaller than DATALEN) or -1 and the error number
   in errno.  If NONBLOCK is not zero, return with -1 and set errno
   to EWOULDBLOCK if operation would block for a long time.  */
ssize_t
vcons_output (vcons_t vcons, int nonblock, char *data, size_t datalen)
{
  error_t err;
  char *output;
  size_t output_size;
  ssize_t amount;

  error_t ensure_output_buffer_size (size_t new_size)
  {
    /* Must be a power of two.  */
#define OUTPUT_ALLOCSIZE 32

    if (vcons->output_allocated < new_size)
      {
	char *new_output;
	new_size = (new_size + OUTPUT_ALLOCSIZE - 1) & ~(OUTPUT_ALLOCSIZE - 1);
	new_output = realloc (vcons->output, new_size);
	if (!new_output)
	  return ENOMEM;
	vcons->output = new_output;
	vcons->output_allocated = new_size;
      }
    return 0;
  }

  mutex_lock (&vcons->output_lock);
  while (vcons->output_stopped)
    {
      if (nonblock)
	{
	  mutex_unlock (&vcons->output_lock);
	  errno = EWOULDBLOCK;
	  return -1;
	}
      if (hurd_condition_wait (&vcons->output_resumed, &vcons->output_lock))
	{
	  mutex_unlock (&vcons->output_lock);
	  errno = EINTR;
	  return -1;
	}
    }

  if (vcons->output_size)
    {
      err = ensure_output_buffer_size (vcons->output_size + datalen);
      if (err)
	{
	  mutex_unlock (&vcons->output_lock);
	  errno = ENOMEM;
	  return -1;
	}
      output = vcons->output;
      output_size = vcons->output_size;
      memcpy (output + output_size, data, datalen);
      output_size += datalen;
    }
  else
    {
      output = data;
      output_size = datalen;
    }
  amount = output_size;
  err = (*vcons->cons->display_ops->output) (vcons->display_console,
					     &output, &output_size);
  amount -= output_size;

  if (err && !amount)
    {
      mutex_unlock (&vcons->output_lock);
      errno = err;
      return err;
    }

  /* XXX What should be done with invalid characters etc?  */
  if (output_size)
    {
      /* If we used the caller's buffer DATA, the remaining bytes
	 might not fit in our internal output buffer.  In this case we
	 can reallocate the buffer in VCONS without needing to update
	 OUTPUT (as it points into DATA). */
      err = ensure_output_buffer_size (output_size);
      if (err)
	{
	  mutex_unlock (&vcons->output_lock);
	  return err;
	}
      memmove (vcons->output, output, output_size);
    }
  vcons->output_size = output_size;
  amount += output_size;

  mutex_unlock (&vcons->output_lock);
  return amount;
}


/* Add DATALEN bytes starting from DATA to the input queue in
   VCONS.  */
error_t
vcons_input (vcons_t vcons, char *data, size_t datalen)
{
  error_t err = 0;

  error_t ensure_input_buffer_size (size_t new_size)
  {
    /* Must be a power of two.  */
#define INPUT_ALLOCSIZE 32

    if (vcons->input_allocated < new_size)
      {
	char *new_input;
	new_size = (new_size + INPUT_ALLOCSIZE - 1) & ~(INPUT_ALLOCSIZE - 1);
	new_input = realloc (vcons->input, new_size);
	if (!new_input)
	  return ENOMEM;
	vcons->input = new_input;
	vcons->input_allocated = new_size;
      }
    return 0;
  }

  mutex_lock (&vcons->input_lock);
  err = ensure_input_buffer_size (vcons->input_size + datalen);
  if (err)
    {
      mutex_unlock (&vcons->input_lock);
      return err;
    }
  memcpy (vcons->input + vcons->input_size, data, datalen);
  vcons->input_size += datalen;
  mutex_unlock (&vcons->input_lock);
  return 0;
}


/* Return the dimension of the virtual console VCONS in WINSIZE.  */
void
vcons_getsize (vcons_t vcons, struct winsize *size)
{
  (*vcons->cons->display_ops->getsize) (vcons->display_console, size);
}


/* Set the owner of the virtual console VCONS to PID.  The owner
   receives the SIGWINCH signal when the terminal size changes.  */
error_t
vcons_set_owner (vcons_t vcons, pid_t pid)
{
  mutex_lock (&vcons->lock);
  vcons->has_owner = 1;
  vcons->owner_id = pid;
  mutex_unlock (&vcons->lock);
  return 0;
}


/* Return the owner of the virtual console VCONS in PID.  If there is
   no owner, return ENOTTY.  */
error_t
vcons_get_owner (vcons_t vcons, pid_t *pid)
{
  error_t err = 0;
  mutex_lock (&vcons->lock);
  if (!vcons->has_owner)
    err = ENOTTY;
  else
    *pid = vcons->owner_id;
  mutex_unlock (&vcons->lock);
  return err;
}
