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

#ifndef __PQ_H__
#define __PQ_H__

#include <errno.h>
#include <stddef.h>		/* for size_t */
#include <mach/mach.h>

struct packet
{
  /* The packet type, from PACKET_* below.  */
  unsigned short type;

  /* Where this packet was sent from.  */
  void *source;

  /* Buffer space. */
  char *buf;
  size_t buf_len;
  /* Pointers to the data within BUF.  */
  char *buf_start, *buf_end;
  /* True if BUF was allocated using vm_allocate rather than malloc; only
     valid if BUF_LEN > 0.  */
  int buf_vm_alloced;

  /* Port data */
  mach_port_t *ports;
  size_t num_ports, ports_alloced;

  /* Next and previous packets within the packet queue we're part of.  If
     PREV is null, we're at the head of the queue, and if NEXT is null, we're
     at the tail.  */
  struct packet *next, *prev;
};

#define PACKET_TYPE_ANY		0 /* matches any type of packet */
#define PACKET_TYPE_DATA	1
#define PACKET_TYPE_CONTROL	2

/* Sets PACKET's ports to be PORTS, of length NUM_PORTS.  ENOMEM is returned
   if a memory allocation error occurred, otherwise, 0.  */
error_t packet_set_ports (struct packet *packet,
			  mach_port_t *ports, size_t num_ports);

/* If PACKET has any ports, deallocates them.  */
void packet_dealloc_ports (struct packet *packet);

/* Returns the number of bytes of data in PACKET.  */
extern inline size_t
packet_readable (struct packet *packet)
{
  return packet->buf_end - packet->buf_start;
}

/* Append the bytes in DATA, of length DATA_LEN, to what's already in PACKET,
   and return the amount appended in AMOUNT.  */
error_t packet_write (struct packet *packet,
		      char *data, size_t data_len, size_t *amount);

/* Removes up to AMOUNT bytes from the beginning of the data in PACKET, and
   puts it into *DATA, and the amount read into DATA_LEN.  If more than the
   original *DATA_LEN bytes are available, new memory is vm_allocated, and
   the address and length of this array put into DATA and DATA_LEN.  */
error_t packet_read (struct packet *packet,
		     char **data, size_t *data_len, size_t amount);

/* Returns any ports in PACKET in PORTS and NUM_PORTS, and removes them from
   PACKET.  */
error_t packet_read_ports (struct packet *packet,
			   mach_port_t **ports, size_t *num_ports);

/* Return the source addressd in PACKET in SOURCE, deallocating it from
   PACKET.  */
extern inline void
packet_read_source (struct packet *packet, void **source)
{
  *source = packet->source;
  packet->source = 0;
}

/* The packet size above which we start to do things differently to avoid
   copying around data.  */
#define PACKET_SIZE_LARGE	8192

/* Returns a size to which a packet can be set, which will be at least GOAL,
   but perhaps more.  */
size_t packet_size_adjust (size_t goal);

/* Try to extend PACKET to be NEW_LEN bytes long, which should be greater
   than the current packet size.  This should be a valid length -- i.e., if
   it's greater than PAGE_PACKET_SIZE, it should be a mulitple of
   VM_PAGE_SIZE.  If PACKET cannot be extended for some reason, false is
   returned, otherwise true.  */
int packet_extend (struct packet *packet, size_t new_len);

/* Reallocate PACKET to have NEW_LEN bytes of buffer space, which should be
   greater than the current packet size.  This should be a valid length --
   i.e., if it's greater than PAGE_PACKET_SIZE, it should be a multiple of
   VM_PAGE_SIZE.  If an error occurs, PACKET is not modified and the error is
   returned.  */
error_t packet_realloc (struct packet *packet, size_t new_len);

/* Make sure that PACKET has room for at least AMOUNT more bytes, or return
   the reason why not.  */
extern inline error_t
packet_ensure (struct packet *packet, size_t amount)
{
  size_t new_len;
  size_t old_len = packet->buf_len;
  size_t left = packet->buf + old_len - packet->buf_end;

  if (amount < left)
    return 0;

  new_len = packet_size_adjust (old_len + amount - left);
  if (packet_extend (packet, new_len))
    return 0;
  else
    return packet_realloc (packet, new_len);
}

/* Make sure that PACKET has room for at least AMOUNT more bytes, *only* if
   it can be done efficiently, e.g., the packet can be grown in place, rather
   than moving the contents (or there is little enough data so that copying
   it is OK).  True is returned if room was made, false otherwise.  */
extern inline int
packet_ensure_efficiently (struct packet *packet, size_t amount)
{
  size_t new_len;
  size_t old_len = packet->buf_len;
  size_t left = packet->buf + old_len - packet->buf_end;

  if (amount < left)
    return 1;

  new_len = packet_size_adjust (old_len + amount - left);
  if (packet_extend (packet, new_len))
    return 1;
  if ((packet->buf_end - packet->buf_start) < PACKET_SIZE_LARGE)
    return packet_realloc (packet, new_len) == 0;
  return 0;
}

struct pq
{
  struct packet *head, *tail;	/* Packet queue */
  struct packet *free;		/* Free packets */
};

/* Pushes a new packet of type TYPE and source SOURCE, and returns it, or
   NULL if there was an allocation error.  SOURCE is returned to readers of
   the packet, or deallocated by calling pipe_dealloc_addr.  */
struct packet *pq_queue (struct pq *pq, unsigned type, void *source);

/* Returns the tail of the packet queue PQ, which may mean pushing a new
   packet if TYPE and SOURCE do not match the current tail, or this is the
   first packet.  */
extern inline struct packet *
pq_tail (struct pq *pq, unsigned type, void *source)
{
  struct packet *tail = pq->tail;
  if (!tail
      || (type && tail->type != type) || (source && tail->source != source))
    tail = pq_queue (pq, type, source);
  return tail;
}

/* Remove the first packet (if any) in PQ, deallocating any resources it
   holds.  True is returned if a packet was found, false otherwise.  */
int pq_dequeue (struct pq *pq);

/* Returns the next available packet in PQ, without removing it from the
   queue, or NULL if there is none, or the next packet isn't appropiate.  
   A packet is inappropiate if SOURCE is non-NULL its source field doesn't
   match it, or TYPE is non-NULL and the packet's type field doesn't match
   it.  */
extern inline struct packet *
pq_head (struct pq *pq, unsigned type, void *source)
{
  struct packet *head = pq->head;
  if (!head)
    return 0;
  if (type && head->type != type)
    return 0;
  if (source && head->source != source)
    return 0;
  return head;
}

/* The same as pq_head, but first discards the head of the queue.  */
extern inline struct packet *
pq_next (struct pq *pq, unsigned type, void *source)
{
  if (!pq->head)
    return 0;
  pq_dequeue (pq);
  return pq_head (pq, type, source);
}

/* Dequeues all packets in PQ.  */
void pq_drain (struct pq *pq);

/* Create a new packet queue, returning it in PQ.  The only possible error is
   ENOMEM.  */
error_t pq_create (struct pq **pq);

/* Frees PQ and any resources it holds, including deallocating any ports in
   packets left in the queue.  */
void pq_free (struct pq *pq);

#endif /* __PQ_H__ */
