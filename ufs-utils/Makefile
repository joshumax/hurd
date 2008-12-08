# 
#   Copyright (C) 1994, 1996, 2008 Free Software Foundation
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

dir := ufs-utils
makemode := utilities

targets = mkfs.ufs clri.ufs stati.ufs
target-suffix = .ufs
SRCS = mkfs.c clri.c stati.c dlabel.c
installationdir = $(sbindir)

OBJS = $(SRCS:.c=.o)
HURDLIBS = store shouldbeinlibc

include ../Makeconf

mkfs.ufs: mkfs.o dlabel.o ../libstore/libstore.a ../libshouldbeinlibc/libshouldbeinlibc.a

$(targets): %.ufs: %.o


