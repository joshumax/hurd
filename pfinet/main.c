/*
   Copyright (C) 1995,96,97,99,2000 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <error.h>
#include <argp.h>
#include <hurd/startup.h>
#include <string.h>
#include <fcntl.h>

#include <linux/netdevice.h>
#include <linux/inet.h>

/* devinet.c */
extern error_t configure_device (struct device *dev,
                                 uint32_t addr, uint32_t netmask,
				 uint32_t peer, uint32_t broadcast);

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid;
int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ | O_WRITE;
struct port_class *trivfs_protid_portclasses[1];
int trivfs_protid_nportclasses = 1;
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_cntl_nportclasses = 1;

struct port_class *shutdown_notify_class;

/* Option parser.  */
extern struct argp pfinet_argp;

int
pfinet_demuxer (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  struct port_info *pi;
  extern int io_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int socket_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int startup_notify_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int pfinet_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int iioctl_server (mach_msg_header_t *, mach_msg_header_t *);

  /* We have several classes in one bucket, which need to be demuxed
     differently.  */
  pi = ports_lookup_port(pfinet_bucket, inp->msgh_local_port, socketport_class);
  
  if (pi)
    {
      ports_port_deref (pi);
      
      return (io_server (inp, outp)
	      || socket_server (inp, outp)
	      || pfinet_server (inp, outp)
	      || iioctl_server (inp, outp)
	      || trivfs_demuxer (inp, outp)
	      || startup_notify_server (inp, outp));
    }
  else
    return (socket_server (inp, outp)
	    || pfinet_server (inp, outp)
	    || iioctl_server (inp, outp)
	    || trivfs_demuxer (inp, outp)
	    || startup_notify_server (inp, outp));
}

/* The system is going down; destroy all the extant port rights.  That
   will cause net channels and such to close promptly.  */
error_t
S_startup_dosync (mach_port_t handle)
{
  struct port_info *inpi = ports_lookup_port (pfinet_bucket, handle,
					      shutdown_notify_class);

  if (!inpi)
    return EOPNOTSUPP;

  ports_class_iterate (socketport_class, ports_destroy_right);
  return 0;
}

void
sigterm_handler (int signo)
{
  ports_class_iterate (socketport_class, ports_destroy_right);
  sleep (10);
  signal (SIGTERM, SIG_DFL);
  raise (SIGTERM);
}

void
arrange_shutdown_notification ()
{
  error_t err;
  mach_port_t initport, notify;
  process_t procserver;
  struct port_info *pi;

  shutdown_notify_class = ports_create_class (0, 0);

  signal (SIGTERM, sigterm_handler);

  /* Arrange to get notified when the system goes down,
     but if we fail for some reason, just silently give up.  No big deal. */

  err = ports_create_port (shutdown_notify_class, pfinet_bucket,
			   sizeof (struct port_info), &pi);
  if (err)
    return;

  procserver = getproc ();
  if (!procserver)
    return;

  err = proc_getmsgport (procserver, 1, &initport);
  mach_port_deallocate (mach_task_self (), procserver);
  if (err)
    return;

  notify = ports_get_send_right (pi);
  ports_port_deref (pi);
  startup_request_notification (initport, notify,
				MACH_MSG_TYPE_MAKE_SEND,
				program_invocation_short_name);
  mach_port_deallocate (mach_task_self (), notify);
  mach_port_deallocate (mach_task_self (), initport);
}


/* Return an open device called NAME.  If NAME is 0, and there is a single
   active device, it is returned, otherwise an error.  */
error_t
find_device (char *name, struct device **device)
{
  struct device *dev = dev_base;

  /* Skip loopback interface. */
  assert (dev);
  dev = dev->next;

  if (!name)
    {
      if (dev)
	{
	  if (dev->next)
	    return EBUSY;	/* XXXACK */
	  else
	    {
	      *device = dev;
	      return 0;
	    }
	}
      else
	return ENXIO;		/* XXX */
    }

  for (; dev; dev = dev->next)
    if (strcmp (dev->name, name) == 0)
      {
	*device = dev;
	return 0;
      }

  if (strncmp(name, "tun", 3) == 0)
    setup_tunnel_device (name, device);
  else if (strncmp(name, "dummy", 5) == 0)
    setup_dummy_device (name, device);
  else
    setup_ethernet_device (name, device);

  /* Turn on device. */
  dev_open (*device);

  return 0;
}

/* Call FUN with each active device.  If a call to FUN returns a
   non-zero value, this function will return immediately.  Otherwise 0 is
   returned.  */
error_t
enumerate_devices (error_t (*fun) (struct device *dev))
{
  error_t err;
  struct device *dev = dev_base;

  /* Skip loopback device.  */
  assert (dev);
  dev = dev->next;

  for (; dev; dev = dev->next)
    {
      err = (*fun) (dev);
      if (err)
	return err;
    }

  return 0;
}

extern void sk_init (void), skb_init (void);
extern int net_dev_init (void);

int
main (int argc,
      char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct stat st;

  pfinet_bucket = ports_create_bucket ();
  trivfs_protid_portclasses[0] = ports_create_class (trivfs_clean_protid, 0);
  trivfs_cntl_portclasses[0] = ports_create_class (trivfs_clean_cntl, 0);
  addrport_class = ports_create_class (clean_addrport, 0);
  socketport_class = ports_create_class (clean_socketport, 0);
  trivfs_fsid = getpid ();
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &fsys_identity);

  /* Generic initialization */

  init_time ();
  ethernet_initialize ();
  cthread_detach (cthread_fork (net_bh_worker, 0));

  __mutex_lock (&global_lock);

  prepare_current (1);		/* Set up to call into Linux initialization. */

  sk_init ();
#ifdef SLAB_SKB
  skb_init ();
#endif
  inet_proto_init (0);

  /* This initializes the Linux network device layer, including
     initializing each device on the `dev_base' list.  For us,
     that means just loopback_dev, which will get fully initialized now.
     After this, we can use `register_netdevice' for new interfaces.  */
  net_dev_init ();

  /* ifconfig lo up 127.0.0.1 netmask 0xff000000 */
  configure_device (&loopback_dev,
		    htonl (INADDR_LOOPBACK), htonl (IN_CLASSA_NET),
		    htonl (INADDR_NONE), htonl (INADDR_NONE));

  __mutex_unlock (&global_lock);

  /* Parse options.  When successful, this configures the interfaces
     before returning; to do so, it will acquire the global_lock.
     (And when not sucessful, it never returns.)  */
  argp_parse (&pfinet_argp, argc, argv, 0,0,0);

  /* Ask init to tell us when the system is going down,
     so we can try to be friendly to our correspondents on the network.  */
  arrange_shutdown_notification ();

  /* Talk to parent and link us in.  */
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  err = trivfs_startup (bootstrap, 0,
			trivfs_cntl_portclasses[0], pfinet_bucket,
			trivfs_protid_portclasses[0], pfinet_bucket,
			&pfinetctl);

  if (err)
    error (1, err, "contacting parent");

  /* Initialize status from underlying node.  */
  err = io_stat (pfinetctl->underlying, &st);
  if (err)
    {
      /* We cannot stat the underlying node.  Fallback to the defaults.  */
      pfinet_owner = pfinet_group = 0;
      err = 0;
    }
  else
    {
      pfinet_owner = st.st_uid;
      pfinet_group = st.st_gid;
    }

  /* Launch */
  ports_manage_port_operations_multithread (pfinet_bucket,
					    pfinet_demuxer,
					    0, 0, 0);
  return 0;
}

void
trivfs_modify_stat (struct trivfs_protid *cred,
		    struct stat *st)
{
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  if (flags & FSYS_GOAWAY_FORCE)
    exit (0);
  else
    {
      /* Stop new requests.  */
      ports_inhibit_class_rpcs (trivfs_cntl_portclasses[0]);
      ports_inhibit_class_rpcs (trivfs_protid_portclasses[0]);
      ports_inhibit_class_rpcs (socketport_class);

      if (ports_count_class (socketport_class) != 0)
	{
	  /* We won't go away, so start things going again...  */
	  ports_enable_class (socketport_class);
	  ports_resume_class_rpcs (trivfs_cntl_portclasses[0]);
	  ports_resume_class_rpcs (trivfs_protid_portclasses[0]);

	  return EBUSY;
	}

      /* There are no sockets, so we can die without breaking anybody
	 too badly.  We don't let user ports on the /servers/socket/2
	 file keep us alive because those get cached in every process
	 that ever makes a PF_INET socket, libc copes with getting
	 MACH_SEND_INVALID_DEST and looking up the new translator.  */
      exit (0);
    }
}
