# Makefile for sutils
#
#   Copyright (C) 1996,97,99 Free Software Foundation, Inc.
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

targets = $(special-targets) reboot halt fsck swapon swapoff
special-targets = e2os MAKEDEV losetup
installationdir = $(sbindir)
SCRIPTS = e2os.sh MAKEDEV.sh losetup.sh
SRCS = reboot.c halt.c fsck.c fstab.c clookup.c swapon.c swapoff.c $(SCRIPTS)
LCLHDRS = fstab.h

OBJS = $(filter-out $(SCRIPTS),$(SRCS:.c=.o))
HURDLIBS = store shouldbeinlibc

include ../Makeconf

fsck: fstab.o clookup.o
swapon swapoff: ../libstore/libstore.a
reboot halt fsck swapon swapoff: ../libshouldbeinlibc/libshouldbeinlibc.a

$(filter-out $(special-targets), $(targets)): %: %.o

install: $(prefix)/dev $(prefix)/dev/MAKEDEV
$(prefix)/dev/MAKEDEV:
	ln -s ../sbin/MAKEDEV $@
$(prefix)/dev:
	@$(MKINSTALLDIRS) $@
