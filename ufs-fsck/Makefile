#   Copyright (C) 1994, 1995, 1996, 2012 Free Software Foundation
#   Written by Michael I. Bushnell.
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
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := ufs-fsck
makemode := utility

SRCS = dir.c main.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c setup.c utilities.c inode.c
OBJS = $(subst .c,.o,$(SRCS)) tables.o
target = fsck.ufs
installationdir = $(sbindir)
HURDLIBS=shouldbeinlibc

include ../Makeconf

vpath tables.c $(top_srcdir)/ufs

