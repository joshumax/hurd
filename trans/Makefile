#
#   Copyright (C) 1994,95,96,97,99 Free Software Foundation, Inc.
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

targets = symlink firmlink ifsock magic null fifo new-fifo fwd crash \
	  password hello
SRCS = ifsock.c symlink.c magic.c null.c fifo.c new-fifo.c fwd.c \
	crash.c firmlink.c password.c hello.c
OBJS = $(SRCS:.c=.o) fsysServer.o ifsockServer.o passwordServer.o
HURDLIBS=ports trivfs threads fshelp pipe ihash shouldbeinlibc
password-LDLIBS = $(LIBCRYPT)

include ../Makeconf

symlink: fsysServer.o
ifsock: ifsockServer.o
crash: crashServer.o crash_replyUser.o msgServer.o
password: passwordServer.o

crash password: ../libports/libports.a ../libtrivfs/libtrivfs.a ../libthreads/libthreads.a ../libfshelp/libfshelp.a
fifo new-fifo: ../libpipe/libpipe.a
fwd new-fifo: ../libfshelp/libfshelp.a ../libports/libports.a
hello magic null ifsock fifo new-fifo firmlink: ../libtrivfs/libtrivfs.a ../libfshelp/libfshelp.a ../libports/libports.a ../libihash/libihash.a
$(targets): ../libshouldbeinlibc/libshouldbeinlibc.a
hello magic fwd null ifsock fifo new-fifo firmlink: ../libthreads/libthreads.a

$(targets): %: %.o
