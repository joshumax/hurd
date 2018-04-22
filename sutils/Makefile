# Makefile for sutils
#
#   Copyright (C) 1996,97,99,2000,2010,2012 Free Software Foundation, Inc.
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
#

dir := sutils
makemode := utilities

progs = reboot halt fsck swapon swapoff bless
scripts = e2os MAKEDEV losetup
targets = $(special-targets) $(progs)
special-targets = $(scripts)
installationdir = $(sbindir)
SRCS = $(progs:=.c) clookup.c fstab.c update.c $(scripts:=.sh)

OBJS = $(progs:=.o)
HURDLIBS = store shouldbeinlibc

include ../Makeconf

fsck: fstab.o clookup.o
swapon swapoff: ../libstore/libstore.a default_pagerUser.o
$(progs): %: %.o ../libshouldbeinlibc/libshouldbeinlibc.a

install: $(DESTDIR)$(prefix)/dev/MAKEDEV
$(DESTDIR)$(prefix)/dev/MAKEDEV: $(DESTDIR)$(prefix)/dev
	ln -sf ../sbin/MAKEDEV $@
$(DESTDIR)$(prefix)/dev:
	@$(MKINSTALLDIRS) $@
