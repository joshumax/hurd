# Makefile for libstore
#
#   Copyright (C) 1995, 1996, 1997, 1999, 2001, 2002, 2010, 2012, 2013, 2014,
#   2016 Free Software Foundation, Inc.
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
	      concat \
	      copy \
	      device \
	      file \
	      ileave \
	      memobj \
	      module \
	      mvol \
	      nbd \
	      remap \
	      task \
	      zero

# This has to be evaluated after config.make has been included;
# as a consequence, using 'ifneq' or similar is not an option.
store-types += \
	      $(and $(PARTED_LIBS),part) \
	      $(and $(HAVE_LIBBZ2),bunzip2) \
	      $(and $(HAVE_LIBZ),gunzip) \

libstore.so-LDLIBS += $(PARTED_LIBS) -ldl
installhdrs=store.h

HURDLIBS = shouldbeinlibc
LDLIBS += -lpthread $(and $(HAVE_LIBBZ2),-lbz2) $(and $(HAVE_LIBZ),-lz)
GUNZIP_OBJS = do-gunzip.o util.o
BUNZIP2_OBJS = do-bunzip2.o
OBJS = $(SRCS:.c=.o) \
	      $(and $(HAVE_LIBZ),$(GUNZIP_OBJS)) \
	      $(and $(HAVE_LIBBZ2),$(BUNZIP2_OBJS))

include ../Makeconf

module-CPPFLAGS = -D'STORE_SONAME_SUFFIX=".so.$(hurd-version)"'
module-DEPS = $(..)config.make

libstore_gunzip.so.$(hurd-version): $(GUNZIP_OBJS:.o=_pic.o)
libstore_bunzip2.so.$(hurd-version): $(BUNZIP2_OBJS:.o=_pic.o)

# You can use this rule to make a dynamically-loadable version of any
# of the modules.  We don't make any of these by default, since we
# just include all the standard store types in libstore.so itself.
libstore_%.so.$(hurd-version): %_pic.o libstore.so
	$(CC) -shared -Wl,-soname=$@ -o $@ \
	      $(lpath) $(CFLAGS) $(LDFLAGS) $(libstore_$*.so-LDFLAGS) $^

# Each libstore_TYPE.a is in fact an object file script so that `-lstore_TYPE'
# just has the same effect as `-u store_TYPE_class'.
$(store-types:%=libstore_%.a): libstore_%.a: $(srcdir)/Makefile
	$(CC) -r -nostdlib -nostartfiles -x c /dev/null \
	      -o $@ -u store_$*_class
cleantarg += $(store-types:%=libstore_%.a)

all: $(store-types:%=libstore_%.a)

install: $(addprefix $(DESTDIR),$(store-types:%=$(libdir)/libstore_%.a))
$(addprefix $(DESTDIR),$(store-types:%=$(libdir)/libstore_%.a)): $(DESTDIR)$(libdir)/%: %
	$(INSTALL_DATA) $< $@
