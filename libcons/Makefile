#   Copyright (C) 1994,95,96,97,98,99,2000,01,02,2005,2010,2012 Free Software
#   Foundation, Inc.
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

dir := libcons
makemode := library

libname = libcons
SRCS= demuxer.c init-init.c init-loop.c opts-version.c extra-version.c \
      dir-changed.c file-changed.c opts-std-startup.c cons-lookup.c \
      cons-switch.c vcons-remove.c vcons-add.c vcons-open.c \
      vcons-close.c vcons-destroy.c vcons-refresh.c vcons-scrollback.c \
      vcons-input.c vcons-move-mouse.c vcons-event.c
installhdrs = cons.h

fs_notify-MIGSFLAGS = -imacros $(srcdir)/mutations.h
MIGSTUBS = fs_notifyServer.o
OBJS = $(sort $(SRCS:.c=.o) $(MIGSTUBS))

HURDLIBS = ports
LDLIBS += -lpthread

MIGCOMSFLAGS = -prefix cons_

include ../Makeconf
