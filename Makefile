# 
#   Copyright (C) 1993, 1994, 1995 Free Software Foundation
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
	      libthreads libps libdirmgt libnetfs libihash libpipe
prog-subdirs = auth boot exec fstests init.trim \
	       proc term ufs utils trans fsck bsdfsck \
	       devio newfs ext2fs benchmarks pflocal tmpfs defpager \
	       login nfs
other-subdirs = hurd doc init tmpfs dev
subdirs = $(lib-subdirs) $(prog-subdirs) $(other-subdirs)
subdirs-nodist = 
working-prog-subdirs := $(filter-out \
			  $(patsubst %/,%,\
				 $(dir $(wildcard $(prog-subdirs:=/BROKEN)))),\
			  $(prog-subdirs))
DIST_FILES = COPYING Makeconf config.make.in configure.in configure \
	     hurd.boot.in build.mk.in \
	     README NEWS tasks INSTALL README-binary 

all: hurd.boot \
     $(addsuffix -all,$(lib-subdirs) $(working-prog-subdirs))

%-all: 
	$(MAKE) -C $* all

%-lndist: hurd-snap
	$(MAKE) -C $* lndist no_deps=t

%-clean:
	$(MAKE) -C $* clean no_deps=t

%-relink:
	$(MAKE) -C $* relink

%-install:
	$(MAKE) -C $* install

%-TAGS:
	$(MAKE) -C $* TAGS no_deps=t

hurd-snap:
	mkdir hurd-snap

date:=$(shell date +%y%m%d)
dist: hurd-snap $(addsuffix -lndist,$(filter-out $(subdirs-nodist), $(subdirs))) lndist
	mv hurd-snap hurd-snap-$(date)
	tar cfz hurd-snap-$(date).tar.gz hurd-snap-$(date)
	rm -rf hurd-snap-$(date)

clean: $(addsuffix -clean,$(lib-subdirs)) $(addsuffix -clean,$(working-prog-subdirs)) clean-misc

relink: $(addsuffix -relink,$(prog-subdirs))

install: $(addsuffix -install,$(lib-subdirs) $(working-prog-subdirs))

lndist: lndist-cthreads-h

lndist-cthreads-h:
	ln -s libthreads/cthreads.h $(srcdir)/hurd-snap/cthreads.h
	
TAGS: $(addsuffix -TAGS,$(prog-subdirs) $(lib-subdirs))

hurd.boot: ${srcdir}/hurd.boot.in config.make
	sed 's%@exec_prefix@%${exec_prefix}%g' $< > $@.new
	mv -f $@.new $@

.PHONY: clean-misc distclean
clean-misc:
	rm -f hurd.boot 

distclean: clean
	rm -f config.make config.log config.status
ifneq (.,${srcdir})
	rm -f Makefile
endif
