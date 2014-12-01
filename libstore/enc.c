/* Store wire encoding/decoding

   Copyright (C) 1996, 1997, 1999 Free Software Foundation, Inc.
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

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>
#include <sys/mman.h>

#include "store.h"

/* Initialize ENC.  The given vector and sizes will be used for the encoding
   if they are big enough (otherwise new ones will be automatically
   allocated).  */
void
store_enc_init (struct store_enc *enc,
		mach_port_t *ports, mach_msg_type_number_t num_ports,
		int *ints, mach_msg_type_number_t num_ints,
		off_t *offsets, mach_msg_type_number_t num_offsets,
		char *data, mach_msg_type_number_t data_len)
{
  memset (enc, 0, sizeof(*enc));

  enc->ports = enc->init_ports = ports;
  enc->num_ports = num_ports;
  enc->ints = enc->init_ints = ints;
  enc->num_ints = num_ints;
  enc->offsets = enc->init_offsets = offsets;
  enc->num_offsets = num_offsets;
  enc->data = enc->init_data = data;
  enc->data_len = data_len;
}

/* Deallocate storage used by the fields in ENC (but nothing is done with ENC
   itself).  */
void
store_enc_dealloc (struct store_enc *enc)
{
  if (enc->ports && enc->num_ports > 0)
    /* For ports, we must deallocate each port as well.  */
    {
      while (enc->cur_port < enc->num_ports)
	{
	  mach_port_t port = enc->ports[enc->cur_port++];
	  if (port != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), port);
	}

      if (enc->ports != enc->init_ports)
	munmap ((caddr_t) enc->ports, enc->num_ports * sizeof (*enc->ports));
    }

  if (enc->ints && enc->num_ints > 0 && enc->ints != enc->init_ints)
    munmap ((caddr_t) enc->ints, enc->num_ints * sizeof (*enc->ints));

  if (enc->offsets && enc->num_offsets > 0
      && enc->offsets != enc->init_offsets)
    munmap ((caddr_t) enc->offsets, enc->num_offsets * sizeof (*enc->offsets));

  if (enc->data && enc->data_len > 0 && enc->data != enc->init_data)
    munmap (enc->data, enc->data_len);

  /* For good measure...  */
  memset (enc, 0, sizeof(*enc));
}

/* Copy out the parameters from ENC into the given variables suitably for
   returning from a file_get_storage_info rpc, and deallocate ENC.  */
void
store_enc_return (struct store_enc *enc,
		  mach_port_t **ports, mach_msg_type_number_t *num_ports,
		  int **ints, mach_msg_type_number_t *num_ints,
		  off_t **offsets, mach_msg_type_number_t *num_offsets,
		  char **data, mach_msg_type_number_t *data_len)
{
  *ports = enc->ports;
  *num_ports = enc->num_ports;
  *ints = enc->ints;
  *num_ints = enc->num_ints;
  *offsets = enc->offsets;
  *num_offsets = enc->num_offsets;
  *data = enc->data;
  *data_len = enc->data_len;
}
