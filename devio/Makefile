# 
#   Copyright (C) 1995 Free Software Foundation, Inc.
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

dir := devio
makemode := server

target = devio
SRCS = dev.c iostate.c window.c devio.c open.c devpager.c io.c rdwr.c mem.c
LCLHDRS = dev.h iostate.h window.h open.h ptypes.h mem.h
HURDLIBS = libtrivfs libpager libports libfshelp libthreads
DIST_FILES = MAKEDEV

OBJS = $(SRCS:.c=.o) error.o

CPPFLAGS += -I../lib
CPPFLAGS += $(CPPFLAGS-$(notdir $<))

CPPFLAGS-error.c = -Dprogram_name=program_invocation_name -DHAVE_VPRINTF -DSTDC_HEADERS -DHAVE_STRERROR

vpath %.c ../lib

include ../Makeconf

install: $(prefix)/dev/MAKEDEV

$(prefix)/dev/MAKEDEV: MAKEDEV
	$(INSTALL_DATA) MAKEDEV $(prefix)/dev/MAKEDEV
	chmod +x $(prefix)/dev/MAKEDEV

open.o rdwr.o window.o: window.h
dev.o devio.o devpager.o io.o iostate.o open.o rdwr.o window.o: dev.h
devio.o io.o open.o rdwr.o: open.h
dev.o io.o iostate.o: iostate.h
dev.o devio.o devpager.o: ../hurd/pager.h
devio.o io.o: ../hurd/ports.h ../hurd/trivfs.h