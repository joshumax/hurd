# 
#   Copyright (C) 1996 Free Software Foundation, Inc.
#   Written by Michael I. Bushnell, p/BSG.
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
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.

dir := libmom
makemode := library

libname = libmom

SRCS = add-ref.c allocate-address.c allocate-memory.c copy-ref.c \
	deallocate-memory.c drop-ref.c fetch-mach-port.c hash-ref.c \
	mach-port-set.c make-memory-readonly.c make-memory-readwrite.c \
	memory-init.c ref-destroy.c refs-identical.c reserve-memory.c \
	reserve-memory-anywhere.c unreserve-memory.c unuse-memory.c \
	wire-memory.c

LCLHDRS = mom.h mom-kernep.h priv.h
installhdrs = mom.h mom-kerndep.h
installhdrsubdir = .

OBJS = $(SRCS:.c=.o)

include ../Makeconf


