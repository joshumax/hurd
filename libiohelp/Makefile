# 
#   Copyright (C) 1993, 1994 Free Software Foundation
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

dir := libioserver

include ../Makeconf

SRCS = get_conch.c handle_io_get_conch.c handle_io_release_conch.c \
	initialize_conch.c verify_user_conch.c
OBJS = get_conch.o handle_io_get_conch.o handle_io_release_conch.o \
	initialize_conch.o verify_user_conch.o

DIST_FILES = $(SRCS) Makefile ioserver.h

all: libioserver.a

libioserver.a: $(OBJS)
	rm -f libioserver.a
	$(AR) r $@ $^

$(hurdinst)/lib/libioserver.a: libioserver.a
	cp libioserver.a $@
	$(RANLIB) $@

$(headers)/hurd/ioserver.h: ioserver.h
	cp $< $@

install: $(hurdinst)/lib/libioserver.a $(headers)/hurd/ioserver.h

$(OBJS): ioserver.h

clean:
	rm -f libioserver.a *.o
