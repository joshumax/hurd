# 
#   Copyright (C) 1994, 1995 Free Software Foundation, Inc.
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

OBJS = $(addsuffix .o,$(basename $(notdir $(SRCS) $(I386SRCS))))

OTHERTAGS = $(I386SRCS) $(I386HDRS)

# We need this to find <cthreads.h> in the source directory.
CPPFLAGS = -I. -I$(srcdir)/libthreads

libname = libthreads

# Don't put cthreads.h in installhdrs or else it will get installed into
# $(hurdinst)/include/hurd instead of $(hurdinst)/include.

VPATH = $(machine)

include ../Makeconf

# This is cheating, but it's a lot easier than writing PIC assembly
# code.
csw_pic.o: csw.o
	cp $< $@

install: install-cthreads.h
install-cthreads.h: cthreads.h
	$(INSTALL_DATA) cthreads.h $(includedir)/cthreads.h

lndist: lndist-i386-files

lndist-i386-files: $(srcdir)/hurd-snap/$(dir)/i386
	ln $(I386SRCS) $(I386HDRS) $(srcdir)/hurd-snap/$(dir)/i386

$(srcdir)/hurd-snap/$(dir)/i386:
	mkdir $(srcdir)/hurd-snap/$(dir)/i386
