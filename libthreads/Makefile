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

dir := libthreads

# This is currently i386 specific. XXX 

include ../Makeconf

GEN_SRCS = call.c cprocs.c cthread_data.c cthreads.c malloc.c mig_support.c \
	  stack.c sync.c
I386_SRCS =  i386/csw.s i386/lock.s i386/thread.c
SRCS = $(GEN_SRCS) $(I386_SRCS)
OBJS = call.o cprocs.o cthread_data.o cthreads.o malloc.o mig_support.o \
	stack.o sync.o csw.o lock.o thread.o

DIST_FILES = $(GEN_SRCS) Makefile cthread_internals.h cthreads.h options.h\
		 ChangeLog
I386_DIST_FILES = $(I386_SRCS) i386/asm.h i386/cthreads.h

VPATH = .:i386

all: libthreads.a

libthreads.a: $(OBJS)
	rm -f libthreads.a
	$(AR) r $@ $^

$(hurdinst)/lib/libthreads.a: libthreads.a
	cp libthreads.a $@
	$(RANLIB) $@

$(headers)/cthreads.h: cthreads.h
	cp $< $@

$(headers)/i386/cthreads.h: i386/cthreads.h
	cp $< $@

install: $(hurdinst)/lib/libthreads.a \
	$(headers)/cthreads.h $(headers)/i386/cthreads.h

$(OBJS): cthreads.h cthread_internals.h options.h \
	i386/asm.h i386/cthreads.h

clean:
	rm -f libthreads.a *.o

lndist: lndist-i386-files

lndist-i386-files: $(hurdsource)/hurd-snap/$(dir)/i386
	ln $(I386_DIST_FILES) $(hurdsource)/hurd-snap/$(dir)/i386

$(hurdsource)/hurd-snap/$(dir)/i386:
	mkdir $(hurdsource)/hurd-snap/$(dir)/i386
