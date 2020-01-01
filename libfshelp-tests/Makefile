# Makefile libfshelp test cases
#
#   Copyright (C) 2001, 2015-2019 Free Software Foundation, Inc.
#
#   Written by Neal H Walfield <neal@cs.uml.edu>
#
#   This file is part of the GNU Hurd.
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
#   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.

dir := libfshelp-tests
makemode := utilities

targets = race locks fork test-flock test-lockf test-fcntl
SRCS = race.c locks.c fork.c test-flock.c test-lockf.c test-fcntl.c

MIGSTUBS = fsUser.o ioUser.o
OBJS = $(SRCS:.c=.o) $(MIGSTUBS)
HURDLIBS = fshelp ports ihash shouldbeinlibc
LDLIBS += -lpthread

race: race.o fsUser.o ioUser.o
fork: fork.o fsUser.o
locks: locks.o
test-flock: test-flock.o
test-lockf: test-lockf.o
test-fcntl: test-fcntl.o ../libfshelp/libfshelp.a

race locks: ../libfshelp/libfshelp.a ../libports/libports.a ../libihash/libihash.a ../libshouldbeinlibc/libshouldbeinlibc.a

include ../Makeconf
