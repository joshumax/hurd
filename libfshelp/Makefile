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
	kill-trans.c trans-iter.c lock-acquire.c lock-init.c 

OBJS = fsys-startup.o init-trans.o trans-start.o transboot-clean.o \
	kill-trans.o trans-iter.o lock-acquire.o lock-init.o 

DIST_FILES = $(SRCS) Makefile trans.h fshelp.h locks.h

all: libfshelp.a

libfshelp.a: $(OBJS)
	rm -f libfshelp.a
	ar r $@ $^

$(hurdinst)/lib/libfshelp.a: libfshelp.a
	cp libfshelp.a $@
	ranlib $@

$(headers)/hurd/fshelp.h: fshelp.h
	cp $< $@

$(OBJS): fshelp.h $(headers)/hurd/ports.h
fsys-startup.o init-trans.o trans-start.o transboot-clean.o: trans.h
kill-trans.o trans-iter.o: trans.h

lock-acquire.o lock-init.o: locks.h


.PHONY: clean install

clean:
	rm -f libfshelp.a *.o

install: $(hurdinst)/lib/libfshelp.a $(headers)/hurd/fshelp.h
