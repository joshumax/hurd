#
#   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2001, 2002, 2004,
#   2006, 2009, 2011, 2012, 2013 Free Software Foundation, Inc.
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

DISTFILES := configure

include ./Makeconf

## Subdirectories of this directory should all be mentioned here

# Hurd libraries
lib-subdirs = libshouldbeinlibc libihash libiohelp libports libthreads \
	      libpager libfshelp libdiskfs libtrivfs libps \
	      libnetfs libpipe libstore libhurdbugaddr libftpconn libcons \
	      libhurd-slab \
	      libbpf \

# Hurd programs
prog-subdirs = auth proc exec term \
	       ext2fs isofs tmpfs fatfs \
	       storeio pflocal pfinet defpager mach-defpager \
	       login daemons boot console \
	       hostmux usermux ftpfs trans \
	       console-client utils sutils \
	       benchmarks fstests \
	       random \
	       procfs \
	       startup \
	       init \
	       devnode \
	       eth-multiplexer \

ifeq ($(HAVE_SUN_RPC),yes)
prog-subdirs += nfs nfsd
endif

# Other directories
other-subdirs = hurd doc config release include

# All the subdirectories together
subdirs = $(lib-subdirs) $(prog-subdirs) $(other-subdirs)

# This allows the creation of a file BROKEN in any of the prog-subdirs;
# that will prevent this top level Makefile from attempting to make it.
working-prog-subdirs := $(filter-out \
			  $(patsubst %/,%,\
				 $(dir $(wildcard $(prog-subdirs:=/BROKEN)))),\
			  $(prog-subdirs))


$(subdirs): version.h

## GNU Coding Standards targets (not all are here yet), and some other
## similar sorts of things

all: $(lib-subdirs) $(working-prog-subdirs)

# Create a distribution tar file.

git_describe := git describe --match '*release*'
dist-version := $(shell cd $(top_srcdir)/ && $(git_describe))

.PHONY: dist
ifdef configured
dist: $(foreach Z,bz2 gz,$(dist-version).tar.$(Z))
else
dist:
	@echo >&2 'Cannot build a distribution from an unconfigured tree.'
	false
endif

HEAD.tar: FORCE
	cd $(top_srcdir)/ && git status --short \
	  | $(AWK) '{ print; rc=1 } END { exit rc }' \
	  || { echo >&2 \
		'Refusing to build a distribution from dirty sources.' && \
		false; }
	(cd $(top_srcdir)/ && git archive --prefix=$(dist-version)/ HEAD) > $@

ChangeLog.tar: gen-ChangeLog
	tar -c -f $@ --owner=0 --group=0 \
	  --transform='s%^%$(dist-version)/%' $(ChangeLog_files)

gen_start_commit = 2772f5c6a6a51cf946fd95bf6ffe254273157a21
ChangeLog_files = \
  ChangeLog \
  auth/ChangeLog \
  benchmarks/ChangeLog \
  boot/ChangeLog \
  config/ChangeLog \
  console-client/ChangeLog \
  console/ChangeLog \
  daemons/ChangeLog \
  defpager/ChangeLog \
  doc/ChangeLog \
  exec/ChangeLog \
  ext2fs/ChangeLog \
  fatfs/ChangeLog \
  fstests/ChangeLog \
  ftpfs/ChangeLog \
  hostmux/ChangeLog \
  hurd/ChangeLog \
  include/ChangeLog \
  init/ChangeLog \
  isofs/ChangeLog \
  libcons/ChangeLog \
  libdirmgt/ChangeLog \
  libdiskfs/ChangeLog \
  libfshelp/ChangeLog \
  libftpconn/ChangeLog \
  libhurdbugaddr/ChangeLog \
  libihash/ChangeLog \
  libiohelp/ChangeLog \
  libnetfs/ChangeLog \
  libpager/ChangeLog \
  libpipe/ChangeLog \
  libports/ChangeLog \
  libps/ChangeLog \
  libshouldbeinlibc/ChangeLog \
  libstore/ChangeLog \
  libthreads/ChangeLog \
  libtrivfs/ChangeLog \
  login/ChangeLog \
  mach-defpager/ChangeLog \
  nfs/ChangeLog \
  nfsd/ChangeLog \
  pfinet/ChangeLog \
  pflocal/ChangeLog \
  proc/ChangeLog \
  release/ChangeLog \
  storeio/ChangeLog \
  sutils/ChangeLog \
  term/ChangeLog \
  tmpfs/ChangeLog \
  trans/ChangeLog \
  usermux/ChangeLog \
  utils/ChangeLog
.PHONY: gen-ChangeLog
gen-ChangeLog:
	$(AM_V_GEN)if test -d $(top_srcdir)/.git; then			\
	  rm -f $(ChangeLog_files) &&					\
	  (cd $(top_srcdir)/ &&						\
	  ./gitlog-to-changelog	--strip-tab				\
	    $(gen_start_commit).. &&					\
	  echo) >> ChangeLog &&						\
	  for f in $(ChangeLog_files); do				\
	    (cd $(top_srcdir)/ &&					\
	    git show $(gen_start_commit):$$f) >> $$f			\
	    || exit $$?;						\
	  done;								\
	fi

$(dist-version).tar: HEAD.tar $(addsuffix /dist-hook,hurd/.. $(subdirs)) ChangeLog.tar
	tar -c -f $@ --files-from=/dev/null
# Concatenate the tar files.  Have
# to do it one by one: <http://savannah.gnu.org/patch/?7757>.
	for f in HEAD.tar dist.tar */dist.tar ChangeLog.tar; do \
	  tar -v --concatenate -f $@ "$$f"; \
	done

clean: $(addsuffix -clean,$(subdirs)) clean-misc

relink: $(addsuffix -relink,$(lib-subdirs) $(prog-subdirs))

objs: $(addsuffix -objs,$(lib-subdirs) $(prog-subdirs))

install: $(addsuffix -install,$(lib-subdirs) $(working-prog-subdirs) \
	   $(other-subdirs))

install-headers: $(addsuffix -install-headers,$(lib-subdirs) \
		$(working-prog-subdirs)\
		$(other-subdirs))

TAGS: $(addsuffix -TAGS,$(working-prog-subdirs) $(lib-subdirs))
	etags -o $@ $(patsubst %-TAGS,-i %/TAGS,$^)

%.bz2: %
	bzip2 -9 < $< > $@

%.gz: %
	gzip -9n < $< > $@

## Targets used by the main targets above.
$(prog-subdirs) $(lib-subdirs): FORCE
	$(MAKE) -C $@ all
FORCE:

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

.PHONY: %/dist-hook
%/dist-hook:
	$(MAKE) -C $* dist-hook no_deps=t dist-version=$(dist-version)

.PHONY: clean-misc distclean
clean-misc:
	rm -f HEAD.tar ChangeLog.tar $(ChangeLog_files)

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


## Build system

AUTOCONF = autoconf
AUTOCONF_FLAGS = -I $(top_srcdir)

$(top_srcdir)/configure: $(top_srcdir)/configure.ac $(top_srcdir)/aclocal.m4
	$(AUTOCONF) $(AUTOCONF_FLAGS) $< > $@
	chmod +x $@

config.status: $(top_srcdir)/configure
	$(SHELL) $@ --recheck

config.make: config.status $(top_srcdir)/config.make.in
# No stamp file is used here, as config.make's timestamp changing will not have
# any far-reaching consequences.
	$(SHELL) $< --file=$@

version.h: stamp-version; @:
stamp-version: version.h.in config.make
	sed -e 's/MASTER_HURD_VERSION/\"$(package-version)\"/' \
	  < $< > version.h.new
	$(move-if-change) version.h.new version.h
	touch $@
