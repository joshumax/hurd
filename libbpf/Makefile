#
#   Copyright (C) 1994,95,96,97,98,99,2000,01,02,2005 Free Software Foundation, Inc.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := libbpf
makemode := library

libname = libbpf
SRCS= bpf_impl.c queue.c
LCLHDRS = bpf_impl.h queue.h
installhdrs = bpf_impl.h queue.h

MIGSTUBS =
OBJS = $(sort $(SRCS:.c=.o) $(MIGSTUBS))

LDLIBS = -lpthread

MIGCOMSFLAGS =

include ../Makeconf
