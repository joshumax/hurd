/* 
   Copyright (C) 1995, 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
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

#include "pfinet.h"
#include "io_S.h"
#include <netinet/in.h>
#include <linux/wait.h>
#include <linux-inet/sock.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <mach/notify.h>

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
  err = (*user->sock->ops->write) (user->sock, data, datalen,
				   user->sock->userflags);
  mutex_unlock (&global_lock);

  if (err < 0)
    err = -err;
  else
    {
      *amount = err;
      err = 0;
    }
  
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
  err = (*user->sock->ops->read) (user->sock, *data, amount, 
				  user->sock->userflags);
  mutex_unlock (&global_lock);
  
  if (err < 0)
    err = -err;
  else
    {
      *datalen = err;
      if (alloced && round_page (*datalen) < round_page (amount))
	munmap (*data + round_page (*datalen),
		round_page (amount) - round_page (*datalen));
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
  switch (user->sock->type)
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
S_io_set_all_openmodes (struct sock_user *user,
			int bits)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  if (bits & O_NONBLOCK)
    user->sock->userflags |= O_NONBLOCK;
  else
    user->sock->userflags &= ~O_NONBLOCK;
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_get_openmodes (struct sock_user *user,
		    int *bits)
{
  struct sock *sk;
  
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  sk = user->sock->data;
  
  *bits = 0;
  if (!(sk->shutdown & SEND_SHUTDOWN))
    *bits |= O_WRITE;
  if (!(sk->shutdown & RCV_SHUTDOWN))
    *bits |= O_READ;
  if (user->sock->userflags & O_NONBLOCK)
    *bits |= O_NONBLOCK;
  
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_set_some_openmodes (struct sock_user *user,
			 int bits)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  if (bits & O_NONBLOCK)
    user->sock->userflags |= O_NONBLOCK;
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_clear_some_openmodes (struct sock_user *user,
			   int bits)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  if (bits & O_NONBLOCK)
    user->sock->userflags &= ~O_NONBLOCK;
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_select (struct sock_user *user,
	     mach_port_t reply, mach_msg_type_name_t reply_type,
	     int *select_type)
{
  int avail = 0;
  int cancel = 0;
  int requested_notify = 0;
  select_table table;
  struct select_table_elt *elt, *nxt;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&global_lock);
  become_task (user);

  /* In Linux, this means (supposedly) that I/O will never be possible.  
     That's a lose, so prevent it from happening.  */
  assert (user->sock->ops->select);

  /* The select function returns one if the specified I/O type is
     immediately possible.  If it returns zero, then it is not
     immediately possible, and it has called select_wait.  Eventually
     it will wakeup the wait queue specified in the select_wait call;
     at that point we should retry the call. */

  for (;;)
    {
      condition_init (&table.master_condition);
      table.head = 0;
      
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
	{
	  if (! requested_notify)
	    {
	      ports_interrupt_self_on_notification (user, reply,
						    MACH_NOTIFY_DEAD_NAME);
	      requested_notify = 1;
	    }
	  cancel = hurd_condition_wait (&table.master_condition, &global_lock);
	}

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
	  mach_port_deallocate (mach_task_self (), reply);
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
  
  /* tcp.c happens to use an uninitalized wait queue;
     so this special hack is for that. */
  if (*wait_address == 0)
    {
      *wait_address = malloc (sizeof (struct wait_queue));
      condition_init (&(*wait_address)->c);
    }

  elt = malloc (sizeof (struct select_table_elt));
  elt->dependent_condition = &(*wait_address)->c;
  elt->next = p->head;
  p->head = elt;

  condition_implies (elt->dependent_condition, &p->master_condition);
}

error_t
S_io_stat (struct sock_user *user,
	   struct stat *st)
{
  if (!user)
    return EOPNOTSUPP;
  
  bzero (st, sizeof (struct stat));
  
  st->st_fstype = FSTYPE_SOCKET;
  st->st_fsid = getpid ();
  st->st_ino = (ino_t) user->sock; /* why not? */
  
  st->st_blksize = 512;		/* ???? */
  return 0;
}

error_t
S_io_reauthenticate (struct sock_user *user,
		     mach_port_t rend)
{
  struct sock_user *newuser;
  uid_t gubuf[20], ggbuf[20], aubuf[20], agbuf[20];
  uid_t *gen_uids, *gen_gids, *aux_uids, *aux_gids;
  u_int genuidlen, gengidlen, auxuidlen, auxgidlen;
  error_t err;
  int i;
  auth_t auth;
  mach_port_t newright;

  if (!user)
    return EOPNOTSUPP;
  
  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gen_uids = gubuf;
  gen_gids = ggbuf;
  aux_uids = aubuf;
  aux_gids = agbuf;

  mutex_lock (&global_lock);
  newuser = make_sock_user (user->sock, 0, 1);
  
  auth = getauth ();
  newright = ports_get_right (newuser);
  err = mach_port_insert_right (mach_task_self (), newright, newright,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror (err);
  do
    err = auth_server_authenticate (auth, 
				    rend,
				    MACH_MSG_TYPE_COPY_SEND,
				    newright,
				    MACH_MSG_TYPE_COPY_SEND,
				    &gen_uids, &genuidlen, 
				    &aux_uids, &auxuidlen,
				    &gen_gids, &gengidlen,
				    &aux_gids, &auxgidlen);
  while (err == EINTR);
  mach_port_deallocate (mach_task_self (), rend);
  mach_port_deallocate (mach_task_self (), newright);
  mach_port_deallocate (mach_task_self (), auth);

  if (err)
    newuser->isroot = 0;
  else
    for (i = 0; i < genuidlen; i++)
      if (gen_uids[i] == 0)
	newuser->isroot = 1;

  mach_port_move_member (mach_task_self (), newuser->pi.port_right,
			 pfinet_bucket->portset);

  mutex_unlock (&global_lock);

  ports_port_deref (newuser);

  if (gubuf != gen_uids)
    munmap (gen_uids, genuidlen * sizeof (uid_t));
  if (ggbuf != gen_gids)
    munmap (gen_gids, gengidlen * sizeof (uid_t));
  if (aubuf != aux_uids)
    munmap (aux_uids, auxuidlen * sizeof (uid_t));
  if (agbuf != aux_gids)
    munmap (aux_gids, auxgidlen * sizeof (uid_t));

  return 0;
}

error_t
S_io_restrict_auth (struct sock_user *user,
		    mach_port_t *newobject,
		    mach_msg_type_name_t *newobject_type,
		    uid_t *uids,
		    u_int uidslen,
		    uid_t *gids,
		    u_int gidslen)
{
  struct sock_user *newuser;
  int i = 0;
  int isroot;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&global_lock);

  isroot = 0;
  if (user->isroot)
    for (i = 0; i < uidslen && !isroot; i++)
      if (uids[i] == 0)
	isroot = 1;
  
  newuser = make_sock_user (user->sock, isroot, 0);
  *newobject = ports_get_right (newuser);
  *newobject_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newuser);
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_duplicate (struct sock_user *user,
		mach_port_t *newobject,
		mach_msg_type_name_t *newobject_type)
{
  struct sock_user *newuser;
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  newuser = make_sock_user (user->sock, user->isroot, 0);
  *newobject = ports_get_right (newuser);
  *newobject_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newuser);
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_identity (struct sock_user *user,
	       mach_port_t *id,
	       mach_msg_type_name_t *idtype,
	       mach_port_t *fsys,
	       mach_msg_type_name_t *fsystype,
	       int *fileno)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  if (user->sock->identity == MACH_PORT_NULL)
    {
      err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				&user->sock->identity);
      if (err)
	{
	  mutex_unlock (&global_lock);
	  return err;
	}
    }

  *id = user->sock->identity;
  *idtype = MACH_MSG_TYPE_MAKE_SEND;
  *fsys = fsys_identity;
  *fsystype = MACH_MSG_TYPE_MAKE_SEND;
  *fileno = (ino_t) user->sock;	/* matches S_io_stat above */
  
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_revoke (struct sock_user *user)
{
  /* XXX maybe we should try */
  return EOPNOTSUPP;
}



error_t
S_io_async (struct sock_user *user,
	    mach_port_t notify,
	    mach_port_t *id,
	    mach_msg_type_name_t *idtype)
{
  return EOPNOTSUPP;
}

error_t
S_io_mod_owner (struct sock_user *user,
		pid_t owner)
{
  return EOPNOTSUPP;
}

error_t
S_io_get_owner (struct sock_user *user,
		pid_t *owner)
{
  return EOPNOTSUPP;
}
 
error_t  
S_io_get_icky_async_id (struct sock_user *user,
			mach_port_t *id,
			mach_msg_type_name_t *idtype)
{
  return EOPNOTSUPP;
}

error_t
S_io_server_version (struct sock_user *user,
		     char *name,
		     int *major,
		     int *minor,
		     int *edit)
{
  return EOPNOTSUPP;
}

error_t
S_io_pathconf (struct sock_user *user,
	       int name,
	       int *value)
{
  return EOPNOTSUPP;
}



error_t
S_io_map (struct sock_user *user,
	  mach_port_t *rdobj,
	  mach_msg_type_name_t *rdobj_type,
	  mach_port_t *wrobj,
	  mach_msg_type_name_t *wrobj_type)
{
  return EOPNOTSUPP;
}

error_t
S_io_map_cntl (struct sock_user *user,
	       mach_port_t *obj,
	       mach_msg_type_name_t *obj_type)
{
  return EOPNOTSUPP;
}

error_t
S_io_get_conch (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_release_conch (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_eofnotify (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_prenotify (struct sock_user *user,
		vm_offset_t start,
		vm_offset_t end)
{
  return EOPNOTSUPP;
}

error_t
S_io_postnotify (struct sock_user *user,
		 vm_offset_t start,
		 vm_offset_t end)
{
  return EOPNOTSUPP;
}

error_t
S_io_readnotify (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_readsleep (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_sigio (struct sock_user *user)
{
  return EOPNOTSUPP;
}

