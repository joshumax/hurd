#
#   Copyright (C) 1994, 1995, 1996, 1999 Free Software Foundation
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

dir := init
makemode := server

# Set this to something other than `yes' to get the old Hurd init
# that has BSD-style rc and ttys functionality rolled in.
# The option for this old style may go away at some point in the future;
# if you want BSD style, best to use runttys (in daemons) instead.
split-init = yes

SRCS = init.c
OBJS = $(SRCS:.c=.o) \
       startupServer.o notifyServer.o startup_replyUser.o msgServer.o \
       startup_notifyUser.o
LCLHDRS = ttys.h
target = init
HURDLIBS = shouldbeinlibc

ifeq (yes,$(split-init))
init-CPPFLAGS = -DSPLIT_INIT
SRCS += stubs.c
else
SRCS += ttys.c
LDLIBS = -lutil
HURDLIBS += ports fshelp
endif

include ../Makeconf

mung_msg_S.h: msg_S.h
	sed 's/msg_server/mung_msg_server/' < $< > $@
