# Makefile for Hurd release tools
#   Copyright (C) 1996, 1997, 2012 Free Software Foundation, Inc.
#   Written by Michael I. Bushnell, p/BSG.
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
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.

dir := release
makemode := misc

include ../Makeconf

ifneq ($(dist-root),)
install-dist: dist-links
	cp $(srcdir)/INSTALL-binary $(dist-root)/
	cp $(srcdir)/dist-README $(dist-root)/README
	cp $(srcdir)/SETUP $(dist-root)/SETUP
	cp $(srcdir)/SOURCES.0.2 $(dist-root)/SOURCES
	cp $(srcdir)/servers.boot $(dist-root)/boot/servers.boot
	cp $(srcdir)/menu.lst $(dist-root)/boot/grub/menu.lst
	cp $(top_srcdir)/COPYING $(dist-root)/share/misc/COPYING
	cp $(srcdir)/COPYING.LIB $(dist-root)/share/misc/COPYING.LIB

dist-links:
	mkdir -p $(dist-root)/boot/grub
	mkdir -p $(dist-root)/servers/socket
	mkdir -p $(dist-root)/share/misc
	touch $(dist-root)/servers/socket/1
	touch $(dist-root)/servers/socket/2
	ln -f $(dist-root)/servers/socket/1 $(dist-root)/servers/socket/local
	ln -f $(dist-root)/servers/socket/2 $(dist-root)/servers/socket/inet
	touch $(dist-root)/servers/exec
endif

# Where to get files for the floppies from.
bfloppy-src=$(firstword $(dist-root) /)

# Boot floppies we generate
boot-floppies = bfloppy1.ext2 bfloppy2.ext2 bfloppy.ext2 rfloppy.ext2.gz

boot-floppies: $(boot-floppies)

%.ext2.gz:
	./mkfsimage $@ --compress --owner=root.wheel		\
	  $(bfloppy-src) --copy-rules=$(srcdir)/$*.copy		\
	  $(srcdir)      --copy-rules=$(srcdir)/$*-special.copy
%.ext2:
	./mkfsimage $@ --owner=root.wheel			\
	  $(bfloppy-src) --copy-rules=$(srcdir)/$*.copy		\
	  $(srcdir)      --copy-rules=$(srcdir)/$*-special.copy

# floppy dependencies
ifneq ($(no_deps),t)
-include $(patsubst %,%.f_d, $(boot-floppies))

%.ext2.f_d %.ext2.gz.f_d: mkfsimage
	./mkfsimage $(patsubst %.f_d,%,$@) --dependencies=$@	\
	  $(bfloppy-src) --copy-rules=$(srcdir)/$*.copy		\
	  $(srcdir)      --copy-rules=$(srcdir)/$*-special.copy
endif
