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

dir := nfs
makemode := server

target = nfs
LCLHDRS = nfs.h mount.h nfs-spec.h
SRCS = ops.c rpc.c mount.c cred.c nfs.c cache.c consts.c main.c 
OBJS = $(subst .c,.o,$(SRCS))

nfs: $(OBJS) ../libports/libports.a ../libnetfs/libnetfs.a ../libfshelp/libfshelp.a ../libthreads/libthreads.a

include ../Makeconf


