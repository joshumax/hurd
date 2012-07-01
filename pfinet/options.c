/* Pfinet option parsing

   Copyright (C) 1996, 1997, 2000, 2001, 2006, 2007 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <string.h>
#include <hurd.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pfinet.h"

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/route.h>
#include <linux/rtnetlink.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

/* Our interface to the set of devices.  */
extern error_t find_device (char *name, struct device **device);
extern error_t enumerate_devices (error_t (*fun) (struct device *dev));

/* devinet.c */
extern error_t configure_device (struct device *dev, uint32_t addr,
				 uint32_t netmask, uint32_t peer,
				 uint32_t broadcast);
extern void inquire_device (struct device *dev, uint32_t *addr,
			    uint32_t *netmask, uint32_t *peer,
			    uint32_t *broadcast);

/* addrconf.c */
extern struct inet6_dev *ipv6_find_idev (struct device *dev);
extern int inet6_addr_add (int ifindex, struct in6_addr *pfx, int plen);
extern int inet6_addr_del (int ifindex, struct in6_addr *pfx, int plen);


/* Pfinet options.  Used for both startup and runtime.  */
static const struct argp_option options[] =
{
  {"interface", 'i', "DEVICE",  0,  "Network interface to use", 1},
  {0,0,0,0,"These apply to a given interface:", 2},
  {"address",   'a', "ADDRESS", 0, "Set the network address"},
  {"netmask",   'm', "MASK",    0, "Set the netmask"},
  {"peer",      'p', "ADDRESS", 0, "Set the peer address"},
  {"gateway",   'g', "ADDRESS", 0, "Set the default gateway"},
  {"ipv4",      '4', "NAME",    0, "Put active IPv4 translator on NAME"},
#ifdef CONFIG_IPV6  
  {"ipv6",      '6', "NAME",    0, "Put active IPv6 translator on NAME"},
  {"address6",  'A', "ADDR/LEN",0, "Set the global IPv6 address"},
  {"gateway6",  'G', "ADDRESS", 0, "Set the IPv6 default gateway"},
#endif
  {"shutdown",  's', 0,         0, "Shut it down"},
  {0}
};

static const char doc[] = "Interface-specific options before the first \
interface specification apply to the first following interface; otherwise \
they apply to the previously specified interface.";

/* Used to describe a particular interface during argument parsing.  */
struct parse_interface
{
  /* The network interface in question.  */
  struct device *device;

  /* New values to apply to it. (IPv4) */
  uint32_t address, netmask, peer, gateway;

#ifdef CONFIG_IPV6
  /* New IPv6 configuration to apply. */
  struct inet6_ifaddr address6;
  struct in6_addr gateway6;
#endif
};

/* Used to hold data during argument parsing.  */
struct parse_hook
{
  /* A list of specified interfaces and their corresponding options.  */
  struct parse_interface *interfaces;
  size_t num_interfaces;

  /* Interface to which options apply.  If the device field isn't filled in
     then it should be by the next --interface option.  */
  struct parse_interface *curint;
};

/* Adds an empty interface slot to H, and sets H's current interface to it, or
   returns an error. */
static error_t
parse_hook_add_interface (struct parse_hook *h)
{
  struct parse_interface *new =
    realloc (h->interfaces,
	     (h->num_interfaces + 1) * sizeof (struct parse_interface));
  if (! new)
    return ENOMEM;
  h->interfaces = new;
  h->num_interfaces++;
  h->curint = new + h->num_interfaces - 1;
  h->curint->device = 0;
  h->curint->address = INADDR_NONE;
  h->curint->netmask = INADDR_NONE;
  h->curint->peer = INADDR_NONE;
  h->curint->gateway = INADDR_NONE;

#ifdef CONFIG_IPV6
  memset (&h->curint->address6, 0, sizeof (struct inet6_ifaddr));
  memset (&h->curint->gateway6, 0, sizeof (struct in6_addr));
#endif

  return 0;
}

#ifdef CONFIG_IPV6
static struct rt6_info *
ipv6_get_dflt_router (void)
{
  struct in6_addr daddr = { 0 };

  struct fib6_node *fib = fib6_lookup
    (&ip6_routing_table, &daddr, NULL);
  return fib->leaf;
}
#endif /* CONFIG_IPV6 */


static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  error_t err = 0;
  struct parse_hook *h = state->hook;

  /* Return _ERR from this routine, and in the special case of OPT being
     ARGP_KEY_SUCCESS, remember to free H first.  */
#define RETURN(_err)								\
  do { if (opt == ARGP_KEY_SUCCESS)						\
	 { err = (_err); goto free_hook; }					\
       else									\
	 return _err; } while (0)

  /* Print a parsing error message and (if exiting is turned off) return the
     error code ERR.  */
#define PERR(err, fmt, args...)							\
  do { argp_error (state, fmt , ##args); RETURN (err); } while (0)

  /* Like PERR but for non-parsing errors.  */
#define FAIL(rerr, status, perr, fmt, args...) \
  do{ argp_failure (state, status, perr, fmt , ##args); RETURN (rerr); } while(0)

  /* Parse STR and return the corresponding  internet address.  If STR is not
     a valid internet address, signal an error mentioned TYPE.  */
#undef	ADDR
#define ADDR(str, type)							      \
  ({ unsigned long addr = inet_addr (str);				      \
     if (addr == INADDR_NONE) PERR (EINVAL, "Malformed %s", type);	      \
     addr; })

  switch (opt)
    {
      struct parse_interface *in;
      uint32_t gateway;
#ifdef CONFIG_IPV6
      struct parse_interface *gw6_in;
      char *ptr;
#endif

    case 'i':
      /* An interface.  */
      err = 0;
      if (h->curint->device)
	/* The current interface slot is not available.  */
	{
	  /* First see if a previously specified one is being re-specified.  */
	  for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	    if (strcmp (in->device->name, arg) == 0)
	      /* Re-use an old slot.  */
	      {
		h->curint = in;
		return 0;
	      }

	  /* Add a new interface entry.  */
	  err = parse_hook_add_interface (h);
	}
      in = h->curint;

      if (! err)
	err = find_device (arg, &in->device);
      if (err)
	FAIL (err, 10, err, "%s", arg);

      break;

    case 'a':
      h->curint->address = ADDR (arg, "address");
      if (!IN_CLASSA (ntohl (h->curint->address))
	  && !IN_CLASSB (ntohl (h->curint->address))
	  && !IN_CLASSC (ntohl (h->curint->address)))
	{
	  if (IN_MULTICAST (ntohl (h->curint->address)))
	    FAIL (EINVAL, 1, 0,
		  "%s: Cannot set interface address to multicast address",
		  arg);
	  else
	    FAIL (EINVAL, 1, 0,
		  "%s: Illegal or undefined network address", arg);
	}
      break;
    case 'm':
      h->curint->netmask = ADDR (arg, "netmask"); break;
    case 'p':
      h->curint->peer = ADDR (arg, "peer"); break;
    case 'g':
      h->curint->gateway = ADDR (arg, "gateway"); break;

    case '4':
      pfinet_bind (PORTCLASS_INET, arg);

      /* Install IPv6 port class on bootstrap port. */
      pfinet_bootstrap_portclass = PORTCLASS_INET6;
      break;

#ifdef CONFIG_IPV6
    case '6':
      pfinet_bind (PORTCLASS_INET6, arg);
      break;

    case 'A':
      if ((ptr = strchr (arg, '/'))) 
	{
	  h->curint->address6.prefix_len = atoi (ptr + 1);
	  if (h->curint->address6.prefix_len > 128) 
	    FAIL (EINVAL, 1, 0, "%s: The prefix-length is invalid", arg);

	  *ptr = 0;
	}
      else
	{
	  h->curint->address6.prefix_len = 64;
	  fprintf (stderr, "No prefix-length given, defaulting to %s/64.\n",
		   arg);
	}

      if (inet_pton (AF_INET6, arg, &h->curint->address6.addr) <= 0)
	PERR (EINVAL, "Malformed address");

      if (IN6_IS_ADDR_MULTICAST (&h->curint->address6.addr))
	FAIL (EINVAL, 1, 0, "%s: Cannot set interface address to "
	      "multicast address", arg);
      break;

    case 'G':
      if (inet_pton (AF_INET6, arg, &h->curint->gateway6) <= 0)
	PERR (EINVAL, "Malformed gateway");

      if (IN6_IS_ADDR_MULTICAST (&h->curint->gateway6))
	FAIL (EINVAL, 1, 0, "%s: Cannot set gateway to "
	      "multicast address", arg);
      break;
#endif /* CONFIG_IPV6 */

    case ARGP_KEY_INIT:
      /* Initialize our parsing state.  */
      h = malloc (sizeof (struct parse_hook));
      if (! h)
	FAIL (ENOMEM, 11, ENOMEM, "option parsing");

      h->interfaces = 0;
      h->num_interfaces = 0;
      err = parse_hook_add_interface (h);
      if (err)
	FAIL (err, 12, err, "option parsing");

      state->hook = h;
      break;

    case ARGP_KEY_SUCCESS:
      in = h->curint;
      if (! in->device)
	/* No specific interface specified; is that ok?  */
	if (in->address != INADDR_NONE || in->netmask != INADDR_NONE
	    || in->gateway != INADDR_NONE)
	  /* Some options were specified, so we need an interface.  See if
             there's a single extant interface to use as a default.  */
	  {
	    err = find_device (0, &in->device);
	    if (err)
	      FAIL (err, 13, 0, "No default interface");
	  }
#if 0				/* XXX what does this mean??? */
      /* Check for bogus option combinations.  */
      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	if (in->netmask != INADDR_NONE
	    && in->address == INADDR_NONE && in->device->pa_addr == 0)
	  /* Specifying a netmask for an address-less interface is a no-no.  */
	  FAIL (EDESTADDRREQ, 14, 0, "Cannot set netmask");
#endif

      gateway = INADDR_NONE;
#ifdef CONFIG_IPV6
      gw6_in = NULL;
#endif
      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	{
	  if (in->gateway != INADDR_NONE)
	    {
	      if (gateway != INADDR_NONE)
		FAIL (err, 15, 0, "Cannot have multiple default gateways");
	      gateway = in->gateway;
	      in->gateway = INADDR_NONE;
	    }

#ifdef CONFIG_IPV6
	  if (!IN6_IS_ADDR_UNSPECIFIED (&in->gateway6))
	    {
	      if (gw6_in != NULL)
		FAIL (err, 15, 0, "Cannot have multiple IPv6 "
		      "default gateways");
	      gw6_in = in;
	    }
#endif
	}
      /* Successfully finished parsing, return a result.  */

      __mutex_lock (&global_lock);

      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	{
#ifdef CONFIG_IPV6
	  struct inet6_dev *idev = NULL;
	  if (trivfs_protid_portclasses[PORTCLASS_INET6] != MACH_PORT_NULL
	      && in->device)
	    idev = ipv6_find_idev(in->device);
#endif

	  if (in->address != INADDR_NONE || in->netmask != INADDR_NONE)
	    {
	      err = configure_device (in->device, in->address, in->netmask,
				      in->peer, INADDR_NONE);
	      if (err)
		{
		  __mutex_unlock (&global_lock);
		  FAIL (err, 16, 0, "cannot configure interface");
		}
	    }

#ifdef CONFIG_IPV6
	  if (!idev)
	    continue;

	  /* First let's remove all non-local addresses. */
	  struct inet6_ifaddr *ifa = idev->addr_list;

	  while (ifa)
	    {
	      struct inet6_ifaddr *c_ifa = ifa;
	      ifa = ifa->if_next;

	      if (IN6_ARE_ADDR_EQUAL (&c_ifa->addr, &in->address6.addr))
		memset (&in->address6, 0, sizeof (struct inet6_ifaddr));

	      else if (!IN6_IS_ADDR_LINKLOCAL (&c_ifa->addr)
		       && !IN6_IS_ADDR_SITELOCAL (&c_ifa->addr))
		inet6_addr_del (in->device->ifindex, &c_ifa->addr,
				c_ifa->prefix_len);
	    }

	  if (!IN6_IS_ADDR_UNSPECIFIED (&in->address6.addr))
	    {
	      /* Now assign the new address */
	      inet6_addr_add (in->device->ifindex, &in->address6.addr,
			      in->address6.prefix_len);
	    }
#endif /* CONFIG_IPV6 */
	}

      /* Set the default gateway.  This code is cobbled together from what
	 the SIOCADDRT ioctl code does, and from the apparent functionality
	 of the "netlink" layer from perusing a little.  */
      {
	struct kern_rta rta;
	struct
	{
	  struct nlmsghdr nlh;
	  struct rtmsg rtm;
	} req;
	struct fib_table *tb;

	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = 0;
	req.nlh.nlmsg_len = NLMSG_LENGTH (sizeof req.rtm);

	bzero (&req.rtm, sizeof req.rtm);
	bzero (&rta, sizeof rta);
	req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
	req.rtm.rtm_type = RTN_UNICAST;
	req.rtm.rtm_protocol = RTPROT_STATIC;
	rta.rta_gw = &gateway;

	if (gateway == INADDR_NONE)
	  {
	    /* Delete any existing default route.  */
	    req.nlh.nlmsg_type = RTM_DELROUTE;
	    req.nlh.nlmsg_flags = 0;
	    tb = fib_get_table (req.rtm.rtm_table);
	    if (tb)
	      {
		err = - (*tb->tb_delete) (tb, &req.rtm, &rta, &req.nlh, 0);
		if (err && err != ESRCH)
		  {
		    __mutex_unlock (&global_lock);
		    FAIL (err, 17, 0, "cannot remove old default gateway");
		  }
		err = 0;
	      }
	  }
	else
	  {
	    /* Add a default route, replacing any existing one.  */
	    req.nlh.nlmsg_type = RTM_NEWROUTE;
	    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE;
	    tb = fib_new_table (req.rtm.rtm_table);
	    err = (!tb ? ENOBUFS
		   : - (*tb->tb_insert) (tb, &req.rtm, &rta, &req.nlh, 0));
	    if (err)
	      {
		__mutex_unlock (&global_lock);
	        FAIL (err, 17, 0, "cannot set default gateway");
	      }
	  }
      }

      /* Set IPv6 default router. */
#ifdef CONFIG_IPV6
      if (trivfs_protid_portclasses[PORTCLASS_INET6] != MACH_PORT_NULL)
	{
	  struct rt6_info *rt6i = ipv6_get_dflt_router ();

	  if (!gw6_in || rt6i->rt6i_dev != gw6_in->device
	      || !IN6_ARE_ADDR_EQUAL (&rt6i->rt6i_gateway, &gw6_in->gateway6))
	    {
	      rt6_purge_dflt_routers (0);
	      if (gw6_in)
		rt6_add_dflt_router (&gw6_in->gateway6, gw6_in->device);
	    }
	}
#endif       

      __mutex_unlock (&global_lock);

      /* Fall through to free hook.  */

    case ARGP_KEY_ERROR:
      /* Parsing error occurred, free everything. */
    free_hook:
      free (h->interfaces);
      free (h);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return err;
}

struct argp
pfinet_argp = { options, parse_opt, 0, doc };

struct argp *trivfs_runtime_argp = &pfinet_argp;

error_t
trivfs_append_args (struct trivfs_control *fsys, char **argz, size_t *argz_len)
{
  error_t add_dev_opts (struct device *dev)
    {
      error_t err = 0;
      uint32_t addr, mask, peer, broad;
      struct rt_key key = { 0 };
      struct fib_result res;

      inquire_device (dev, &addr, &mask, &peer, &broad);

#define ADD_OPT(fmt, args...)						\
  do { char buf[100];							\
       if (! err) {							\
         snprintf (buf, sizeof buf, fmt , ##args);			\
         err = argz_add (argz, argz_len, buf); } } while (0)
#define ADD_ADDR_OPT(name, addr)					\
  do { struct in_addr i;						\
       i.s_addr = (addr);						\
       ADD_OPT ("--%s=%s", name, inet_ntoa (i)); } while (0)

      ADD_OPT ("--interface=%s", dev->name);
      if (addr != INADDR_NONE)
        ADD_ADDR_OPT ("address", addr);
      if (mask != INADDR_NONE)
        ADD_ADDR_OPT ("netmask", mask);
      if (peer != addr)
	ADD_ADDR_OPT ("peer", peer);
      key.iif = dev->ifindex;
      if (! main_table->tb_lookup (main_table, &key, &res)) 
	ADD_ADDR_OPT ("gateway", FIB_RES_GW (res));

#undef ADD_ADDR_OPT

#ifdef CONFIG_IPV6
      struct inet6_dev *idev = NULL;

      if (trivfs_protid_portclasses[PORTCLASS_INET6] != MACH_PORT_NULL)
	idev = ipv6_find_idev(dev);

      if (idev)
	{
	  struct inet6_ifaddr *ifa = idev->addr_list;
	  static char addr_buf[INET6_ADDRSTRLEN];

	  /* Push all IPv6 addresses assigned to the interface. */
	  do 
	    {
	      inet_ntop (AF_INET6, &ifa->addr, addr_buf, INET6_ADDRSTRLEN);
	      ADD_OPT ("--address6=%s/%d", addr_buf, ifa->prefix_len);
	    }
	  while ((ifa = ifa->if_next));

	  /* Last not least push --gateway6 option. */
	  struct rt6_info *rt6i = ipv6_get_dflt_router ();
	  if(rt6i->rt6i_dev == dev) 
	    {
	      inet_ntop (AF_INET6, &rt6i->rt6i_gateway, addr_buf,
			 INET6_ADDRSTRLEN);
	      ADD_OPT ("--gateway6=%s", addr_buf);
	    }
	}
#endif /* CONFIG_IPV6 */

#undef ADD_OPT

      return err;
    }

  return enumerate_devices (add_dev_opts);
}
