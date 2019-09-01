# Makefile for libshouldbeinlibc
#
#   Copyright (C) 1995,96,97,98,99,2002,2012 Free Software Foundation, Inc.
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
SRCS = termsize.c timefmt.c exec-reauth.c maptime-funcs.c \
       canon-host.c maptime.c shared-dom.c localhost.c wire.c portinfo.c \
       xportinfo.c portxlate.c lcm.c cacheq.c fsysops.c \
       idvec.c idvec-auth.c idvec-funcs.c \
       idvec-impgids.c idvec-verify.c idvec-rep.c \
       ugids.c ugids-argp.c ugids-rep.c ugids-verify.c ugids-subtract.c \
       ugids-auth.c ugids-xinl.c ugids-merge.c ugids-imply.c ugids-posix.c \
       ugids-verify-auth.c nullauth.c \
       refcount.c \
       assert-backtrace.c \

installhdrs = idvec.h timefmt.h maptime.h \
	      wire.h portinfo.h portxlate.h cacheq.h ugids.h nullauth.h \
	      refcount.h \
	      assert-backtrace.h \

installhdrsubdir = .

LDLIBS += $(and $(HAVE_LIBCRYPT),-lcrypt)

OBJS = $(SRCS:.c=.o)

include ../Makeconf
