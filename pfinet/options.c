/* Pfinet option parsing

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pfinet.h"

/* Our interface to the set of devices.  */
extern error_t find_device (char *name, struct device **device);
extern error_t enumerate_devices (error_t (*fun) (struct device *dev));

/* Pfinet options.  Used for both startup and runtime.  */
static const struct argp_option options[] =
{
  {"interface", 'i', "DEVICE",  0,  "Network interface to use", 1},
  {0,0,0,0,"These apply to a given interface:", 2},
  {"address",   'a', "ADDRESS", 0, "Set the network address"},
  {"netmask",   'm', "MASK",    0, "Set the netmask"},
  {"gateway",   'g', "ADDRESS", 0, "Set the default gateway"},
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

  /* New values to apply to it.  */
  unsigned long address, netmask, gateway;
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
  h->curint->gateway = INADDR_NONE;
  return 0;
}

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
#define ADDR(str, type)							      \
  ({ unsigned long addr = inet_addr (str);				      \
     if (addr == INADDR_NONE) PERR (EINVAL, "Malformed %s", type);	      \
     addr; })

  switch (opt)
    {
      struct parse_interface *in;

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
    case 'g':
      h->curint->gateway = ADDR (arg, "gateway"); break;

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

      /* Check for bogus option combinations.  */
      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	if (in->netmask != INADDR_NONE
	    && in->address == INADDR_NONE && in->device->pa_addr == 0)
	  /* Specifying a netmask for an address-less interface is a no-no.  */
	  FAIL (EDESTADDRREQ, 14, 0, "Cannot set netmask");

      /* Successfully finished parsing, return a result.  */
      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	{
	  struct device *dev = in->device;
	  if (in->address != INADDR_NONE || in->netmask != INADDR_NONE)
	    {
	      if (dev->pa_addr != 0)
		/* There's already an address, delete the old entry.  */
		ip_rt_del (dev->pa_addr & dev->pa_mask, dev);

	      if (in->address != INADDR_NONE)
		dev->pa_addr = in->address;

	      if (in->netmask != INADDR_NONE)
		dev->pa_mask = in->netmask;
	      else
		{
		  if (IN_CLASSA (ntohl (dev->pa_addr)))
		    dev->pa_mask = htonl (IN_CLASSA_NET);
		  else if (IN_CLASSB (ntohl (dev->pa_addr)))
		    dev->pa_mask = htonl (IN_CLASSB_NET);
		  else if (IN_CLASSC (ntohl (dev->pa_addr)))
		    dev->pa_mask = htonl (IN_CLASSC_NET);
		  else
		    abort ();
		}

	      dev->family = AF_INET;
	      dev->pa_brdaddr = dev->pa_addr | ~dev->pa_mask;

	      ip_rt_add (0, dev->pa_addr & dev->pa_mask, dev->pa_mask,
			 0, dev, 0, 0);
	    }
	  if (in->gateway != INADDR_NONE)
	    {
	      ip_rt_del (0, dev);
	      ip_rt_add (RTF_GATEWAY, 0, 0, in->gateway, dev, 0, 0);
	    }
	}
      /* Fall through to free hook.  */

    case ARGP_KEY_ERROR:
      /* Parsing error occured, free everything. */
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
trivfs_get_options (struct trivfs_control *fsys, char **argz, size_t *argz_len)
{
  error_t add_dev_opts (struct device *dev)
    {
      error_t err = 0;

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
      if (dev->pa_addr != 0)
        ADD_ADDR_OPT ("address", dev->pa_addr);
      if (dev->pa_mask != 0)
        ADD_ADDR_OPT ("netmask", dev->pa_mask);

      /* XXX how do we figure out the default gateway?  */
#undef ADD_OPT

      return err;
    }

  *argz = 0;
  *argz_len = 0;

  return enumerate_devices (add_dev_opts);
}
