/*
   Copyright (C) 2001, 2014-2019 Free Software Foundation

   Written by Neal H Walfield <neal@cs.uml.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef FSHELP_RLOCK_H
#define FSHELP_RLOCK_H

#include <pthread.h>
#include <string.h>

struct rlock_linked_list
{
  struct rlock_list *next;
  struct rlock_list **prevp;
};

struct rlock_list
{
  loff_t start;
  loff_t len;
  int type;

  struct rlock_linked_list node;
  struct rlock_linked_list po;

  pthread_cond_t wait;
  int waiting;

  void *po_id;
};

extern inline error_t
rlock_list_init (struct rlock_peropen *po, struct rlock_list *l)
{
  memset (l, 0, sizeof (struct rlock_list));
  pthread_cond_init (&l->wait, NULL);
  l->po_id = po->locks;
  return 0;
}

/* void list_list (X ={po,node}, struct rlock_list **head,
		   struct rlock_list *node)

   Insert a node in the given list, X, in sorted order.  */
#define list_link(X, head, node)				\
	do							\
	  {							\
	    struct rlock_list **e;				\
	    for (e = head;					\
		 *e && ((*e)->start < node->start);		\
		 e = &(*e)->X.next)				\
	      ;							\
	    node->X.next = *e;					\
	    if (node->X.next)					\
	      node->X.next->X.prevp = &node->X.next;		\
	    node->X.prevp = e;					\
	    *e = node;						\
	  }							\
	while (0)

/* void list_unlock (X = {po,node}, struct rlock_list *node)  */
#define list_unlink(X, node)					\
	do							\
	  {							\
	    *node->X.prevp = node->X.next;			\
	    if (node->X.next)					\
	      node->X.next->X.prevp = node->X.prevp;		\
	  }							\
	while (0)

#endif /* FSHELP_RLOCK_H */
