# 
#   Copyright (C) 1993, 1994 Free Software Foundation
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

dir := .

include Makeconf

LIB_SUBDIRS = libioserver libports libpager libfshelp libdiskfs libtrivfs
PROG_SUBDIRS = auth boot exec fstests hello ifsock init.trim mkbootfs \
	 proc term tmpfs ufs pflocal sh.trim ps
OTHER_SUBDIRS = hurd i386 doc init
SUBDIRS = $(LIB_SUBDIRS) $(PROG_SUBDIRS) $(OTHER_SUBDIRS)

DIST_FILES = COPYING Makeconf Makefile Maketools README NEWS

all: $(addsuffix -all,$(PROG_SUBDIRS))

%-all: 
	make -C $* all

%-lndist: hurd-snap
	make -C $* lndist

%-clean:
	make -C $* clean

%-relink:
	make -C $* relink

%-install:
	make -C $* install

hurd-snap:
	mkdir hurd-snap

dist: hurd-snap $(addsuffix -lndist,$(SUBDIRS)) lndist
	tar cfz hurd-snap.tar.gz hurd-snap
	rm -rf hurd-snap

clean: $(addsuffix -clean,$(LIB_SUBDIRS)) $(addsuffix -clean,$(PROG_SUBDIRS))

relink: $(addsuffix -relink,$(PROG_SUBDIRS))

install: $(addsuffix -install,$(PROG_SUBDIRS))
