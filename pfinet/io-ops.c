/*
   Copyright (C) 1995,96,97,98,99,2000,02 Free Software Foundation, Inc.
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

#include <linux/wait.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <net/sock.h>

#include "io_S.h"
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <mach/notify.h>
#include <sys/mman.h>

error_t
S_io_write (struct sock_user *user,
	    data_t data,
	    size_t datalen,
	    off_t offset,
	    mach_msg_type_number_t *amount)
{
  error_t err;
  struct iovec iov = { data, datalen };
  struct msghdr m = { msg_name: 0, msg_namelen: 0, msg_flags: 0,
		      msg_controllen: 0, msg_iov: &iov, msg_iovlen: 1 };

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  become_task (user);
  if (user->sock->flags & O_NONBLOCK)
    m.msg_flags |= MSG_DONTWAIT;
  err = (*user->sock->ops->sendmsg) (user->sock, &m, datalen, 0);
  pthread_mutex_unlock (&global_lock);

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
	   data_t *data,
	   size_t *datalen,
	   off_t offset,
	   mach_msg_type_number_t amount)
{
  error_t err;
  int alloced = 0;
  struct iovec iov;
  struct msghdr m = { msg_name: 0, msg_namelen: 0, msg_flags: 0,
		      msg_controllen: 0, msg_iov: &iov, msg_iovlen: 1 };

  if (!user)
    return EOPNOTSUPP;

  /* Instead of this, we should peek and the socket and only
     allocate as much as necessary. */
  if (amount > *datalen)
    {
      *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*data == MAP_FAILED)
        /* Should check whether errno is indeed ENOMEM --
           but this can't be done in a straightforward way,
           because the glue headers #undef errno. */
        return ENOMEM;
      alloced = 1;
    }

  iov.iov_base = *data;
  iov.iov_len = amount;

  pthread_mutex_lock (&global_lock);
  become_task (user);
  err = (*user->sock->ops->recvmsg) (user->sock, &m, amount,
				     ((user->sock->flags & O_NONBLOCK)
    				      ? MSG_DONTWAIT : 0),
				     0);
  pthread_mutex_unlock (&global_lock);

  if (err < 0)
    {
      err = -err;
      if (alloced)
	munmap (*data, amount);
    }
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

  pthread_mutex_lock (&global_lock);
  become_task (user);

  /* We need to avoid calling the Linux ioctl routines,
     so here is a rather ugly break of modularity. */

  sk = user->sock->sk;
  err = 0;

  /* Linux's af_inet.c ioctl routine just calls the protocol-specific
     ioctl routine; it's those routines that we need to simulate.  So
     this switch corresponds to the initialization of SK->prot in
     af_inet.c:inet_create. */
  switch (user->sock->type)
    {
    case SOCK_STREAM:
    case SOCK_SEQPACKET:
      err = tcp_tiocinq (sk, amount);
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

  pthread_mutex_unlock (&global_lock);
  return err;
}

error_t
S_io_set_all_openmodes (struct sock_user *user,
			int bits)
{
  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  if (bits & O_NONBLOCK)
    user->sock->flags |= O_NONBLOCK;
  else
    user->sock->flags &= ~O_NONBLOCK;
  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_get_openmodes (struct sock_user *user,
		    int *bits)
{
  struct sock *sk;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  sk = user->sock->sk;

  *bits = 0;
  if (!(sk->shutdown & SEND_SHUTDOWN))
    *bits |= O_WRITE;
  if (!(sk->shutdown & RCV_SHUTDOWN))
    *bits |= O_READ;
  if (user->sock->flags & O_NONBLOCK)
    *bits |= O_NONBLOCK;

  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_set_some_openmodes (struct sock_user *user,
			 int bits)
{
  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  if (bits & O_NONBLOCK)
    user->sock->flags |= O_NONBLOCK;
  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_clear_some_openmodes (struct sock_user *user,
			   int bits)
{
  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  if (bits & O_NONBLOCK)
    user->sock->flags &= ~O_NONBLOCK;
  pthread_mutex_unlock (&global_lock);
  return 0;
}

static error_t
io_select_common (struct sock_user *user,
		  mach_port_t reply,
		  mach_msg_type_name_t reply_type,
		  struct timespec *tsp, int *select_type)
{
  const int want = *select_type | POLLERR;
  int avail, timedout;
  int ret = 0;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  become_task (user);

  /* In Linux, this means (supposedly) that I/O will never be possible.
     That's a lose, so prevent it from happening.  */
  assert_backtrace (user->sock->ops->poll);

  avail = (*user->sock->ops->poll) ((void *) 0xdeadbeef,
				    user->sock,
				    (void *) 0xdeadbead);
  if ((avail & want) == 0)
    {
      ports_interrupt_self_on_notification (user, reply,
					    MACH_NOTIFY_DEAD_NAME);

      do
	{
	  /* Block until we time out, are woken or cancelled.  */
	  timedout = interruptible_sleep_on_timeout (user->sock->sk->sleep,
						     tsp);
	  if (timedout)
	    {
	      pthread_mutex_unlock (&global_lock);
	      *select_type = 0;
	      return 0;
	    }
	  else if (signal_pending (current)) /* This means we were cancelled.  */
	    {
	      pthread_mutex_unlock (&global_lock);
	      return EINTR;
	    }
	  avail = (*user->sock->ops->poll) ((void *) 0xdeadbeef,
					    user->sock,
					    (void *) 0xdeadbead);
	}
      while ((avail & want) == 0);
    }

  if (avail & POLLERR)
    ret = EIO;
  else
    /* We got something.  */
    *select_type = avail;

  pthread_mutex_unlock (&global_lock);

  return ret;
}

error_t
S_io_select (struct sock_user *user,
	     mach_port_t reply,
	     mach_msg_type_name_t reply_type,
	     int *select_type)
{
  return io_select_common (user, reply, reply_type, NULL, select_type);
}

error_t
S_io_select_timeout (struct sock_user *user,
		     mach_port_t reply,
		     mach_msg_type_name_t reply_type,
		     struct timespec ts,
		     int *select_type)
{
  return io_select_common (user, reply, reply_type, &ts, select_type);
}

error_t
S_io_stat (struct sock_user *user,
	   struct stat *st)
{
  if (!user)
    return EOPNOTSUPP;

  memset (st, 0, sizeof(struct stat));

  st->st_fstype = FSTYPE_SOCKET;
  st->st_fsid = getpid ();
  st->st_ino = user->sock->st_ino;

  st->st_mode = S_IFSOCK | ACCESSPERMS;
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
  size_t genuidlen, gengidlen, auxuidlen, auxgidlen;
  error_t err;
  size_t i, j;
  auth_t auth;
  mach_port_t newright;

  if (!user)
    return EOPNOTSUPP;

  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gen_uids = gubuf;
  gen_gids = ggbuf;
  aux_uids = aubuf;
  aux_gids = agbuf;

  pthread_mutex_lock (&global_lock);
  do
    newuser = make_sock_user (user->sock, 0, 1, 0);
    /* Should check whether errno is indeed EINTR --
       but this can't be done in a straightforward way,
       because the glue headers #undef errno. */
  while (!newuser);

  auth = getauth ();
  newright = ports_get_send_right (newuser);
  assert_backtrace (newright != MACH_PORT_NULL);
  /* Release the global lock while blocking on the auth server and client.  */
  pthread_mutex_unlock (&global_lock);
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
  pthread_mutex_lock (&global_lock);
  mach_port_deallocate (mach_task_self (), rend);
  mach_port_deallocate (mach_task_self (), newright);
  mach_port_deallocate (mach_task_self (), auth);

  if (err)
    newuser->isroot = 0;
  else
    /* Check permission as fshelp_isowner would do.  */
    for (i = 0; i < genuidlen; i++)
      {
	if (gen_uids[i] == 0 || gen_uids[i] == pfinet_owner)
	  newuser->isroot = 1;
	if (gen_uids[i] == pfinet_group)
	  for (j = 0; j < gengidlen; j++)
	    if (gen_gids[j] == pfinet_group)
	      newuser->isroot = 1;
      }

  mach_port_move_member (mach_task_self (), newuser->pi.port_right,
			 pfinet_bucket->portset);

  pthread_mutex_unlock (&global_lock);

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
		    uid_t *uids, size_t uidslen,
		    uid_t *gids, size_t gidslen)
{
  struct sock_user *newuser;
  int i, j;
  int isroot;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  isroot = 0;
  if (user->isroot)
    /* Check permission as fshelp_isowner would do.  */
    for (i = 0; i < uidslen; i++)
      {
	if (uids[i] == 0 || uids[i] == pfinet_owner)
	  isroot = 1;
	if (uids[i] == pfinet_group)
	  for (j = 0; j < gidslen; j++)
	    if (gids[j] == pfinet_group)
	      isroot = 1;
      }

  newuser = make_sock_user (user->sock, isroot, 0, 0);
  *newobject = ports_get_right (newuser);
  *newobject_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newuser);
  pthread_mutex_unlock (&global_lock);
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

  pthread_mutex_lock (&global_lock);
  newuser = make_sock_user (user->sock, user->isroot, 0, 0);
  *newobject = ports_get_right (newuser);
  *newobject_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newuser);
  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t
S_io_identity (struct sock_user *user,
	       mach_port_t *id,
	       mach_msg_type_name_t *idtype,
	       mach_port_t *fsys,
	       mach_msg_type_name_t *fsystype,
	       ino_t *fileno)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  if (user->sock->identity == MACH_PORT_NULL)
    {
      err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				&user->sock->identity);
      if (err)
	{
	  pthread_mutex_unlock (&global_lock);
	  return err;
	}
    }

  *id = user->sock->identity;
  *idtype = MACH_MSG_TYPE_MAKE_SEND;
  *fsys = fsys_identity;
  *fsystype = MACH_MSG_TYPE_MAKE_SEND;
  *fileno = user->sock->st_ino;

  pthread_mutex_unlock (&global_lock);
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
