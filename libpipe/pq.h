/* Packet queues

   Copyright (C) 1995, 1996, 2006 Free Software Foundation, Inc.

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
#include <string.h>
#include <mach/mach.h>
#include <features.h>

#ifdef PQ_DEFINE_EI
#define PQ_EI
#else
#define PQ_EI __extern_inline
#endif


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

extern size_t packet_readable (struct packet *packet);

#if defined(__USE_EXTERN_INLINES) || defined(PQ_DEFINE_EI)

/* Returns the number of bytes of data in PACKET.  */
PQ_EI size_t
packet_readable (struct packet *packet)
{
  return packet->buf_end - packet->buf_start;
}

#endif /* Use extern inlines.  */

/* Append the bytes in DATA, of length DATA_LEN, to what's already in PACKET,
   and return the amount appended in AMOUNT if that's not the null pointer.  */
error_t packet_write (struct packet *packet,
		      char *data, size_t data_len, size_t *amount);

/* Removes up to AMOUNT bytes from the beginning of the data in PACKET, and
   puts it into *DATA, and the amount read into DATA_LEN.  If more than the
   original *DATA_LEN bytes are available, new memory is vm_allocated, and
   the address and length of this array put into DATA and DATA_LEN.  */
error_t packet_read (struct packet *packet,
		     char **data, size_t *data_len, size_t amount);

/* Peek up to AMOUNT bytes from the beginning of the data in PACKET, and
   puts it into *DATA, and the amount read into DATA_LEN.  If more than the
   original *DATA_LEN bytes are available, new memory is vm_allocated, and
   the address and length of this array put into DATA and DATA_LEN.  */
error_t packet_peek (struct packet *packet,
		     char **data, size_t *data_len, size_t amount);

/* Returns any ports in PACKET in PORTS and NUM_PORTS, and removes them from
   PACKET.  */
error_t packet_read_ports (struct packet *packet,
			   mach_port_t **ports, size_t *num_ports);

extern void packet_read_source (struct packet *packet, void **source);

#if defined(__USE_EXTERN_INLINES) || defined(PQ_DEFINE_EI)

/* Return the source addressd in PACKET in SOURCE, deallocating it from
   PACKET.  */
PQ_EI void
packet_read_source (struct packet *packet, void **source)
{
  *source = packet->source;
  packet->source = 0;
}

#endif /* Use extern inlines.  */

/* The packet size above which we start to do things differently to avoid
   copying around data.  */
#define PACKET_SIZE_LARGE	8192

/* Returns a legal size to which PACKET can be set allowing enough room for
   EXTRA bytes more than what's already in it, and perhaps more.  */
size_t packet_new_size (struct packet *packet, size_t extra);

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

extern int packet_fit (struct packet *packet, size_t amount);

extern error_t packet_ensure (struct packet *packet, size_t amount);

extern int packet_ensure_efficiently (struct packet *packet, size_t amount);

#if defined(__USE_EXTERN_INLINES) || defined(PQ_DEFINE_EI)

/* Try to make space in PACKET for AMOUNT more bytes without growing the
   buffer, returning true if we could do it.  */
PQ_EI int
packet_fit (struct packet *packet, size_t amount)
{
  char *buf = packet->buf, *end = packet->buf_end;
  size_t buf_len = packet->buf_len;
  size_t left = buf + buf_len - end; /* Free space at the end of the buffer. */

  if (amount > left)
    {
      char *start = packet->buf_start;
      size_t cur_len = end - start; /* Amount of data currently in the buf.  */

      if (buf_len - cur_len >= amount
	  && cur_len < PACKET_SIZE_LARGE && cur_len < (buf_len >> 2))
	/* If we could fit the data in by moving what's already in the
	   buffer, and there's not too much there, and it represents less
	   than 25% of the buffer size, then move the data instead of growing
	   the buffer. */
	{
	  memmove (buf, start, cur_len);
	  packet->buf_start = buf;
	  packet->buf_end = buf + cur_len;
	}
      else
	return 0;		/* We failed... */
    }

  return 1;
}

/* Make sure that PACKET has room for at least AMOUNT more bytes, or return
   the reason why not.  */
PQ_EI error_t
packet_ensure (struct packet *packet, size_t amount)
{
  if (! packet_fit (packet, amount))
    /* We must make the buffer bigger.  */
    {
      size_t new_len = packet_new_size (packet, amount);
      if (! packet_extend (packet, new_len))
	return packet_realloc (packet, new_len);
    }
  return 0;
}

/* Make sure that PACKET has room for at least AMOUNT more bytes, *only* if
   it can be done efficiently, e.g., the packet can be grown in place, rather
   than moving the contents (or there is little enough data so that copying
   it is OK).  True is returned if room was made, false otherwise.  */
PQ_EI int
packet_ensure_efficiently (struct packet *packet, size_t amount)
{
  if (! packet_fit (packet, amount))
    {
      size_t new_len = packet_new_size (packet, amount);
      if (packet_extend (packet, new_len))
	return 1;
      if ((packet->buf_end - packet->buf_start) < PACKET_SIZE_LARGE)
	return packet_realloc (packet, new_len) == 0;
    }
  return 0;
}

#endif /* Use extern inlines.  */

struct pq
{
  struct packet *head, *tail;	/* Packet queue */
  struct packet *free;		/* Free packets */
};

/* Pushes a new packet of type TYPE and source SOURCE, and returns it, or
   NULL if there was an allocation error.  SOURCE is returned to readers of
   the packet, or deallocated by calling pipe_dealloc_addr.  */
struct packet *pq_queue (struct pq *pq, unsigned type, void *source);

extern struct packet * pq_tail (struct pq *pq, unsigned type, void *source);

#if defined(__USE_EXTERN_INLINES) || defined(PQ_DEFINE_EI)

/* Returns the tail of the packet queue PQ, which may mean pushing a new
   packet if TYPE and SOURCE do not match the current tail, or this is the
   first packet.  */
PQ_EI struct packet *
pq_tail (struct pq *pq, unsigned type, void *source)
{
  struct packet *tail = pq->tail;
  if (!tail
      || (type && tail->type != type) || (source && tail->source != source))
    tail = pq_queue (pq, type, source);
  return tail;
}

#endif /* Use extern inlines.  */

/* Remove the first packet (if any) in PQ, deallocating any resources it
   holds.  True is returned if a packet was found, false otherwise.  */
int pq_dequeue (struct pq *pq);

extern struct packet * pq_head (struct pq *pq, unsigned type, void *source);

extern struct packet * pq_next (struct pq *pq, unsigned type, void *source);

#if defined(__USE_EXTERN_INLINES) || defined(PQ_DEFINE_EI)

/* Returns the next available packet in PQ, without removing it from the
   queue, or NULL if there is none, or the next packet isn't appropriate.
   A packet is inappropriate if SOURCE is non-NULL its source field doesn't
   match it, or TYPE is non-NULL and the packet's type field doesn't match
   it.  */
PQ_EI struct packet *
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
PQ_EI struct packet *
pq_next (struct pq *pq, unsigned type, void *source)
{
  if (!pq->head)
    return 0;
  pq_dequeue (pq);
  return pq_head (pq, type, source);
}

#endif /* Use extern inlines.  */

/* Dequeues all packets in PQ.  */
void pq_drain (struct pq *pq);

/* Create a new packet queue, returning it in PQ.  The only possible error is
   ENOMEM.  */
error_t pq_create (struct pq **pq);

/* Frees PQ and any resources it holds, including deallocating any ports in
   packets left in the queue.  */
void pq_free (struct pq *pq);

#endif /* __PQ_H__ */
