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

include ../Makeconf

SRCS = call.c cprocs.c cthread_data.c cthreads.c malloc.c mig_support.c \
	stack.c sync.c machine/csw.s machine/lock.s machine/thread.c
OBJS = call.o cprocs.o cthread_data.o cthreads.o malloc.o mig_support.o \
	stack.o sync.o csw.o lock.o thread.o

DIST_FILES = $(SRCS) Makefile cthread_internal.h cthreads.h options.h \
	machine/asm.h machine/cthreads.h 

all: libthreads.a

libthreads.a: $(OBJS)
	rm -f libthreads.a
	$(AR) r $@ $^

$(hurdinst)/lib/libthreads.a: libthreads.a
	cp libthreads.a $@
	$(RANLIB) $@

$(headers)/cthreads.h: cthreads.h
	cp $< $@

$(headers)/machine/cthreads.h: machine/cthreads.h
	cp $< $@

install: $(hurdinst)/lib/libthreads.a \
	$(headers)/cthreads.h $(headers)/machine/cthreads.h

$(OBJS): cthreads.h cthread_internal.h options.h \
	machine/asm.h machine/cthreads.h

clean:
	rm -f libthreads.a *.o
