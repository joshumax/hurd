# Makefile for libstore
#
#   Copyright (C) 1995,96,97,2001,02 Free Software Foundation, Inc.
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
SRCS = create.c derive.c make.c rdwr.c set.c \
       enc.c encode.c decode.c clone.c argp.c kids.c flags.c \
       open.c xinl.c typed.c map.c url.c unknown.c \
       stripe.c $(filter-out ileave.c concat.c,$(store-types:=.c))

store-types = \
	      bunzip2 \
	      concat \
	      copy \
	      device \
	      file \
	      gunzip \
	      ileave \
	      memobj \
	      module \
	      mvol \
	      nbd \
	      remap \
	      task \
	      zero

libstore.so-LDLIBS += -ldl
LCLHDRS=store.h
installhdrs=store.h

HURDLIBS = shouldbeinlibc
GUNZIP_OBJS = unzip.o inflate.o util.o
BUNZIP2_OBJS = do-bunzip2.o
OBJS = $(patsubst part.o,parted.o,$(SRCS:.c=.o)) $(GUNZIP_OBJS) $(BUNZIP2_OBJS)
DIST_FILES = unzipstore.c

# Look for zip stuff
vpath %.c $(srcdir)/../exec
CPPFLAGS += -I$(srcdir)/../exec

include ../Makeconf

module-CPPFLAGS = -D'STORE_SONAME_SUFFIX=".so.$(hurd-version)"'

libstore_gunzip.so.$(hurd-version): $(GUNZIP_OBJS:.o=_pic.o)
libstore_bunzip2.so.$(hurd-version): $(BUNZIP2_OBJS:.o=_pic.o)

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


libstore_%.so.$(hurd-version): %_pic.o libstore.so
	$(CC) -shared -Wl,-soname=$@ -o $@ \
	      $(rpath) $(CFLAGS) $(LDFLAGS) $(libstore_$*.so-LDFLAGS) $^

# Each libstore_TYPE.a is in fact an object file script so that `-lstore_TYPE'
# just has the same effect as `-u store_TYPE_class'.
$(store-types:%=libstore_%.a): libstore_%.a: $(srcdir)/Makefile
	$(CC) -r -nostdlib -nostartfiles -x c /dev/null \
	      -o $@ -u store_$*_class
cleantarg += $(store-types:%=libstore_%.a)

all: $(store-types:%=libstore_%.a)

install: $(store-types:%=$(libdir)/libstore_%.a)
$(store-types:%=$(libdir)/libstore_%.a): $(libdir)/%: %
	$(INSTALL_DATA) $< $@
