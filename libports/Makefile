# 
#   Copyright (C) 1995 Free Software Foundation, Inc.
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

dir := libports
makemode := library

SRCS = create-bucket.c create-class.c allocate-port.c intern-external-port.c \
 reallocate-port.c reallocate-from-external.c destroy-right.c \
 lookup-port.c port-ref.c port-ref-weak.c port-deref.c port-deref-weak.c \
 no-senders.c begin-rpc.c end-rpc.c manage-one-thread.c manage-multithread.c \
 inhibit-port-rpcs.c inhibit-class-rpcs.c inhibit-bucket-rpcs.c \
 inhibit-all-rpcs.c resume-port-rpcs.c resume-class-rpcs.c \
 resume-bucket-rpcs.c resume-all-rpcs.c interrupt-rpc.c \
 init.c complete-deallocate.c get-right.c count-class.c count-bucket.c \
 enable-class.c enable-bucket.c bucket-iterate.c stubs.c

OBJS = $(subst .c,.o,$(SRCS))
LCLHDRS = ports.h
REMHDRS = ../libihash/ihash.h
libname = libports
installhdrs = ports.h

include ../Makeconf

$(OBJS): ports.h
allocate-port.o complete-deallocate.o create-bucket.o destroy-right.o \
inhibit-all-rpcs.o inhibit-bucket-rpcs.o intern-external-port.o \
lookup-port.o reallocate-from-external.o reallocate-port.o: ../libihash/ihash.h
