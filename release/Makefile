# Makefile for Hurd release tools
#   Copyright (C) 1996 Free Software Foundation, Inc.
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

# Files that go into the root of binary distributions
dist-files = INSTALL-binary dist-README SETUP

# Tools for making images
tool-files = tool-Makefile install-stripped

DIST_FILES = README $(dist-files) $(tool-files)

include ../Makeconf

ifneq ($(dist-root),)
install-dist: dist-links
	cp INSTALL-binary $(dist-root)/
	cp dist-README $(dist-root)/README
	cp SETUP $(dist-root)/SETUP

dist-links:
	mkdir -p $(dist-root)/servers/socket
	touch $(dist-root)/servers/socket/{1,2}
	ln $(dist-root)/servers/socket/1 /servers/socket/local
	ln $(dist-root)/servers/socket/2 /servers/socket/inet
endif

ifneq ($(tool-root),)
install-tools:
	cp install-stripped $(tool-root)/
	cp tool-Makefile $(tool-root)/Makefile
endif
