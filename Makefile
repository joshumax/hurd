#
#   Copyright (C) 1993,94,95,96,97,98,99,2001,02 Free Software Foundation, Inc.
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

include ./Makeconf

DIST_FILES = COPYING Makeconf config.make.in configure.in configure \
	     move-if-change hurd.boot build.mk.in build.mkcf.in aclocal.m4 \
	     README NEWS tasks INSTALL INSTALL-cross version.h.in


## Subdirectories of this directory should all be mentioned here

# Hurd libraries
lib-subdirs = libshouldbeinlibc libihash libiohelp libports libthreads \
	      libpager libfshelp libdiskfs libtrivfs libps \
	      libnetfs libpipe libstore libhurdbugaddr libftpconn libcons

# Hurd programs
prog-subdirs = auth proc exec init term \
	       ufs ext2fs isofs nfs tmpfs \
	       storeio pflocal pfinet defpager mach-defpager \
	       login daemons nfsd boot serverboot console \
	       hostmux usermux ftpfs trans \
	       console-client utils sutils ufs-fsck ufs-utils \
	       benchmarks fstests

# Other directories
other-subdirs = hurd doc config release include debian

# All the subdirectories together
subdirs = $(lib-subdirs) $(prog-subdirs) $(other-subdirs)

# Any subdirectories here that we don't want to distribute to the world
subdirs-nodist =

# This allows the creation of a file BROKEN in any of the prog-subdirs;
# that will prevent this top level Makefile from attempting to make it.
working-prog-subdirs := $(filter-out \
			  $(patsubst %/,%,\
				 $(dir $(wildcard $(prog-subdirs:=/BROKEN)))),\
			  $(prog-subdirs))


$(subdirs): version.h

version.h: version.h.in
	sed -e 's/MASTER_HURD_VERSION/\"$(hurd-version)\"/' < $< > $@


## GNU Coding Standards targets (not all are here yet), and some other
## similar sorts of things

all: $(lib-subdirs) $(working-prog-subdirs)

# Create a distribution tar file.  Set make variable `version' on the
# command line; otherwise the tar file will be a dated snapshot.
ifeq ($(version),)
version:=$(shell date +%Y%m%d)
endif
dirname:=hurd

dist: $(srcdir)/hurd-snap $(addsuffix -lndist,$(filter-out $(subdirs-nodist), $(subdirs))) lndist
	mv $(srcdir)/hurd-snap $(srcdir)/$(dirname)-$(version)
	cd $(srcdir); tar cfz $(dirname)-$(version).tar.gz $(dirname)-$(version)
	rm -rf $(srcdir)/$(dirname)-$(version)

clean: $(addsuffix -clean,$(lib-subdirs)) $(addsuffix -clean,$(working-prog-subdirs)) clean-misc

relink: $(addsuffix -relink,$(lib-subdirs) $(prog-subdirs))

objs: $(addsuffix -objs,$(lib-subdirs) $(prog-subdirs))

install: $(addsuffix -install,$(lib-subdirs) $(working-prog-subdirs) \
	   $(other-subdirs))

install-headers: $(addsuffix -install-headers,$(lib-subdirs) \
		$(working-prog-subdirs)\
		$(other-subdirs))

TAGS: $(addsuffix -TAGS,$(working-prog-subdirs) $(lib-subdirs))
	etags -o $@ $(patsubst %-TAGS,-i %/TAGS,$^)

## Targets used by the main targets above.
$(prog-subdirs) $(lib-subdirs): FORCE
	$(MAKE) -C $@ all
FORCE:

%-lndist: $(top_srcdir)/hurd-snap
	$(MAKE) -C $* lndist no_deps=t

%-clean:
	$(MAKE) -C $* clean no_deps=t

%-relink:
	$(MAKE) -C $* relink no_deps=t

%-objs:
	$(MAKE) -C $* objs

%-install:
	$(MAKE) -C $* install

%-install-headers:
	$(MAKE) -C $* install-headers

%-TAGS:
	$(MAKE) -C $* TAGS no_deps=t

$(srcdir)/hurd-snap:
	mkdir $(srcdir)/hurd-snap

lndist: cp-linked-files

linked-files = install-sh config.guess config.sub mkinstalldirs
lf-inst = $(addprefix $(srcdir)/hurd-snap/,$(linked-files))
cp-linked-files: $(lf-inst)
$(lf-inst): $(srcdir)/hurd-snap/%: $(srcdir)/%
	cp $< $@

.PHONY: clean-misc distclean
clean-misc:

distclean: clean
	rm -f config.make config.log config.status config.cache
ifneq (.,${srcdir})
	rm -f Makefile
endif


## Directory dependencies
#
# Some directories depend on others, so we need to find out exactly
# what they are.  This does that for us.

ifneq ($(no_deps),t)
-include $(addsuffix .d,$(subdirs))
endif

# How to build them
$(addsuffix .d,$(subdirs)): %.d: $(top_srcdir)/%/Makefile
	$(MAKE) -C $* directory-depend no_deps=t
