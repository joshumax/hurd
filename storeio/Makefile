# Makefile for storeio
# 
#   Copyright (C) 1995, 1996, 1997, 2000, 2012 Free Software Foundation, Inc.
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

dir := storeio
makemode := server

target = storeio
SRCS = dev.c storeio.c open.c pager.c io.c

OBJS = $(SRCS:.c=.o)
HURDLIBS = trivfs pager fshelp iohelp store ports ihash shouldbeinlibc
LDLIBS = -lpthread

include ../Makeconf
