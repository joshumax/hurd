/* Packet queues

   Copyright (C) 1995, 1996, 1998, 1999, 2002, 2006
     Free Software Foundation, Inc.

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
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>

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
	{
	  if (head->buf_vm_alloced)
	    munmap (head->buf, head->buf_len);
	  else
	    free (head->buf);
	}
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
      packet->ports_alloced = 0;
      packet->buf_vm_alloced = 0;
    }
  else
    pq->free = packet->next;

  packet->num_ports = 0;
  packet->buf_start = packet->buf_end = packet->buf;

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

/* Returns a legal size to which PACKET can be set allowing enough room for
   EXTRA bytes more than what's already in it, and perhaps more.  */
size_t
packet_new_size (struct packet *packet, size_t extra)
{
  size_t new_len = (packet->buf_end - packet->buf) + extra;
  if (packet->buf_vm_alloced || new_len >= PACKET_SIZE_LARGE)
    /* Round NEW_LEN up to a page boundary (OLD_LEN should already be).  */
    return round_page (new_len);
  else
    /* Otherwise, just round up to a multiple of 512 bytes.  */
    return (new_len + 511) & ~511;
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
    {
      new_buf = mmap (0, new_len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      err = (new_buf == (char *) -1) ? errno : 0;
    }
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
	   But there's not, and memcpy may do vm_copy where it can.  Will we
	   still takes faults on the new copy, even though we've deallocated
	   the old one???  XXX */
	memcpy (new_buf, start, end - start);

      /* And get rid of the old buffer.  */
      if (old_len > 0)
	{
	  if (packet->buf_vm_alloced)
	    vm_deallocate (mach_task_self (), (vm_address_t)old_buf, old_len);
	  else
	    free (old_buf);
	}

      packet->buf = new_buf;
      packet->buf_len = new_len;
      packet->buf_vm_alloced = vm_alloc;
      packet->buf_start = new_buf;
      packet->buf_end = new_buf + (end - start);
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
      mach_port_t *new_ports = malloc (sizeof (mach_port_t) * num_ports);
      if (! new_ports)
	return ENOMEM;
      free (packet->ports);
      packet->ports = new_ports;
      packet->ports_alloced = num_ports;
    }
  memcpy (packet->ports, ports, sizeof (mach_port_t) * num_ports);
  packet->num_ports = num_ports;
  return 0;
}

/* Returns any ports in PACKET in PORTS and NUM_PORTS, and removes them from
   PACKET.  */
error_t
packet_read_ports (struct packet *packet,
		   mach_port_t **ports, size_t *num_ports)
{
  int length = packet->num_ports * sizeof (mach_port_t);
  if (*num_ports < packet->num_ports)
    {
      *ports = mmap (0, length, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*ports == (mach_port_t *) -1)
	return errno;
    }
  *num_ports = packet->num_ports;
  memcpy (*ports, packet->ports, length);
  packet->num_ports = 0;
  return 0;
}

/* Append the bytes in DATA, of length DATA_LEN, to what's already in PACKET,
   and return the amount appended in AMOUNT if that's not the null pointer.  */
error_t
packet_write (struct packet *packet,
	      char *data, size_t data_len, size_t *amount)
{
  error_t err = packet_ensure (packet, data_len);

  if (err)
    return err;

  /* Add the new data.  */
  memcpy (packet->buf_end, data, data_len);
  packet->buf_end += data_len;
  if (amount != NULL)
    *amount = data_len;

  return 0;
}

/* Remove or peek up to AMOUNT bytes from the beginning of the data in PACKET, and
   puts it into *DATA, and the amount read into DATA_LEN.  If more than the
   original *DATA_LEN bytes are available, new memory is vm_allocated, and
   the address and length of this array put into DATA and DATA_LEN.  */
static error_t
packet_fetch (struct packet *packet,
	     char **data, size_t *data_len, size_t amount, int remove)
{
  char *start = packet->buf_start;
  char *end = packet->buf_end;

  if (amount > end - start)
    amount = end - start;

  if (amount > 0)
    {
      char *buf = packet->buf;

      if (remove && packet->buf_vm_alloced && amount >= vm_page_size)
	/* We can return memory from BUF directly without copying.  */
	{
	  if (buf + vm_page_size <= start)
	    /* BUF_START has been advanced past the start of the buffer
	       (perhaps by a series of small reads); as we're going to assume
	       everything before START is gone, make sure we deallocate any
	       memory on pages before those we return to the user.  */
	    vm_deallocate (mach_task_self (),
			   (vm_address_t)buf,
			   trunc_page (start) - (vm_address_t)buf);

	  *data = start;	/* Return the buffer directly.  */
	  start += amount;	/* Advance the read point.  */

	  if (start < end)
	    /* Since returning a partial page actually means returning the
	       whole page, we have to be careful not to grab past the page
	       boundary before the end of the data we want.  */
	    {
	      char *non_aligned_start = start;
	      start = (char *)trunc_page (start);
	      amount -= non_aligned_start - start;
	    }
	  else
	    /* This read will be up to the end of the buffer, so we can just
	       consume any space on the page following BUF_END (vm_alloced
	       buffers are always allocated in whole pages).  */
	    {
	      start = (char *)round_page (start);
	      packet->buf_end = start; /* Ensure BUF_START <= BUF_END.  */
	    }

	  /* We've actually consumed the memory at the start of BUF.  */
	  packet->buf = start;
	  packet->buf_start = start;
	  packet->buf_len -= start - buf;
	}
      else
	/* Just copy the data the old fashioned way....  */
	{
	  if (*data_len < amount)
	    *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

	  memcpy (*data, start, amount);
	  start += amount;

	  if (remove && start - buf > 2 * PACKET_SIZE_LARGE)
	    /* Get rid of unused space at the beginning of the buffer -- we
	       know it's vm_alloced because of the size, and this will allow
	       the buffer to just slide through memory.  Because we wait for
	       a relatively large amount of free space before doing this, and
	       packet_write() would have gotten rid the free space if it
	       didn't require copying much data, it's unlikely that this will
	       happen if it would have been cheaper to just move the packet
	       contents around to make space for the next write.  */
	    {
	      vm_size_t dealloc = trunc_page (start) - (vm_address_t)buf;
	      vm_deallocate (mach_task_self (), (vm_address_t)buf, dealloc);
	      packet->buf = buf + dealloc;
	      packet->buf_len -= dealloc;
	    }

	  if (remove)
	    packet->buf_start = start;
	}
    }
  *data_len = amount;

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
  return packet_fetch (packet, data, data_len, amount, 1);
}

/* Peek up to AMOUNT bytes from the beginning of the data in PACKET, and
   puts it into *DATA, and the amount read into DATA_LEN.  If more than the
   original *DATA_LEN bytes are available, new memory is vm_allocated, and
   the address and length of this array put into DATA and DATA_LEN.  */
error_t
packet_peek (struct packet *packet,
	     char **data, size_t *data_len, size_t amount)
{
  return packet_fetch (packet, data, data_len, amount, 0);
}
