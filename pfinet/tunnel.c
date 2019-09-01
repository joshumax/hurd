/*
   Copyright (C) 1995,96,98,99,2000,02 Free Software Foundation, Inc.
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

#include <hurd.h>
#include <pthread.h>
#include <fcntl.h>
#include <device/device.h>
#include <device/net_status.h>
#include <netinet/in.h>
#include <string.h>
#include <error.h>
#include <errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>

#include "libtrivfs/trivfs_fs_S.h"
#include "libtrivfs/trivfs_io_S.h"

struct port_class *tunnel_cntlclass;
struct port_class *tunnel_class;

struct tunnel_device
{
  struct tunnel_device *next;
  struct trivfs_control *cntl;
  char *devname;
  file_t underlying;
  struct iouser *user;
  struct sk_buff_head xq;         /* Transmit queue.  */
  pthread_cond_t wait;            /* For read and select.  */
  pthread_cond_t select_alert;    /* For read and select.  */
  pthread_mutex_t lock;           /* For read and select.  */
  int read_blocked;               /* For read and select.  */
  struct device dev;
  struct net_device_stats stats;
};


/* Linked list of all tunnel devices.  */
struct tunnel_device *tunnel_dev;


struct net_device_stats *
tunnel_get_stats (struct device *dev)
{
  struct tunnel_device *tdev = (struct tunnel_device *) dev->priv;

  assert_backtrace (tdev);

  return &tdev->stats;
}

int
tunnel_stop (struct device *dev)
{
  struct tunnel_device *tdev = (struct tunnel_device *) dev->priv;
  struct sk_buff *skb;

  assert_backtrace (tdev);

  while ((skb = skb_dequeue (&tdev->xq)) != 0)
    dev_kfree_skb(skb);

  /* Call those only if removing the device completely.  */
  /*  free (tdev->devname); */
  /* XXX???  mach_port_deallocate (mach_task_self, tdev->underlying)  */
  return 0;
}

void
tunnel_set_multi (struct device *dev)
{
}

void
tunnel_initialize (void)
{
}

int
tunnel_open (struct device *dev)
{
  struct tunnel_device *tdev = (struct tunnel_device *) dev->priv;

  assert_backtrace (tdev);

  skb_queue_head_init(&tdev->xq);

  return 0;
}

/* Transmit an ethernet frame */
int
tunnel_xmit (struct sk_buff *skb, struct device *dev)
{
  struct tunnel_device *tdev = (struct tunnel_device *) dev->priv;

  assert_backtrace (tdev);

  pthread_mutex_lock (&tdev->lock);

  /* Avoid unlimited growth.  */
  if (skb_queue_len(&tdev->xq) > 128)
    {
      struct sk_buff *skb;

      skb = skb_dequeue(&tdev->xq);
      dev_kfree_skb(skb);
    }

  /* Queue it for processing.  */
  skb_queue_tail(&tdev->xq, skb);

  if (tdev->read_blocked)
    {
      tdev->read_blocked = 0;
      pthread_cond_broadcast (&tdev->wait);
      pthread_cond_broadcast (&tdev->select_alert);
    }

  pthread_mutex_unlock (&tdev->lock);

  return 0;
}

void
setup_tunnel_device (char *name, struct device **device)
{
  error_t err;
  struct tunnel_device *tdev;
  struct device *dev;
  char *base_name;

  /* Do global initialization before setting up first tunnel device. */
  if (!tunnel_dev)
    {
      trivfs_add_control_port_class (&tunnel_cntlclass);
      trivfs_add_protid_port_class (&tunnel_class);
    }

  tdev = calloc (1, sizeof (struct tunnel_device));
  if (!tdev)
    error (2, ENOMEM, "%s", name);
  tdev->next = tunnel_dev;
  tunnel_dev = tdev;

  *device = dev = &tdev->dev;

  base_name = strrchr (name, '/');
  if (base_name)
    base_name++;
  else
    base_name = name;

  dev->name = strdup (base_name);

  dev->priv = tdev;
  dev->get_stats = tunnel_get_stats;

  /* Functions.  These ones are the true "hardware layer" in Linux.  */
  dev->open = tunnel_open;
  dev->stop = tunnel_stop;
  dev->hard_start_xmit = tunnel_xmit;
  dev->set_multicast_list = tunnel_set_multi;

  /* These are the ones set by drivers/net/ppp_generic.c::ppp_net_init.  */
  dev->hard_header = 0;
  dev->hard_header_len = 0;
  dev->mtu = PPP_MTU;
  dev->addr_len = 0;
  dev->tx_queue_len = 3;
  dev->type = ARPHRD_PPP;
  dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;

  dev_init_buffers (dev);

  if (base_name != name)
    tdev->devname = strdup (name);
  else
    /* Setting up the translator at /dev/tunX.  */
    asprintf (&tdev->devname, "/dev/%s", tdev->dev.name);
  tdev->underlying = file_name_lookup (tdev->devname, O_CREAT|O_NOTRANS, 0664);

  if (tdev->underlying == MACH_PORT_NULL)
    error (2, /* XXX */ 1, "%s", tdev->dev.name);

  err = trivfs_create_control (tdev->underlying, tunnel_cntlclass,
				 pfinet_bucket, tunnel_class, pfinet_bucket,
				 &tdev->cntl);
  tdev->cntl->hook = tdev;

  if (! err)
    {
      mach_port_t right = ports_get_send_right (tdev->cntl);
      err = file_set_translator (tdev->underlying, 0, FS_TRANS_EXCL
				 | FS_TRANS_SET, 0, 0, 0, right,
				 MACH_MSG_TYPE_COPY_SEND);
      mach_port_deallocate (mach_task_self (), right);
    }

  if (err)
    error (2, err, "%s", tdev->dev.name);

  pthread_mutex_init (&tdev->lock, NULL);
  pthread_cond_init (&tdev->wait, NULL);
  pthread_cond_init (&tdev->select_alert, NULL);

  /* This call adds the device to the `dev_base' chain,
     initializes its `ifindex' member (which matters!),
     and tells the protocol stacks about the device.  */
  err = - register_netdevice (dev);
  assert_perror_backtrace (err);
}

/* If a new open with read and/or write permissions is requested,
   restrict to exclusive usage.  */
static error_t
check_open_hook (struct trivfs_control *cntl,
		 struct iouser *user,
		 int flags)
{
  struct tunnel_device *tdev;

  for (tdev = tunnel_dev; tdev; tdev = tdev->next)
    if (tdev->cntl == cntl)
      break;

  if (tdev && flags != O_NORW)
    {
      if (tdev->user)
	return EBUSY;
      else
	tdev->user = user;
    }
  return 0;
}

/* When a protid is destroyed, check if it is the current user.
   If yes, release the interface for other users.  */
static void
pi_destroy_hook (struct trivfs_protid *cred)
{
  struct tunnel_device *tdev;

  if (cred->pi.class != tunnel_class)
    return;

  tdev = (struct tunnel_device *) cred->po->cntl->hook;

  if (tdev->user == cred->user)
    tdev->user = 0;
}

/* If this variable is set, it is called every time a new peropen
   structure is created and initialized. */
error_t (*trivfs_check_open_hook)(struct trivfs_control *,
				  struct iouser *, int)
     = check_open_hook;

/* If this variable is set, it is called every time a protid structure
   is about to be destroyed. */
void (*trivfs_protid_destroy_hook) (struct trivfs_protid *) = pi_destroy_hook;

/* Read data from an IO object.  If offset is -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMOUNT.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred,
                  mach_port_t reply, mach_msg_type_name_t reply_type,
                  data_t *data, mach_msg_type_number_t *data_len,
                  loff_t offs, size_t amount)
{
  struct tunnel_device *tdev;
  struct sk_buff *skb;

  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  tdev = (struct tunnel_device *) cred->po->cntl->hook;

  pthread_mutex_lock (&tdev->lock);

  while (skb_queue_len(&tdev->xq) == 0)
    {
      if (cred->po->openmodes & O_NONBLOCK)
	{
	  pthread_mutex_unlock (&tdev->lock);
	  return EWOULDBLOCK;
	}

      tdev->read_blocked = 1;
      if (pthread_hurd_cond_wait_np (&tdev->wait, &tdev->lock))
        {
          pthread_mutex_unlock (&tdev->lock);
          return EINTR;
        }
      /* See term/users.c for possible race?  */
    }

  skb = skb_dequeue (&tdev->xq);
  assert_backtrace (skb);

  if (skb->len < amount)
    amount = skb->len;
  if (amount > 0)
    {
      /* Possibly allocate a new buffer. */
      if (*data_len < amount)
	{
	  *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    {
	      dev_kfree_skb (skb);
	      pthread_mutex_unlock (&tdev->lock);
	      return ENOMEM;
	    }
	}

      /* Copy the constant data into the buffer. */
      memcpy ((char *) *data, skb->data, amount);
    }
  *data_len = amount;
  dev_kfree_skb (skb);

  /* Set atime, see term/users.c */

  pthread_mutex_unlock (&tdev->lock);

  return 0;
}

/* Write data to an IO object.  If offset is -1, write at the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount successfully written is returned in amount.  A
   given user should not have more than one outstanding io_write on an
   object at a time; servers implement congestion control by delaying
   responses to io_write.  Servers may drop data (returning ENOBUFS)
   if they receive more than one write when not prepared for it.  */
error_t
trivfs_S_io_write (struct trivfs_protid *cred,
                   mach_port_t reply,
                   mach_msg_type_name_t replytype,
                   data_t data,
                   mach_msg_type_number_t datalen,
                   off_t offset,
                   mach_msg_type_number_t *amount)
{
  struct tunnel_device *tdev;
  struct sk_buff *skb;

  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_WRITE))
    return EBADF;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  tdev = (struct tunnel_device *) cred->po->cntl->hook;

  pthread_mutex_lock (&tdev->lock);

  pthread_mutex_lock (&net_bh_lock);
  skb = alloc_skb (datalen, GFP_ATOMIC);
  skb->len = datalen;
  skb->dev = &tdev->dev;

  memcpy (skb->data, data, datalen);

  /* Drop it on the queue. */
  skb->mac.raw = skb->data;
  skb->protocol = htons (ETH_P_IP);
  netif_rx (skb);
  pthread_mutex_unlock (&net_bh_lock);

  *amount = datalen;

  pthread_mutex_unlock (&tdev->lock);
  return 0;
}

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
kern_return_t
trivfs_S_io_readable (struct trivfs_protid *cred,
                      mach_port_t reply, mach_msg_type_name_t replytype,
                      mach_msg_type_number_t *amount)
{
  struct tunnel_device *tdev;
  struct sk_buff *skb;

  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  tdev = (struct tunnel_device *) cred->po->cntl->hook;

  pthread_mutex_lock (&tdev->lock);

  /* XXX: Now return the length of the next entry in the queue.
     From the BSD manual:
     The tunnel device, normally /dev/tunN, is exclusive-open (it cannot be
     opened if it is already open) and is restricted to the super-user.  A
     read() call will return an error (EHOSTDOWN) if the interface is not
     ``ready'' address has been set (which means that the control device is
     open and  the  interface's). Once the interface is ready, read() will re-
     turn a packet if one is available; if not, it will either block until one
     is or return EWOULDBLOCK, depending on whether non-blocking I/O has been
     enabled.  If the packet is longer than is allowed for in the buffer
     passed to read(), the extra data will be silently dropped.
  */

  skb = skb_dequeue(&tdev->xq);
  if (skb)
    {
      *amount = skb->len;
      skb_queue_head(&tdev->xq, skb);
    }
  else
    *amount = 0;

  pthread_mutex_unlock (&tdev->lock);

  return 0;
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
static error_t
io_select_common (struct trivfs_protid *cred,
		  mach_port_t reply,
		  mach_msg_type_name_t reply_type,
		  struct timespec *tsp, int *type)
{
  struct tunnel_device *tdev;
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  tdev = (struct tunnel_device *) cred->po->cntl->hook;

  /* We only deal with SELECT_READ and SELECT_WRITE here.  */
  *type &= SELECT_READ | SELECT_WRITE;

  if (*type == 0)
    return 0;

  pthread_mutex_lock (&tdev->lock);

  if (*type & SELECT_WRITE)
    {
      /* We are always writable.  */
      if (skb_queue_len (&tdev->xq) == 0)
	*type &= ~SELECT_READ;
      pthread_mutex_unlock (&tdev->lock);
      return 0;
    }

  while (1)
    {
      if (skb_queue_len (&tdev->xq) != 0)
	{
	  *type = SELECT_READ;
	  pthread_mutex_unlock (&tdev->lock);
	  return 0;
	}

      ports_interrupt_self_on_port_death (cred, reply);
      tdev->read_blocked = 1;
      err = pthread_hurd_cond_timedwait_np (&tdev->select_alert, &tdev->lock,
					    tsp);
      if (err)
        {
          *type = 0;
          pthread_mutex_unlock (&tdev->lock);

          if (err == ETIMEDOUT)
            err = 0;

          return err;
        }
    }
}

error_t
trivfs_S_io_select (struct trivfs_protid *cred,
                    mach_port_t reply,
                    mach_msg_type_name_t reply_type,
                    int *type)
{
  return io_select_common (cred, reply, reply_type, NULL, type);
}

error_t
trivfs_S_io_select_timeout (struct trivfs_protid *cred,
			    mach_port_t reply,
			    mach_msg_type_name_t reply_type,
			    struct timespec ts,
			    int *type)
{
  return io_select_common (cred, reply, reply_type, &ts, type);
}

/* Change current read/write offset */
error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
                  mach_port_t reply, mach_msg_type_name_t reply_type,
                  off_t offs, int whence, off_t *new_offs)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return ESPIPE;
}

/* Change the size of the file.  If the size increases, new blocks are
   zero-filled.  After successful return, it is safe to reference mapped
   areas of the file up to NEW_SIZE.  */
error_t
trivfs_S_file_set_size (struct trivfs_protid *cred,
                        mach_port_t reply, mach_msg_type_name_t reply_type,
                        off_t size)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return size == 0 ? 0 : EINVAL;
}

/* These four routines modify the O_APPEND, O_ASYNC, O_FSYNC, and
   O_NONBLOCK bits for the IO object. In addition, io_get_openmodes
   will tell you which of O_READ, O_WRITE, and O_EXEC the object can
   be used for.  The O_ASYNC bit affects icky async I/O; good async
   I/O is done through io_async which is orthogonal to these calls. */
error_t
trivfs_S_io_set_all_openmodes(struct trivfs_protid *cred,
                              mach_port_t reply,
                              mach_msg_type_name_t reply_type,
                              int mode)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return 0;
}

error_t
trivfs_S_io_set_some_openmodes (struct trivfs_protid *cred,
                                mach_port_t reply,
                                mach_msg_type_name_t reply_type,
                                int bits)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return 0;
}

error_t
trivfs_S_io_clear_some_openmodes (struct trivfs_protid *cred,
                                  mach_port_t reply,
                                  mach_msg_type_name_t reply_type,
                                  int bits)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return 0;
}

error_t
trivfs_S_io_get_owner (struct trivfs_protid *cred,
                       mach_port_t reply,
                       mach_msg_type_name_t reply_type,
                       pid_t *owner)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  *owner = 0;
  return 0;
}

error_t
trivfs_S_io_mod_owner (struct trivfs_protid *cred,
                       mach_port_t reply, mach_msg_type_name_t reply_type,
                       pid_t owner)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return EINVAL;
}

/* Return objects mapping the data underlying this memory object.  If
   the object can be read then memobjrd will be provided; if the
   object can be written then memobjwr will be provided.  For objects
   where read data and write data are the same, these objects will be
   equal, otherwise they will be disjoint.  Servers are permitted to
   implement io_map but not io_map_cntl.  Some objects do not provide
   mapping; they will set none of the ports and return an error.  Such
   objects can still be accessed by io_read and io_write.  */
error_t
trivfs_S_io_map (struct trivfs_protid *cred,
		 mach_port_t reply,
		 mach_msg_type_name_t replyPoly,
		 memory_object_t *rdobj,
		 mach_msg_type_name_t *rdtype,
		 memory_object_t *wrobj,
		 mach_msg_type_name_t *wrtype)
{
  if (!cred)
    return EOPNOTSUPP;

  if (cred->pi.class != tunnel_class)
    return EOPNOTSUPP;

  return EINVAL;
}
