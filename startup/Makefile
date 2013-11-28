#
#   Copyright (C) 1994,95,96,99,2001 Free Software Foundation, Inc.
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

dir := startup
makemode := server

SRCS = startup.c
OBJS = $(SRCS:.c=.o) \
       startupServer.o notifyServer.o startup_replyUser.o msgServer.o \
       startup_notifyUser.o fsysServer.o
target = startup
HURDLIBS = shouldbeinlibc

# startup does not use libports.  Disable the default payload to port
# conversion.
MIGSFLAGS="-DHURD_DEFAULT_PAYLOAD_TO_PORT=1"

include ../Makeconf

mung_msg_S.h: msg_S.h
	sed 's/msg_server/mung_msg_server/' < $< > $@
