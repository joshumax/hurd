# 
#   Copyright (C) 1993 Free Software Foundation
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

LIB_SUBDIRS = libioserver libports
PROG_SUBDIRS = auth boot exec fstests hello ifsock init mkbootfs proc term ufs
OTHER_SUBDIRS = hurd i386
SUBDIRS = $(LIB_SUBDIRS) $(PROG_SUBDIRS) $(OTHER_SUBDIRS)

DIST_FILES = COPYING Makeconf Makefile README NEWS

all:
	@echo 'Can't make all yet.'

%-lndist: hurd-snap
	make -C $* lndist

hurd-snap:
	mkdir hurd-snap

dist: hurd-snap $(addsuffix -lndist,$(SUBDIRS)) lndist
	tar cfz hurd-snap.tar.gz hurd-snap
	rm -rf hurd-snap

	