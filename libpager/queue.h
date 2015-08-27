/* A FIFO queue with constant-time enqueue and dequeue operations.

   Copyright (C) 2014 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdbool.h>

/* A FIFO queue with constant-time enqueue and dequeue operations.  */
struct item {
  struct item *next;
};

struct queue {
  struct item *head;
  struct item **tail;
};

static inline void
queue_init (struct queue *q)
{
  q->head = NULL;
  q->tail = &q->head;
}

static inline void
queue_enqueue (struct queue *q, struct item *r)
{
  *q->tail = r;
  q->tail = &r->next;
  r->next = NULL;
}

static inline void *
queue_dequeue (struct queue *q)
{
  struct item *r = q->head;
  if (r == NULL)
    return NULL;

  /* Pop the first item off.  */
  if ((q->head = q->head->next) == NULL)
    /* The queue is empty, fix tail pointer.  */
    q->tail = &q->head;

  r->next = NULL;
  return r;
}

static inline bool
queue_empty (struct queue *q)
{
  return q->head == NULL;
}
