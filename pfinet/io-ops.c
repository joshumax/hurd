/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

error_t
S_io_write (struct sock_user *user,
	    char *data,
	    u_int datalen,
	    off_t offset,
	    mach_msg_type_number_t *amount)
{
  error_t err;
  
  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&global_lock);
  become_task (user);
  /* O_NONBLOCK for fourth arg? XXX */
  err = - (*user->sock->ops->write) (user->sock, data, datalen, 0);
  mutex_unlock (&global_lock);
  
  return err;
}

error_t
S_io_read (struct sock_user *user,
	   char **data,
	   u_int *datalen,
	   off_t offset,
	   mach_msg_type_number_t amount)
{
  error_t err;
  int alloced = 0;

  if (!user)
    return EOPNOTSUPP;
  
  /* Instead of this, we should peek and the socket and only
     allocate as much as necessary. */
  if (amount > *datalen)
    {
      vm_allocate (mach_task_self (), (vm_address_t *)data, amount, 1);
      alloced = 1;
    }
  
  mutex_lock (&global_lock);
  become_task (user);
  /* O_NONBLOCK for fourth arg? XXX */
  err = (*user->sock->ops->read) (user->sock, *data, amount, 0);
  mutex_unlock (&global_lock);
  
  if (err < 0)
    err = -err;
  else
    {
      *datalen = err;
      if (alloced && page_round (*datalen) < page_round (amount))
	vm_deallocate (mach_task_self (), *data + page_round (*datalen),
		       page_round (amount) - page_round (*datalen));
      err = 0;
    }
  return err;
}

error_t
S_io_seek (struct sock_user *user,
	   off_t offset,
	   int whence,
	   off_t *newp)
{
  return user ? ESPIPE : EOPNOTSUPP;
}

error_t
S_io_readable (struct sock_user *user,
	       mach_msg_type_number_t *amount)
{
  struct sock *sk;
  error_t err;
  
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  become_task (user);
  
  /* We need to avoid calling the Linux ioctl routines,
     so here is a rather ugly break of modularity. */

  sk = (struct sock *) user->sock->data;
  err = 0;
  
  /* Linux's af_inet.c ioctl routine just calls the protocol-specific
     ioctl routine; it's those routines that we need to simulate.  So
     this switch corresponds to the initialization of SK->prot in
     af_inet.c:inet_create. */
  switch (sock->type)
    {
    case SOCK_STREAM:
    case SOCK_SEQPACKET:
      /* These guts are copied from tcp.c:tcp_ioctl. */
      if (sk->state == TCP_LISTEN)
	err = EINVAL;
      else
	{
	  sk->inuse = 1;
	  *amount = tcp_readable (sk);
	  release_sock (sk);
	}
      break;
      
    case SOCK_DGRAM:
      /* These guts are copied from udp.c:udp_ioctl (TIOCINQ). */
      if (sk->state == TCP_LISTEN)
	err = EINVAL;
      else
	/* Boy, I really love the C language. */
	*amount = (skb_peek (&sk->receive_queue)
		   ? : &((struct sk_buff){}))->len;
      break;
      
    case SOCK_RAW:
    default:
      err = EOPNOTSUPP;
      break;
    }

  mutex_unlock (&global_lock);
  return err;
}

error_t
S_io_select (struct sock_user *user,
	     int *select_type,
	     int *id_tag)
{
  struct sock *sk;
  error_t err;
  int avail = 0;
  int cancel = 0;
  struct select_table table;
  struct select_table_elt *elt, *nxt;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&global_lock);
  become_task (user);

  /* In Linux, this means (supposedly) that I/O will never be possible.  
     That's a lose, so prevent it from happening.  */
  assert (user->sock->ops->select);

  condition_init (&table.master_condition);
  table.head = 0;
      
  /* The select function returns one if the specified I/O type is
     immediately possible.  If it returns zero, then it is not
     immediately possible, and it has called select_wait.  Eventually
     it will wakeup the wait queue specified in the select_wait call;
     at that point we should retry the call. */

  for (;;)
    {
      if (*select_type & SELECT_READ)
	avail |= ((*user->sock->ops->select) (user->sock, SEL_IN, &table) 
		  ? SELECT_READ : 0);
      if (*select_type & SELECT_WRITE)
	avail |= ((*user->sock->ops->select) (user->sock, SEL_OUT, &table) 
		  ? SELECT_WRITE : 0);
      if (*select_type & SELECT_URG)
	avail |= ((*user->sock->ops->select) (user->sock, SEL_EX, &table) 
		  ? SELECT_URG : 0);
    
      if (!avail)
	cancel = hurd_condition_wait (&table.master_condition, &global_lock);

      /* Drop the conditions implications and structures allocated in the
	 select table. */
      for (elt = table.head; elt; elt = nxt)
	{
	  condition_unimplies (elt->dependent_condition, 
			       &table.master_condition);
	  nxt = elt->next;
	  free (elt);
	}

      if (avail)
	{
	  mutex_unlock (&global_lock);
	  *select_type = avail;
	  return 0;
	}

      if (cancel)
	{
	  mutex_unlock (&global_lock);
	  return EINTR;
	}
    }
}

/* Establish that the condition in WAIT_ADDRESS should imply
   the condition in P.  Also, add us to the queue in P so
   that the relation can be undone at the proper time. */
void
select_wait (struct wait_queue **wait_address, select_table *p)
{
  struct select_table_elt *elt;
  
  elt = malloc (sizeof (struct select_table_elt));
  elt->dependent_condition = (*wait_address)->c;
  elt->next = p->head;
  p->head = elt;

  condition_implies (elt->dependent_condition, p->master_condition);
}
