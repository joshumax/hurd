# Makefile for libshouldbeinlibc
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

dir := libshouldbeinlibc
makemode := library

libname = libshouldbeinlibc
SRCS = error.c argz.c options.c
LCLHDRS = error.h argz.h rwlock.h options.h
installhdrs = $(LCLHDRS)
installhdrsubdir = .

OBJS = $(SRCS:.c=.o)

CPPFLAGS += $(CPPFLAGS-$(notdir $<))
CPPFLAGS-error.c = -Dprogram_name=program_invocation_name -DHAVE_VPRINTF -DSTDC_HEADERS -DHAVE_STRERROR

include ../Makeconf

lndist: $(srcdir)/hurd-snap/$(dir)/error.c $(srcdir)/hurd-snap/$(dir)/error.h

$(srcdir)/hurd-snap/$(dir)/error.c: error.c
	cp $< $@
$(srcdir)/hurd-snap/$(dir)/error.h: error.h
	cp $< $@
