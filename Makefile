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
makemode := misc

include Makeconf

lib-subdirs = libioserver libports libpager libfshelp libdiskfs libtrivfs \
	      libthreads 
prog-subdirs = auth boot exec fstests init.trim mkbootfs \
	       proc term ufs pflocal pipes dev.trim utils trans fsck bsdfsck
other-subdirs = hurd doc init tmpfs dev ext2fs 
subdirs = $(lib-subdirs) $(prog-subdirs) $(other-subdirs)
subdirs-nodist = ext2fs libnetserv
working-prog-subdirs := $(filter-out \
			  $(patsubst %/,%,\
				 $(dir $(wildcard $(prog-subdirs:=/BROKEN)))),\
			  $(prog-subdirs))
DIST_FILES = COPYING Makeconf Maketools README NEWS missing tasks INSTALL

all: $(addsuffix -all,$(working-prog-subdirs))

%-all: 
	$(MAKE) -C $* all

%-lndist: hurd-snap
	$(MAKE) -C $* lndist

%-clean:
	$(MAKE) -C $* clean

%-relink:
	$(MAKE) -C $* relink

%-install:
	$(MAKE) -C $* install

%-TAGS:
	$(MAKE) -C $* TAGS

hurd-snap:
	mkdir hurd-snap

dist: hurd-snap $(addsuffix -lndist,$(filter-out $(subdirs-nodist), $(subdirs))) lndist
	tar cfz hurd-snap.tar.gz hurd-snap
	rm -rf hurd-snap

clean: $(addsuffix -clean,$(lib-subdirs)) $(addsuffix -clean,$(prog-subdirs))

relink: $(addsuffix -relink,$(prog-subdirs))

install: $(addsuffix -install,$(working-prog-subdirs))

TAGS: $(addsuffix -TAGS,$(prog-subdirs) $(lib-subdirs))
