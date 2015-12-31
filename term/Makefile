#   Copyright (C) 1995,96,97,99, 2000, 2002, 2012 Free Software Foundation,
#   Inc.
#
#   Written by Michael I. Bushnell, p/BSG.
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

dir := term
makemode := server

target = term
SRCS = devio.c munge.c users.c main.c ptyio.c hurdio.c xinl.c

HURDLIBS = trivfs fshelp iohelp ports ihash shouldbeinlibc
LDLIBS = -lpthread
OBJS = $(subst .c,.o,$(SRCS)) termServer.o device_replyServer.o tioctlServer.o ourmsgUser.o

include ../Makeconf

device_replyServer-CPPFLAGS = -DTypeCheck=0 -Wno-unused # XXX

device_reply-MIGSFLAGS=\
    "-DMACH_PAYLOAD_TO_PORT=ports_payload_get_name" \
    "-DDEVICE_IMPORTS=import \"$(srcdir)/../libports/ports.h\";"
tioctl-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h
term-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h
