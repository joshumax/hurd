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

# In GNU mig_support.c, sync.c and machine/lock.s are omitted; that work is
# all done in libc.
generic-sources = call.c cprocs.c cthread_data.c cthreads.c stack.c malloc.c
machine-sources = csw.S thread.c
sources = $(generic-sources) $(addprefix $(machine)/,$(machine-sources))
objects = $(addsuffix .o,$(basename $(notdir $(sources))))
headers = cthreads.h

SRCS = $(sources)
TAGSHDRS=$(headers)

DIST_FILES = $(generic-sources) $(headers) \
	     cthread_internals.h options.h \
	     ChangeLog Makefile

# In GNU machine/cthreads.h is omitted; that work is done in libc headers.
I386_DIST_FILES = $(addprefix i386/,$(machine-sources)) i386/asm.h

VPATH = $(machine)

all: libthreads.a 

libthreads.a: $(objects)
	rm -f libthreads.a
	$(AR) r $@ $^

$(hurdinst)/lib/libthreads.a: libthreads.a
	cp libthreads.a $@
	$(RANLIB) $@

$(hurdinst)/include/cthreads.h: cthreads.h
	cp $< $@

install: $(hurdinst)/lib/libthreads.a $(hurdinst)/include/cthreads.h

$(objects): cthreads.h cthread_internals.h options.h \
	 i386/asm.h

clean:
	rm -f libthreads.a *.o

lndist: lndist-i386-files

lndist-i386-files: $(hurdsource)/hurd-snap/$(dir)/i386
	ln $(I386_DIST_FILES) $(hurdsource)/hurd-snap/$(dir)/i386

$(hurdsource)/hurd-snap/$(dir)/i386:
	mkdir $(hurdsource)/hurd-snap/$(dir)/i386

