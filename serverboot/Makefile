# Copyright (C) 1997 Free Software Foundation, Inc.
# This file is part of the GNU Hurd.
#
# The GNU Hurd is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The GNU Hurd is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with the GNU Hurd; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

dir := serverboot
makemode := utility

SRCS = bootstrap.c ffs_compat.c load.c wiring.c def_pager_setup.c \
	ffs_file_io.c minix_ffs_compat.c default_pager.c file_io.c\
	minix_file_io.c ext2_file_io.c kalloc.c strfcns.c exec.c \
	translate_root.c panic.c elf-load.c
OBJS = $(subst .c,.o,$(SRCS)) boot_script.o memory_objectServer.o \
	default_pagerServer.o excServer.o bootstrapServer.o \
	memory_object_defaultServer.o
LCLHDRS = assert.h disk_inode_ffs.h fs.h queue.h defs.h ext2_fs.h \
	 minix_ffs_compat.h wiring.h dir.h ffs_compat.h minix_fs.h \
	disk_inode.h file_io.h minix_super.h translate_root.h mach-exec.h
target = serverboot
HURDLIBS = threads
installationdir = $(prefix)/boot

vpath boot_script.c $(srcdir)/../boot

MIGSFLAGS = -DSEQNOS

LDFLAGS += -static

include ../Makeconf

# Don't even bother.
CFLAGS := $(filter-out -Wall,$(CFLAGS))