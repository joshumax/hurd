# 
#   Copyright (C) 1994, 1995 Free Software Foundation
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

dir := trans
makemode := servers

targets = symlink ifsock magic null fifo new-fifo fwd
SRCS = ifsock.c symlink.c magic.c null.c fifo.c new-fifo.c fwd.c
OBJS = $(SRCS:.c=.o) fsysServer.o ifsockServer.o

all: $(targets)

symlink magic: fsysServer.o
ifsock: ifsockServer.o
fifo new-fifo: ../libpipe/libpipe.a
fwd new-fifo: ../libfshelp/libfshelp.a
null ifsock fifo new-fifo: ../libtrivfs/libtrivfs.a ../libports/libports.a ../libihash/libihash.a
$(targets): ../libshouldbeinlibc/libshouldbeinlibc.a
fwd null ifsock fifo new-fifo: ../libthreads/libthreads.a

include ../Makeconf
