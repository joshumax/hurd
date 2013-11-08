# 
#   Copyright (C) 1995, 1996 Free Software Foundation
#   Written by Michael I. Bushnell.
#
#   This file is part of the GNU Hurd.
#
#   The GNU Hurd is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   The GNU Hurd is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := defpager
makemode := misc

SRCS = defpager.c

subst-functions=__syscall_vm_allocate syscall_vm_allocate \
		__vm_allocate_rpc vm_allocate_rpc \
		__syscall_vm_map syscall_vm_map \
		__vm_map_rpc vm_map_rpc
comma=,
LDFLAGS=-Wl,$(subst :,$(comma),$(strip $(subst-functions)))

include ../Makeconf
