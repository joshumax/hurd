/* 
   Copyright (C) 1994, 1995, 1996, 1997, 1999 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef __TRIVFS_MIG_DECLS_H__
#define __TRIVFS_MIG_DECLS_H__

#include "priv.h"

/* Vectors of dynamically allocated port classes/buckets.  */

/* Protid port classes.  */
extern struct port_class **trivfs_dynamic_protid_port_classes;
extern size_t trivfs_num_dynamic_protid_port_classes;

/* Control port classes.  */
extern struct port_class **trivfs_dynamic_control_port_classes;
extern size_t trivfs_num_dynamic_control_port_classes;

/* Port buckets.  */
extern struct port_bucket **trivfs_dynamic_port_buckets;
extern size_t trivfs_num_dynamic_port_buckets;

static inline struct trivfs_protid * __attribute__ ((unused))
trivfs_begin_using_protid (mach_port_t port)
{
  if (trivfs_protid_nportclasses + trivfs_num_dynamic_protid_port_classes > 1)
    {
      struct port_info *pi = ports_lookup_port (0, port, 0);
      int i;

      if (pi)
	{
	  for (i = 0; i < trivfs_protid_nportclasses; i++)
	    if (pi->class == trivfs_protid_portclasses[i])
	      return (struct trivfs_protid *) pi;
	  for (i = 0; i < trivfs_num_dynamic_protid_port_classes; i++)
	    if (pi->class == trivfs_dynamic_protid_port_classes[i])
	      return (struct trivfs_protid *) pi;
	  ports_port_deref (pi);
	}

      return 0;
    }
  else if (trivfs_protid_nportclasses == 1)
    return ports_lookup_port (0, port, trivfs_protid_portclasses[0]);
  else
    return ports_lookup_port (0, port, trivfs_dynamic_protid_port_classes[0]);
}

static inline struct trivfs_protid * __attribute__ ((unused))
trivfs_begin_using_protid_payload (unsigned long payload)
{
  if (trivfs_protid_nportclasses + trivfs_num_dynamic_protid_port_classes > 1)
    {
      struct port_info *pi = ports_lookup_payload (NULL, payload, NULL);
      int i;

      if (pi)
	{
	  for (i = 0; i < trivfs_protid_nportclasses; i++)
	    if (pi->class == trivfs_protid_portclasses[i])
	      return (struct trivfs_protid *) pi;
	  for (i = 0; i < trivfs_num_dynamic_protid_port_classes; i++)
	    if (pi->class == trivfs_dynamic_protid_port_classes[i])
	      return (struct trivfs_protid *) pi;
	  ports_port_deref (pi);
	}

      return NULL;
    }
  else if (trivfs_protid_nportclasses == 1)
    return ports_lookup_payload (NULL, payload,
				 trivfs_protid_portclasses[0]);
  else
    return ports_lookup_payload (NULL, payload,
				 trivfs_dynamic_protid_port_classes[0]);
}

static inline void __attribute__ ((unused))
trivfs_end_using_protid (struct trivfs_protid *cred)
{
  if (cred)
    ports_port_deref (cred);
}

static inline struct trivfs_control * __attribute__ ((unused))
trivfs_begin_using_control (mach_port_t port)
{
  if (trivfs_cntl_nportclasses + trivfs_num_dynamic_control_port_classes > 1)
    {
      struct port_info *pi = ports_lookup_port (0, port, 0);
      int i;

      if (pi)
	{
	  for (i = 0; i < trivfs_cntl_nportclasses; i++)
	    if (pi->class == trivfs_cntl_portclasses[i])
	      return (struct trivfs_control *) pi;
	  for (i = 0; i < trivfs_num_dynamic_control_port_classes; i++)
	    if (pi->class == trivfs_dynamic_control_port_classes[i])
	      return (struct trivfs_control *) pi;
	  ports_port_deref (pi);
	}

      return 0;
    }
  else if (trivfs_cntl_nportclasses == 1)
    return ports_lookup_port (0, port, trivfs_cntl_portclasses[0]);
  else
    return ports_lookup_port (0, port, trivfs_dynamic_control_port_classes[0]);
}

static inline struct trivfs_control * __attribute__ ((unused))
trivfs_begin_using_control_payload (unsigned long payload)
{
  if (trivfs_cntl_nportclasses + trivfs_num_dynamic_control_port_classes > 1)
    {
      struct port_info *pi = ports_lookup_payload (NULL, payload, NULL);
      int i;

      if (pi)
	{
	  for (i = 0; i < trivfs_cntl_nportclasses; i++)
	    if (pi->class == trivfs_cntl_portclasses[i])
	      return (struct trivfs_control *) pi;
	  for (i = 0; i < trivfs_num_dynamic_control_port_classes; i++)
	    if (pi->class == trivfs_dynamic_control_port_classes[i])
	      return (struct trivfs_control *) pi;
	  ports_port_deref (pi);
	}

      return NULL;
    }
  else if (trivfs_cntl_nportclasses == 1)
    return ports_lookup_payload (NULL, payload,
				 trivfs_cntl_portclasses[0]);
  else
    return ports_lookup_payload (NULL, payload,
				 trivfs_dynamic_control_port_classes[0]);
}

static inline void __attribute__ ((unused))
trivfs_end_using_control (struct trivfs_control *cred)
{
  if (cred)
    ports_port_deref (cred);
}

#endif /* __TRIVFS_MIG_DECLS_H__ */
