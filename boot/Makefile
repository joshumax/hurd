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

dir := boot
makemode := utility

SRCS = boot.c tcattr.c sigvec.S
OBJS = boot.o notifyServer.o execServer.o deviceServer.o ioServer.o \
       io_replyUser.o device_replyUser.o sigvec.o \
       termServer.o tcattr.o
target = boot

include ../Makeconf

install: /usr/local/bin/boot

/usr/local/bin/boot:
	cp boot /usr/local/bin/boot

boot.o: notify_S.h exec_S.h device_S.h io_S.h device_reply_U.h io_reply_U.h \
	tioctl_S.h term_S.h

