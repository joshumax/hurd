#
#   Copyright (C) 1993-1999, 2001, 2002, 2004, 2006, 2009,
#   2011-2013, 2015-2019 Free Software Foundation, Inc.
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
	       console-client utils sutils libfshelp-tests \
	       benchmarks fstests \
	       procfs \
	       startup \
	       init \
	       devnode \
	       eth-multiplexer \
	       acpi \
	       shutdown

ifeq ($(HAVE_SUN_RPC),yes)
prog-subdirs += nfs nfsd
endif

ifeq ($(HAVE_LIBLWIP),yes)
prog-subdirs += lwip
endif

ifeq ($(HAVE_LIBPCIACCESS),yes)
prog-subdirs += pci-arbiter
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
dist: $(foreach Z,xz gz,$(dist-version).tar.$(Z))
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

# See the ChangeLog file.
gitlog-to-changelog_rev = \
  2772f5c6a6a51cf946fd95bf6ffe254273157a21..
ChangeLog_files = \
  $(filter-out :%,$(subst :, :,$(ChangeLog_specs)))
ChangeLog_specs = \
  ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  auth/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  benchmarks/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  boot/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  config/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  console-client/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  console/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  daemons/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  defpager/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  doc/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  exec/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  ext2fs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  fatfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  fstests/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  ftpfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  hostmux/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  hurd/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  include/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  init/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  isofs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libcons/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libdirmgt/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libdiskfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libfshelp/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libftpconn/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libhurdbugaddr/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libihash/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libiohelp/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libnetfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libpager/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libpipe/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libports/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libps/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libshouldbeinlibc/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libstore/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libthreads/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  libtrivfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  login/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  mach-defpager/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  nfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  nfsd/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  pfinet/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  pflocal/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  proc/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  release/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  storeio/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  sutils/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  term/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  tmpfs/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  trans/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  usermux/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21 \
  utils/ChangeLog:2772f5c6a6a51cf946fd95bf6ffe254273157a21
# procfs
gitlog-to-changelog_rev += \
  ^aac4aaf42372f61c78061711916c81a9d5bcb42d~1
ChangeLog_specs += \
  procfs/ChangeLog:edb4593c38d421b5d538b221a991b50c36fdba15:ChangeLog
# random
gitlog-to-changelog_rev += \
  ^1ba2ed95690396bf081d0af043d878b26b8563c2~1
# Specify dummy ChangeLog file, will get overwritten.
ChangeLog_specs += \
  random/ChangeLog:HEAD:ChangeLog
.PHONY: gen-ChangeLog
gen-ChangeLog:
	$(AM_V_GEN)if test -d $(top_srcdir)/.git; then			\
	  rm -f $(ChangeLog_files) &&					\
	  (cd $(top_srcdir)/ &&						\
	  ./gitlog-to-changelog	--strip-tab				\
	    $(gitlog-to-changelog_rev) &&				\
	  echo) >> ChangeLog &&						\
	  (cd $(top_srcdir)/ &&						\
	  ./gitlog-to-changelog --strip-tab				\
	    edb4593c38d421b5d538b221a991b50c36fdba15..aac4aaf42372f61c78061711916c81a9d5bcb42d~1 && \
	  echo) >> procfs/ChangeLog &&					\
	  rm -f random/ChangeLog-1 &&					\
	  (cd $(top_srcdir)/ &&						\
	  ./gitlog-to-changelog --strip-tab				\
	    ac38884dc9ad32a11d09f55ba9fe399cd0a48e2f..1ba2ed95690396bf081d0af043d878b26b8563c2~1 && \
	  echo) >> random/ChangeLog-1 &&				\
	  for cs in $(ChangeLog_specs); do				\
	    f=$${cs%%:*} &&						\
	    s=$${cs#$$f:} && s_f=$$s:$$f && s=$${s_f/*:*:*/$$s} &&	\
	    (cd $(top_srcdir)/ &&					\
	    git show $$s) >> $$f					\
	    || exit $$?;						\
	  done &&							\
	  rm -f random/ChangeLog_ &&					\
	  (cd $(top_srcdir)/ &&						\
	  git ls-tree --name-only					\
	    ac38884dc9ad32a11d09f55ba9fe399cd0a48e2f) >> random/ChangeLog_ && \
	  rm -f random/ChangeLog-2 &&					\
	  perl \
	    -e 'print "2011-08-18  Gaël Le Mignot  <kilobug\@freesurf.fr>\n\n"; while(<>){s%^%\t* %;s%$$%: New file.%;print;}' \
	    < random/ChangeLog_ > random/ChangeLog-2 &&			\
	  cat random/ChangeLog-* > random/ChangeLog &&			\
	  rm random/ChangeLog?* &&					\
	  sed								\
	    -e 's%\* [ ]*%* procfs/%'					\
	    -e s%procfs/procfs/%procfs/%				\
	    -i procfs/ChangeLog &&					\
	  sed								\
	    -e 's%\* [ ]*%* random/%'					\
	    -i random/ChangeLog;					\
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

%.xz: %
	xz < $< > $@

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
