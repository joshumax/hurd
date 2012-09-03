#   Copyright (C) 1993, 1994, 1995, 1996, 1998, 2002, 2008, 2012 Free Software
#   Foundation, Inc.
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

dir := libiohelp
makemode := library

SRCS = get_conch.c handle_io_get_conch.c handle_io_release_conch.c \
	initialize_conch.c verify_user_conch.c iouser-create.c \
	iouser-dup.c iouser-reauth.c iouser-free.c iouser-restrict.c \
	shared.c return-buffer.c
OBJS = $(SRCS:.c=.o)
HURDLIBS = shouldbeinlibc
LDLIBS += -lpthread
libname = libiohelp
installhdrs = iohelp.h

include ../Makeconf
