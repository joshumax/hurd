# 
#   Copyright (C) 1995 Free Software Foundation, Inc.
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
SRCS = devio.c munge.c users.c main.c
LCLHDRS = term.h

OBJS = $(subst .c,.o,$(SRCS)) termServer.o device_replyServer.o tioctlServer.o ourmsgUser.o ../libtrivfs/libtrivfs.a ../libports/libports.a ../libihash/libihash.a ../libthreads/libthreads.a

include ../Makeconf

# The reason for -Dout= is to prevent errors for get_init_port,
# get_init_ports, get_init_int, get_init_ints, get_dtable, and get_fd.
# We don't use those, so we're safe in breaking them.
ourmsg_U.h ourmsgUser.c: ourmsg.defs ../hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -Droutine=simpleroutine -Dout= $< \
	| $(MIGCOM) -prefix nowait_ -server /dev/null \
		    -user ourmsgUser.c -header ourmsg_U.h
	mv ourmsg_U.h tmp.h
	sed -e 's/_msg_user_/_ourmsg_user/' < tmp.h > ourmsg_U.h
	rm -f tmp.h

ourmsg.defs: $(includedir)/hurd/msg.defs
	rm -f $@
	ln -s $< $@
