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

dir := libfshelp
makemode := library

SRCS =	lock-acquire.c lock-init.c \
	start-translator-long.c start-translator.c \
	fetch-root.c transbox-init.c translated.c set-active.c fetch-control.c drop-transbox.c

OBJS = $(subst .c,.o,$(SRCS))

LCLHDRS = trans.h fshelp.h locks.h
REMHDRS = ../hurd/ports.h
libname = libfshelp
installhdrs = fshelp.h

include ../Makeconf

$(OBJS): fshelp.h ../hurd/ports.h
handle-startup.o init-trans.o start-trans.o transboot-clean.o: trans.h
trans-cntl.o kill-trans.o trans-iter.o: trans.h
lock-acquire.o lock-init.o: locks.h

