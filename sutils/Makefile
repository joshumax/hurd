# Makefile for sutils 
# 
#   Copyright (C) 1996 Free Software Foundation
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

targets = reboot halt fsck e2os
special-targets = e2os
installationdir = $(sbindir)
SCRIPTS = e2os.sh
SRCS = reboot.c halt.c fsck.c fstab.c clookup.c $(SCRIPTS)
LCLHDRS = fstab.h
DIST_FILES = MAKEDEV.sh

OBJS = $(filter-out $(SCRIPTS),$(SRCS:.c=.o))

include ../Makeconf

all: MAKEDEV
install: $(prefix)/dev/MAKEDEV

fsck: fstab.o clookup.o
reboot halt fsck: ../libshouldbeinlibc/libshouldbeinlibc.a

$(filter-out $(special-targets), $(targets)): %: %.o

$(prefix)/dev/MAKEDEV: MAKEDEV
	$(INSTALL_PROGRAM) MAKEDEV $(prefix)/dev/MAKEDEV

%: %.sh $(top_srcdir)/sh-version.sed
	sed -f $(top_srcdir)/sh-version.sed < $< > $@
