# 
#   Copyright (C) 1994 Free Software Foundation
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

# This is currently i386 specific. XXX 

dir := libthreads
makemode := library

# In GNU mig_support.c, sync.c and machine/lock.s are omitted; that work is
# all done in libc.
SRCS := call.c cprocs.c cthread_data.c cthreads.c stack.c malloc.c
I386SRCS := i386/csw.S i386/thread.c

# In GNU machine/cthreads.h is omitted; that work is done in libc headers.
LCLHDRS := cthread_internals.h options.h cthreads.h
I386HDRS := i386/asm.h

OBJS = $(addsuffix .o,$(basename $(notdir $(SRCS) $(I386SRCS))))

OTHERTAGS = $(I386SRCS) $(I386HDRS)

libname = libthreads
installhdrs = cthreads.h

VPATH = $(machine)

include ../Makeconf

$(objects): cthreads.h cthread_internals.h options.h i386/asm.h

lndist: lndist-i386-files

lndist-i386-files: $(hurdsource)/hurd-snap/$(dir)/i386
	ln $(I386SRCS) $(I386HDRS) $(hurdsource)/hurd-snap/$(dir)/i386

$(hurdsource)/hurd-snap/$(dir)/i386:
	mkdir $(hurdsource)/hurd-snap/$(dir)/i386

