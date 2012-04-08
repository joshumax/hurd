# Makefile for pflocal
# 
#   Copyright (C) 1995, 1996, 2000, 2012 Free Software Foundation, Inc.
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

dir := pflocal
makemode := server

target = pflocal

SRCS = connq.c io.c pflocal.c socket.c pf.c sock.c sserver.c

MIGSTUBS = ioServer.o socketServer.o
OBJS = $(SRCS:.c=.o) $(MIGSTUBS)
HURDLIBS = pipe trivfs iohelp fshelp threads ports ihash shouldbeinlibc

MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h

include ../Makeconf
