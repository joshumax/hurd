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
#include <error.h>

#include "pfinet.h"

/* Pfinet options.  Used for both startup and runtime.  */
static const struct argp_option options[] = {
  {"interface", 'i', "DEVICE",  0,  "Network interface to use", 1},
  {0,0,0,0,"These apply to a given interface:", 2},
  {"address",   'a', "ADDRESS", 0, "Set the network address"},
  {"netmask",   'm', "MASK",    0, "Set the netmask"},
  {"gateway",   'g', "ADDRESS", 0, "Set the default gateway"},
  {"shutdown",  's', 0,       , 0, "Shut it down"}
  {0}
};

static const char args_doc[] = 0;
static const char doc[] = "Interface-specific options before the first \
interface specification apply to the first following interface; otherwise \
they apply to the previously specified interface."

/* Used to describe a particular interface during argument parsing.  */
struct parse_interface
{
  /* The network interface in question.  *?
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
  struct parse_interface *new = realloc (h->interfaces, h->num_interfaces + 1);
  if (! new)
    return ENOMEM;
  h->interfaces = new;
  h->num_interfaces++;
  h->curint = new + h->num_interfaces - 1;
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

  /* Print a parsing error message and (if exiting is turned off) return the
     error code ERR.  */
#define PERR(err, fmt, args...) \
  do { argp_error (state, fmt , ##args); return err; } while (0)

  /* Like PERR but for non-parsing errors.  */
#define FAIL(rerr, status, perr, fmt, args...) \
  do{ argp_failure (state, status, perr, fmt , ##args); return rerr; } while(0)

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
	    if (strcmp (in->device.pa_name, arg) == 0)
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
      if (!IN_CLASSA (h->curint->address)
	  && !IN_CLASSB (h->curint->address)
	  && !IN_CLASSC (h->curint->address))
	{
	  if (IN_MULTICAST (h->curint->address))
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
      /* Check for problems.  */
      if (! h->curint->device)
	/* No interface specified; see if there's a single extant one.  */
	{
	  err = find_device (0, &h->curint->device);
	  if (err)
	    FAIL (err, 13, 0, "No default interface");
	}
      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	if (in->address == INADDR_NONE && in->netmask != INADDR_NONE
	    && in->device->pa_addr != 0)
	  /* Specifying a netmask for an address-less interface is a no-no.  */
	  FAIL (EDESTADDREQ, 14, 0, "Cannot set netmask");

      /* Successfully finished parsing, return a result.  */
      for (in = h->interfaces; in < h->interfaces + h->num_interfaces; in++)
	{
	  struct device *dev = in->device;
	  if (in->address != INADDR_NONE || in->netmask != INADDR_NONE)
	    {
	      if (in->device->pa_addr != 0)
		/* There's already an addres, delete it.  */
		{
		  in->device->pa_addr = 0;
		  /* ....  */
		}

	      if (in->address != INADDR_NONE)
		in->device->pa_addr = in->address;

	      if (in->netmask != INADDR_NONE)
		in->device->pa_mask = in->netmask;
	      else
		{
		  if (IN_CLASSA (in->address))
		    in->device->pa_mask = IN_CLASSA_NET;
		  else if (IN_CLASSB (in->address))
		    in->device->pa_mask = IN_CLASSB_NET;
		  else if (IS_CLASS_C (in->address))
		    in->device->pa_mask = IN_CLASSC_NET;
		  else
		    abort ();
		}

	      in->device->family = AF_INET;
	      in->device->pa_brdaddr = in->device->pa_addr | ~in->device->pa_mask;

	      ip_rt_add (0, in->device->pa_addr & in->device->pa_mask,
			 in->device->pa_mask, 0, in->device, 0, 0);
	    }
	  if (in->gateway != INADDR_NONE)
	    ip_rt_add (RTF_GATEWAY, 0, 0, in->gateway, in->device, 0, 0);
	}
      /* Fall through to free hook.  */

    case ARGP_KEY_ERROR:
      /* Parsing error occured, free everything. */
      free (h->interfaces);
      free (h);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

struct argp
pfinet_argp = { options, parse_opt, args_doc, doc };

error_t
trivfs_S_fsys_set_options (struct trivfs_control *cntl,
			   mach_port_t reply, mach_msg_type_name_t reply_type,
			   char *data, mach_msg_type_number_t len,
			   int do_children)
{
  if (! cntl)
    return EOPNOTSUPP;
  else
    {
      int argc = argz_count (data, len);
      char **argv = alloca (sizeof (char *) * (argc + 1));

      argz_extract (data, len, argv);

      /* XXX should we should serialize requests here? */
      return
	argp_parse (&pfinet_argp, argv, argc,
		    ARGP_NO_ERRS | ARGP_NO_HELP | ARGP_PARSE_ARGV0,
		    0, 0);
    }
}

static error_t
get_opts (char **data, mach_msg_type_number_t *len)
{
  char *argz = 0;
  size_t argz_len = 0;

  error_t add_dev_opts (struct device *dev)
    {

    }

  err = enumerate_devices (add_dev_opts);

  if (! err)
    /* Put ARGZ into vm_alloced memory for the return trip.  */
    {
      if (*len < argz_len)
	err = vm_allocate (mach_task_self (), data, argz_len, 1);
      if (! err)
	{
	  if (argz_len)
	    bcopy (argz, *data, argz_len);
	  *len = argz_len;
	}
    }

  if (argz_len > 0)
    free (argz);

  return err;
}

error_t
trivfs_S_fsys_get_options (struct trivfs_control *fsys,
			   mach_port_t reply, mach_msg_type_name_t reply_type,
			   char **data, mach_msg_type_number_t *len)
{
  return fsys ? get_opts (data, len) : EOPNOTSUPP;
}

error_t
trivfs_S_file_get_fs_options (struct trivfs_protid *cred,
			      mach_port_t reply,
			      mach_msg_type_name_t reply_type,
			      char **data, mach_msg_type_number_t *len)
{
  return cred ? get_opts (data, len) : EOPNOTSUPP;
}
