# Makefile for mach-defpager subdirectory of hurd sources
#
#   Copyright (C) 1999 Free Software Foundation, Inc.
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

dir	:= mach-defpager
makemode:= server
target	:= mach-defpager

SRCS	:= default_pager.c def_pager_setup.c \
	   kalloc.c wiring.c main.c \
	   file_io.c strfcns.c \
	   ext2_file_io.c ffs_file_io.c ffs_compat.c \
	   minix_file_io.c minix_ffs_compat.c

OBJS = $(SRCS:.c=.o) \
       $(addsuffix Server.o,memory_object default_pager memory_object_default \
                            exc bootstrap)

HURDLIBS:= threads
LDFLAGS += -static

vpath %.c $(srcdir)/../serverboot
CPPFLAGS += -I$(srcdir)/../serverboot

MIGSFLAGS = -DSEQNOS

include ../Makeconf

# Don't even bother.
CFLAGS := $(filter-out -Wall,$(CFLAGS))
