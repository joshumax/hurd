# Makefile for libstore
#
#   Copyright (C) 1995,96,97,2001 Free Software Foundation, Inc.
#   Written by Miles Bader <miles@gnu.org>
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
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.

dir := libstore
makemode := library

libname = libstore
SRCS = create.c derive.c make.c rdwr.c set.c device.c file.c stripe.c	   \
	enc.c encode.c decode.c clone.c argp.c std.c kids.c zero.c flags.c \
	open.c remap.c xinl.c task.c typed.c copy.c gunzip.c map.c mvol.c \
	bunzip2.c part.c nbd.c
LCLHDRS=store.h
installhdrs=store.h

HURDLIBS = shouldbeinlibc
UNZIP_OBJS = unzip.o inflate.o util.o do-bunzip2.o
OBJS = $(patsubst part.o,parted.o,$(SRCS:.c=.o)) $(UNZIP_OBJS)

# Look for zip stuff
VPATH += $(srcdir)/../exec
CPPFLAGS += -I$(srcdir)/../exec

include ../Makeconf

ifeq (,$(strip $(PARTED_LIBS)))
# Configure didn't find the Parted libraries, so we have no "part" store type.

parted.o parted_p.o parted_pic.o:
	$(CC) -o $@ -c -x c /dev/null

else

# We have Parted, so link against it.  For the static libraries,
# we slurp in all the Parted code we need and stuff it into one
# object file along with our code that uses it.

libstore.so-LDFLAGS += $(PARTED_LIBS)

parted.o: part.o $(..)config.make
	$(CC) -nostdlib -r -o $@ $< $(PARTED_LIBS)

parted_p.o: part_p.o $(..)config.make
	$(CC) -nostdlib -r -o $@ $< $(PARTED_LIBS:=_p)

parted_pic.o: part_pic.o
	ln -f $< $@

endif
