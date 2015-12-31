#   Copyright (C) 2002, 2012 Free Software Foundation, Inc.
#   Written by Marcus Brinkmann.
#
#   This file is part of the GNU Hurd.
#
#   The GNU Hurd is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   The GNU Hurd is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.

dir := console
makemode := server

target = console

SRCS = console.c display.c pager.c input.c

MIGSTUBS = notifyServer.o tioctlServer.o fs_notifyUser.o

HURDLIBS = netfs fshelp iohelp pager ports ihash shouldbeinlibc
LDLIBS = -lpthread
OBJS = $(sort $(SRCS:.c=.o) $(MIGSTUBS))

MIGSFLAGS += -imacros $(srcdir)/mutations.h

# This is the old monolithic version of the console server.
#SRCS = main.c vcons.c focus.c vga-display.c vga.c dynafont.c bdf.c
#
#HURDLIBS = trivfs fshelp iohelp threads ports ihash shouldbeinlibc
#OBJS = $(subst .c,.o,$(SRCS)) tioctlServer.o
#
#MIGSFLAGS += -imacros $(srcdir)/mutations.h

include ../Makeconf
