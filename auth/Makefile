# Copyright (C) 1991, 1993, 1994 Free Software Foundation, Inc.
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

dir := auth
makemode := server

SRCS = auth.c
OBJS = auth.o authServer.o auth_replyUser.o notifyServer.o
LCLHDRS = auth_mig.h
target = auth

MIGSFLAGS = "-DAUTH_INTRAN=aptptr_t convert_auth_to_apt (auth_t)" \
	"-DAUTH_IMPORTS=import \"auth_mig.h\";" 

include ../Makeconf

auth.o: auth_S.h auth_reply_U.h notify_S.h
