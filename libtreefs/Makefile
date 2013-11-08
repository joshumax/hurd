# Makefile for libtreefs 
#
#   Copyright (C) 1995, 2012 Free Software Foundation, Inc.
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

dir := libtreefs
makemode := library

libname = libtreefs
installhdrs = treefs.h
# RPC stubs
S_SRCS = s-file.c s-dir.c s-io.c s-fsys.c
OTHERSRCS = defhooks.c dir-hooks.c dir-lookup.c fsys-getroot.c fsys-hooks.c \
	fsys-startup.c hooks.c mdir.c nlist.c node-hooks.c rights.c \
	trans-help.c trans-start.c xinl.c
SRCS = $(OTHERSRCS) $(S_SRCS)

MIGSTUBS = fsServer.o ioServer.o fsysServer.o 
OBJS = $(sort $(SRCS:.c=.o)) $(MIGSTUBS)

MIGSFLAGS = -imacros fs-mutate.h
MIGCOMSFLAGS = -prefix treefs_
notify-MIGSFLAGS = -DSEQNOS

include ../Makeconf
