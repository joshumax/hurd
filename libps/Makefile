# Makefile for libps
# 
#   Copyright (C) 1995, 1996 Free Software Foundation
#   Written by Michael I. Bushnell.
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
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := libps
makemode := library

libname = libps
SRCS = context.c filters.c fmt.c host.c proclist.c procstat.c spec.c \
	tty.c user.c write.c
LCLHDRS=ps.h ps_msg.h ps_term.h
installhdrs=$(LCLHDRS)
installhdrsubdir = .

OBJS = $(SRCS:.c=.o) msgUser.o termUser.o

MIGCOMUFLAGS=-subrprefix __
msg-MIGUFLAGS = -D'MSG_IMPORTS=waittime 1000;' -DUSERPREFIX=ps_
term-MIGUFLAGS = -D'TERM_IMPORTS=waittime 1000;' -DUSERPREFIX=ps_

ps_%.h: %_U.h
	sed 's/_$*_user_/_ps_$*_user_/g' $< > $@

include ../Makeconf

libps.so: ../libihash/libihash.so
