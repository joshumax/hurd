/* Helper function for io_identity
   Copyright (C) 1996 Free Software Foundation, Inc.
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


#include <fshelp.h>
#include <hurd/ports.h>
#include <assert.h>

static struct port_class *idclass = 0;
static struct mutex idlock = MUTEX_INITIALIZER;

struct idspec 
{
  struct port_info pi;
  ino_t fileno;
};

static void
id_initialize ()
{
  assert (!idclass);
  idclass = ports_create_class (0, 0);
}

error_t
fshelp_get_identity (struct port_bucket *bucket,
		     ino_t fileno,
		     mach_port_t *pt)
{
  struct idspec *i;
  error_t err = 0;

  error_t check_port (void *arg)
    {
      struct idspec *i = arg;
      if (i->pi.class == idclass && i->fileno == fileno)
	{
	  *pt = ports_get_right (i);
	  return 1;
	}
      else
	return 0;
    }

  mutex_lock (&idlock);
  if (!idclass)
    id_initialize ();

  *pt = MACH_PORT_NULL;
  
  ports_bucket_iterate (bucket, check_port);

  if (*pt != MACH_PORT_NULL)
    {
      mutex_unlock (&idlock);
      return 0;
    }
  
  err = ports_create_port (idclass, bucket, sizeof (struct idspec), &i);
  if (err)
    {
      mutex_unlock (&idlock);
      return err;
    }
  
  *pt = ports_get_right (i);
  ports_port_deref (i);
  return 0;
}

  
