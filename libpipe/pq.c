/* Packet queues

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

#include <malloc.h>
#include <string.h>		/* for bcopy */
#include <stddef.h>
#include <assert.h>

#include "pq.h"

/* ---------------------------------------------------------------- */

/* Create a new packet queue, returning it in PQ.  The only possible error is
   ENOMEM.  */
error_t
pq_create (struct pq **pq)
{
  *pq = malloc (sizeof (struct pq));

  if (! *pq)
    return ENOMEM;

  (*pq)->head = (*pq)->tail = 0;
  (*pq)->free = 0;

  return 0;
}

/* Free every packet (and its contents) in the linked list rooted at HEAD.  */
static void
free_packets (struct packet *head)
{
  if (head)
    {
      struct packet *next = head->next;
      if (head->ports)
	free (head->ports);
      if (head->buf_len > 0)
	if (head->buf_vm_alloced)
	  vm_deallocate (mach_task_self (),
			 (vm_address_t)head->buf, head->buf_len);
	else
	  free (head->buf);
      free (head);
      free_packets (next);
    }
}

/* Frees PQ and any resources it holds, including deallocating any ports in
   packets left in the queue.  */
void
pq_free (struct pq *pq)
{
  pq_drain (pq);
  free_packets (pq->free);
  free (pq);
}

/* ---------------------------------------------------------------- */

/* Remove the first packet (if any) in PQ, deallocating any resources it
   holds.  True is returned if a packet was found, false otherwise.  */
int
pq_dequeue (struct pq *pq)
{
  extern void pipe_dealloc_addr (void *addr);
  struct packet *packet = pq->head;

  if (! packet)
    return 0;

  /* Deallocate any resource in PACKET.  */
  if (packet->num_ports)
    packet_dealloc_ports (packet);
  if (packet->source)
    pipe_dealloc_addr (packet->source);

  pq->head = packet->next;
  packet->next = pq->free;
  pq->free = packet;
  if (pq->head)
    pq->head->prev = 0;
  else
    pq->tail = 0;

  return 1;
}

/* Empties out PQ.  This *will* deallocate any ports in any of the packets.  */
void
pq_drain (struct pq *pq)
{
  while (pq_dequeue (pq))
    ;
}

/* Pushes a new packet of type TYPE and source SOURCE onto the tail of the
   queue, and returns it, or 0 if there was an allocation error. */
struct packet *
pq_queue (struct pq *pq, unsigned type, void *source)
{
  struct packet *packet = pq->free;

  if (!packet)
    {
      packet = malloc (sizeof (struct packet));
      if (!packet)
	return 0;
      packet->buf = 0;
      packet->buf_len = 0;
      packet->ports = 0;
      packet->num_ports = packet->ports_alloced = 0;
      packet->buf_start = packet->buf_end = packet->buf;
    }
  else
    pq->free = packet->next;

  packet->type = type;
  packet->source = source;
  packet->next = 0;
  packet->prev = pq->tail;
  if (pq->tail)
    pq->tail->next = packet;
  pq->tail = packet;
  if (!pq->head)
    pq->head = packet;

  return packet;
}

/* ---------------------------------------------------------------- */

/* Returns a size to which a packet can be set, which will be at least GOAL,
   but perhaps more.  */
size_t
packet_size_adjust (size_t goal)
{
  if (goal > PACKET_SIZE_LARGE)
    /* Round GOAL up to a page boundary (OLD_LEN should already be).  */
    return round_page (goal);
  else
    /* Otherwise, just round up to a multiple of 512 bytes.  */
    return (goal + 511) & ~511;
}

/* Try to extend PACKET to be NEW_LEN bytes long, which should be greater
   than the current packet size.  This should be a valid length -- i.e., if
   it's greater than PACKET_SIZE_LARGE, it should be a mulitple of
   VM_PAGE_SIZE.  If PACKET cannot be extended for some reason, false is
   returned, otherwise true.  */
int
packet_extend (struct packet *packet, size_t new_len)
{
  size_t old_len = packet->buf_len;

  if (old_len == 0)
    /* No existing buffer to extend.  */
    return 0;

  if (packet->buf_vm_alloced)
    /* A vm_alloc'd packet.  */
    {
      char *extension = packet->buf + old_len;

      /* Try to allocate memory at the end of our current buffer.  */
      if (vm_allocate (mach_task_self (),
		       (vm_address_t *)&extension, new_len - old_len, 0) != 0)
	return 0;
    }
  else
    /* A malloc'd packet.  */
    {
      char *new_buf;
      char *old_buf = packet->buf;

      if (new_len >= PACKET_SIZE_LARGE)
	/* The old packet length is malloc'd, but we want to vm_allocate the
	   new length, so we'd have to copy the old contents.  */
	return 0;

      new_buf = realloc (old_buf, new_len);
      if (! new_buf)
	return 0;

      packet->buf = new_buf;
      packet->buf_start = new_buf + (packet->buf_start  - old_buf);
      packet->buf_end = new_buf + (packet->buf_end  - old_buf);
    }
  
  packet->buf_len = new_len;

  return 1;
}

/* Reallocate PACKET to have NEW_LEN bytes of buffer space, which should be
   greater than the current packet size.  This should be a valid length --
   i.e., if it's greater than PACKET_SIZE_LARGE, it should be a multiple of
   VM_PAGE_SIZE.  If an error occurs, PACKET is not modified and the error is
   returned.  */
error_t
packet_realloc (struct packet *packet, size_t new_len)
{
  error_t err;
  char *new_buf;
  char *old_buf = packet->buf;
  int vm_alloc = (new_len >= PACKET_SIZE_LARGE);

  /* Make a new buffer.  */
  if (vm_alloc)
    err =
      vm_allocate (mach_task_self (), (vm_address_t *)&new_buf, new_len, 1);
  else
    {
      new_buf = malloc (new_len);
      err = (new_buf ? 0 : ENOMEM);
    }

  if (! err)
    {
      size_t old_len = packet->buf_len;
      char *start = packet->buf_start, *end = packet->buf_end;

      /* Copy what we must.  */
      if (end != start)
	/* If there was an operation like vm_move, we could use that in the
	   case where both the old and the new buffers were vm_alloced (on
	   the assumption that creating COW pages is somewhat more costly).
	   But there's not, and bcopy will do vm_copy where it can.  Will we
	   still takes faults on the new copy, even though we've deallocated
	   the old one???  XXX */
	bcopy (start, new_buf, end - start);

      /* And get rid of the old buffer.  */
      if (old_len > 0)
	if (packet->buf_vm_alloced)
	  vm_deallocate (mach_task_self (), (vm_address_t)old_buf, old_len);
	else
	  free (old_buf);

      packet->buf = new_buf;
      packet->buf_len = new_len;
      packet->buf_vm_alloced = vm_alloc;
      packet->buf_start = new_buf + (start - old_buf);
      packet->buf_end = new_buf + (end - old_buf);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* If PACKET has any ports, deallocates them.  */
void 
packet_dealloc_ports (struct packet *packet)
{
  unsigned i;
  for (i = 0; i < packet->num_ports; i++)
    {
      mach_port_t port = packet->ports[i];
      if (port != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), port);
    }
}

/* Sets PACKET's ports to be PORTS, of length NUM_PORTS.  ENOMEM is returned
   if a memory allocation error occurred, otherwise, 0.  */
error_t
packet_set_ports (struct packet *packet,
		  mach_port_t *ports, size_t num_ports)
{
  if (packet->num_ports > 0)
    packet_dealloc_ports (packet);
  if (num_ports > packet->ports_alloced)
    {
      mach_port_t *new_ports = malloc (sizeof (mach_port_t *) * num_ports);
      if (! new_ports)
	return ENOMEM;
      free (packet->ports);
      packet->ports_alloced = num_ports;
    }
  bcopy (ports, packet->ports, sizeof (mach_port_t *) * num_ports);
  packet->num_ports = num_ports;
  return 0;
}

/* Returns any ports in PACKET in PORTS and NUM_PORTS, and removes them from
   PACKET.  */
error_t
packet_read_ports (struct packet *packet,
		   mach_port_t **ports, size_t *num_ports)
{
  int length = packet->num_ports * sizeof (mach_port_t *);
  if (*num_ports < packet->num_ports)
    {
      error_t err =
	vm_allocate (mach_task_self (), (vm_address_t *)ports, length, 1);
      if (err)
	return err;
    }
  *num_ports = packet->num_ports;
  bcopy (packet->ports, *ports, length);
  packet->num_ports = 0;
  return 0;
}

/* Append the bytes in DATA, of length DATA_LEN, to what's already in PACKET,
   and return the amount appended in AMOUNT.  */
error_t 
packet_write (struct packet *packet,
	      char *data, size_t data_len, size_t *amount)
{
  error_t err = packet_ensure (packet, data_len);

  if (err)
    return err;

  /* Add the new data.  */
  bcopy (data, packet->buf_end, data_len);
  packet->buf_end += data_len;
  *amount = data_len;

  return 0;
}

/* Removes up to AMOUNT bytes from the beginning of the data in PACKET, and
   puts it into *DATA, and the amount read into DATA_LEN.  If more than the
   original *DATA_LEN bytes are available, new memory is vm_allocated, and
   the address and length of this array put into DATA and DATA_LEN.  */
error_t
packet_read (struct packet *packet,
	     char **data, size_t *data_len, size_t amount)
{
  char *start = packet->buf_start;

  if (amount > packet->buf_end - start)
    amount = packet->buf_end - start;

  if (amount > 0)
    {
      if (packet->buf_vm_alloced && amount > vm_page_size)
	/* We can return memory from BUF directly without copying.  */
	{
	  char *buf = packet->buf;
	  char *end = packet->buf_end;

	  /* Return the buffer directly.  */
	  *data = start;

	  if (buf > start)
	    /* BUF_START has been advanced past the start of the buffer
	       (perhaps by a series of small reads); as we're going to assume
	       everything before START is gone, make sure we deallocate any
	       memory on pages before those we return to the user.  */
	    {
	      char *first_page = (char *)trunc_page (start);
	      if (first_page > buf)
		vm_deallocate (mach_task_self (),
			       (vm_address_t)buf, first_page - buf);
	    }

	  if (start + amount < end)
	    /* Since returning a partial page actually means returning the
	       whole page, we have to be careful not to grab past the page
	       boundary before the end of the data we want, unless the rest
	       of the page is unimportant.  */
	    amount = (char *)trunc_page (start + amount) - start;

	  /* Advance the read point.  */
	  start = (char *)round_page (start + amount);

	  if (start > end)
	    /* Make sure BUF_START is never beyond BUF_END (page-aligning the
	       new BUF_START may have move it past).  */
	    {
	      packet->buf_end = start;
	      packet->buf_len = 0; /* Pin at 0, despite moving past the end. */
	    }
	  else
	    /* Adjust BUF_LEN to reflect what the read has consumed.  */
	    packet->buf_len -= start - buf;

	  /* We've actually consumed the memory at the start of BUF.  */
	  packet->buf = start;
	  packet->buf_start = start;
	}
      else
	/* Just copy the data the old fashioned way....  */
	{
	  if (*data_len < amount)
	    vm_allocate (mach_task_self (), (vm_address_t *)data, amount, 1);
	  bcopy (start, *data, amount);
	  packet->buf_start = start + amount;
	}
    }
  *data_len = amount;

  return 0;
}
