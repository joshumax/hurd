/* Unused memory object interface stubs
   Copyright (C) 1994 Free Software Foundation

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


error_t
_pager_seqnos_memory_object_copy (mach_port_t old,
			   mach_port_seqno_t seq,
			   memory_object_control_t old_ctl,
			   vm_offset_t off,
			   vm_size_t len,
			   mach_port_t new)
{
  printf ("m_o_copy called\n");
  return EOPNOTSUPP;
}

error_t
_pager_seqnos_memory_object_data_write (mach_port_t old,
				 mach_port_seqno_t seq,
				 mach_port_t ctl,
				 vm_offset_t off,
				 pointer_t data,
				 vm_size_t data_cnt)
{
  printf ("m_o_data_write called\n");
  return EOPNOTSUPP;
}

error_t
_pager_seqnos_memory_object_supply_completed (mach_port_t obj,
				       mach_port_seqno_t seq,
				       mach_port_t ctl,
				       vm_offset_t off,
				       vm_size_t len,
				       error_t result,
				       vm_offset_t err_off)
{
  printf ("m_o_supply_completed called\n");
  return EOPNOTSUPP;
}

error_t
_pager_seqnos_memory_object_change_completed (mach_port_t obj,
				       mach_port_seqno_t seq,
				       boolean_t maycache,
				       memory_object_copy_strategy_t strat)
{
  printf ("m_o_change_completed called\n");
  return EOPNOTSUPP;
}


