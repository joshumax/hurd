# 
#   Copyright (C) 1993 Free Software Foundation
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

OBJS = get_conch.o handle_io_get_conch.o handle_io_release_conch.o \
	initialize_conch.o verify_user_conch.o

# Make sure we get local copies of the headers we install.
CPPFLAGS := -I. $(CPPFLAGS)

all: libioserver.a(__.SYMDEF)

libioserver.a(__.SYMDEF): $(OBJS)
	rm -f libioserver.a
	ar r $@ $^
	ranlib $@

$(hurdinst)/lib/libioserver.a: libioserver.a(__.SYMDEF)
	cp libioserver.a $@
	ranlib $@

$(headers)/libioserver.h: libioserver.h
	cp $< $@

install: $(hurdinst)/lib/libioserver.a $(headers)/libioserver.h

$(OBJS): libioserver.h

clean:
	rm libioserver.a *.o
