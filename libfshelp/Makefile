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

SRCS = handle-startup.c init-trans.c start-trans.c transboot-clean.c \
	kill-trans.c trans-iter.c lock-acquire.c lock-init.c trans-cntl.c

OBJS = handle-startup.o init-trans.o start-trans.o transboot-clean.o \
	kill-trans.o trans-iter.o lock-acquire.o lock-init.o trans-cntl.o

TAGSHDRS = trans.h fshelp.h locks.h

DIST_FILES = $(SRCS) Makefile trans.h fshelp.h locks.h ChangeLog

all: libfshelp.a

libfshelp.a: $(OBJS)
	rm -f libfshelp.a
	$(AR) r $@ $^

$(hurdinst)/lib/libfshelp.a: libfshelp.a
	cp libfshelp.a $@
	$(RANLIB) $@

$(includedir)/hurd/fshelp.h: fshelp.h
	cp $< $@

$(OBJS): fshelp.h $(includedir)/hurd/ports.h
handle-startup.o init-trans.o start-trans.o transboot-clean.o: trans.h
trans-cntl.o kill-trans.o trans-iter.o: trans.h

lock-acquire.o lock-init.o: locks.h


.PHONY: clean install

clean:
	rm -f libfshelp.a *.o

install: $(hurdinst)/lib/libfshelp.a $(includedir)/hurd/fshelp.h
