# Copyright (C) 1993, 1994 Free Software Foundation, Inc.
# This file is part of the GNU Hurd.
# 
# The GNU Hurd is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The GNU Hurd is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with the GNU Hurd; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

dir := fstests

include ../Makeconf

DIST_FILES = fstests.c Makefile fdtests.c ChangeLog timertest.c

SRCS = fstests.c fdtests.c timertest.c

all: timertest

fstests: $(OBJS) $(libc)
	$(CC) $(CFLAGS) -o $@ $^

fdtests: $(OBJS) $(libc)
	$(CC) $(CFLAGS) -o $@ $^

timertest: $(OBJS) $(libc)
	$(CC) $(CFLAGS) -o $@ $^

install:  $(hurdroot) timertest fstests fdtests
	$(INSTALL_BIN) fdtests $(hurdroot)/bin/fdtests
	$(INSTALL_BIN) fstests $(hurdroot)/bin/fstests
	$(INSTALL_BIN) timertest $(hurdroot)/bin/timertest

clean: 
	rm -f fstests fdtests timertest *.o
relink:
	rm -f fstests fdtests timertest
