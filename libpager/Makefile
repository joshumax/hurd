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

dir := libpager

include ../Makeconf

COBJS = data-request.o data-return.o data-unlock.o get-port.o inhibit-term.o \
	get-port.o inhibit-term.o lock-completed.o lock-object.o mark-error.o \
	no-senders.o object-init.o object-terminate.o pagemap.o pager-create.o\
	pager-flush.o pager-shutdown.o pager-sync.o reference.o stubs.o \
	seqnos.o demuxer.o

OBJS = $(COBJS) memory_objectServer.o

SRCS = $(subst .o,.c,$(COBJS))

DIST_FILES = $(SRCS) Makefile pager.h priv.h

all: libpager.a(__.SYMDEF)

libpager.a(__.SYMDEF): $(OBJS)
	rm -f libpager.a
	ar r $@ $^
	ranlib $@

$(hurdinst)/lib/libpager.a: libpager.a(__.SYMDEF)
	cp libpager.a $@
	ranlib $@

$(headers)/hurd/pager.h: pager.h
	cp pager.h $@

install: $(hurdinst)/lib/libpager.a $(headers)/hurd/pager.h

$(OBJS): pager.h priv.h $(headers)/hurd/ports.h

data-request.o data-return.o data-unlock.o lock-completed.o: memory_object.h
stubs.o object-init.o object-terminate.o: memory_object.h

clean:
	rm libpager.a *.o

# MiG gook
memory_object.h memory_objectServer.c:
	$(CPP) $(CPPFLAGS) -DSEQNOS $(headers)/mach/memory_object.defs \
	| $(MIGCOM) -prefix _pager_ -sheader memory_object.h \
		-header /dev/null -user /dev/null
