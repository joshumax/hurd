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

dir := libfshelp

include ../Makeconf

SRCS = fsys-startup.c init-trans.c trans-start.c transboot-clean.c \
	kill-trans.c trans-iter.c 

OBJS = fsys-startup.o init-trans.o trans-start.o transboot-clean.o \
	kill-trans.o trans-iter.o

DIST_FILES = $(SRCS) Makefile trans.h fshelp.h

all: libfshelp.a(__.SYMDEF)

libfshelp.a(__.SYMDEF): $(OBJS)
	rm -f libfshelp.a
	ar r $@ $^
	ranlib $@

$(hurdinst)/lib/libfshelp.a: libfshelp.a(__.SYMDEF)
	cp libioserver.a $@
	ranlib $@

$(headers)/hurd/fshelp.h: fshelp.h
	cp $< $@

$(OBJS): fshelp.h
fsys-startup.o init-trans.o trans-start.o transboot-clean.o: trans.h
kill-trans.o trans-iter.o: trans.h

clean:
	rm libfshelp.a *.o
