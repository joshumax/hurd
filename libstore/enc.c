/* Store wire encoding/decoding

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

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <string.h>

#include "store.h"

/* Initialize ENC.  The given vector and sizes will be used for the encoding
   if they are big enough (otherwise new ones will be automatically
   allocated).  */
void
store_enc_init (struct store_enc *enc,
		mach_port_t *ports, mach_msg_type_number_t ports_len,
		int *ints, mach_msg_type_number_t ints_len,
		off_t *offsets, mach_msg_type_number_t offsets_len,
		char *data, mach_msg_type_number_t data_len)
{
  bzero (enc, sizeof (*enc));

  enc->ports = enc->init_ports = ports;
  enc->ports_len = ports_len;
  enc->ints = enc->init_ints = ints;
  enc->ints_len = ints_len;
  enc->offsets = enc->init_offsets = offsets;
  enc->offsets_len = offsets_len;
  enc->data = enc->init_data = data;
  enc->data_len = data_len;
}

/* Deallocate storage used by the fields in ENC (but nothing is done with ENC
   itself).  */
void
store_enc_dealloc (struct store_enc *enc)
{
  if (enc->ports && enc->ports_len > 0)
    /* For ports, we must deallocate each port as well.  */
    {
      while (enc->cur_port < enc->ports_len)
	{
	  mach_port_t port = enc->ports[enc->cur_port++];
	  if (port != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), port);
	}

      if (enc->ports != enc->init_ports)
	vm_deallocate (mach_task_self (),
		       (vm_address_t)enc->ports,
		       enc->ports_len * sizeof (*enc->ports));
    }

  if (enc->ints && enc->ints_len > 0 && enc->ints != enc->init_ints)
    vm_deallocate (mach_task_self (),
		   (vm_address_t)enc->ints,
		   enc->ints_len * sizeof (*enc->ints));

  if (enc->offsets && enc->offsets_len > 0
      && enc->offsets != enc->init_offsets)
    vm_deallocate (mach_task_self (),
		   (vm_address_t)enc->offsets,
		   enc->offsets_len * sizeof (*enc->offsets));

  if (enc->data && enc->data_len > 0 && enc->data != enc->init_data)
    vm_deallocate (mach_task_self (),
		   (vm_address_t)enc->data, enc->data_len);

  /* For good measure...  */
  bzero (enc, sizeof (*enc));
}
