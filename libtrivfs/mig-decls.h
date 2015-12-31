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
  struct port_info *pi = ports_lookup_port (0, port, 0);

  if (pi)
    {
      size_t i;
      for (i = 0; i < trivfs_num_dynamic_protid_port_classes; i++)
	if (pi->class == trivfs_dynamic_protid_port_classes[i])
	  return (struct trivfs_protid *) pi;
      ports_port_deref (pi);
    }

  return NULL;
}

static inline struct trivfs_protid * __attribute__ ((unused))
trivfs_begin_using_protid_payload (unsigned long payload)
{
  struct port_info *pi = ports_lookup_payload (NULL, payload, NULL);

  if (pi)
    {
      size_t i;
      for (i = 0; i < trivfs_num_dynamic_protid_port_classes; i++)
	if (pi->class == trivfs_dynamic_protid_port_classes[i])
	  return (struct trivfs_protid *) pi;
      ports_port_deref (pi);
    }

  return NULL;
}

static inline void __attribute__ ((unused))
trivfs_end_using_protid (struct trivfs_protid *cred)
{
  if (cred)
    ports_port_deref (cred);
}

static inline mach_port_t __attribute__ ((unused))
trivfs_convert_to_port(struct trivfs_protid *protid)
{
  return protid->pi.port_right;
}

static inline struct trivfs_control * __attribute__ ((unused))
trivfs_begin_using_control (mach_port_t port)
{
  struct port_info *pi = ports_lookup_port (0, port, 0);

  if (pi)
    {
      size_t i;
      for (i = 0; i < trivfs_num_dynamic_control_port_classes; i++)
	if (pi->class == trivfs_dynamic_control_port_classes[i])
	  return (struct trivfs_control *) pi;
      ports_port_deref (pi);
    }

  return NULL;
}

static inline struct trivfs_control * __attribute__ ((unused))
trivfs_begin_using_control_payload (unsigned long payload)
{
  struct port_info *pi = ports_lookup_payload (NULL, payload, NULL);

  if (pi)
    {
      size_t i;
      for (i = 0; i < trivfs_num_dynamic_control_port_classes; i++)
	if (pi->class == trivfs_dynamic_control_port_classes[i])
	  return (struct trivfs_control *) pi;
      ports_port_deref (pi);
    }

  return NULL;
}

static inline void __attribute__ ((unused))
trivfs_end_using_control (struct trivfs_control *cred)
{
  if (cred)
    ports_port_deref (cred);
}

#endif /* __TRIVFS_MIG_DECLS_H__ */
