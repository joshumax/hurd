# Copyright (C) 1997,99,2001,02 Free Software Foundation, Inc.
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

SRCS = bootstrap.c ffs_compat.c load.c wiring.c \
	ffs_file_io.c minix_ffs_compat.c file_io.c \
	minix_file_io.c ext2_file_io.c strfcns.c exec.c \
	panic.c elf-load.c gunzip.c bunzip2.c
LCLHDRS = assert.h disk_inode_ffs.h fs.h queue.h defs.h \
	 minix_ffs_compat.h wiring.h dir.h ffs_compat.h minix_fs.h \
	disk_inode.h file_io.h minix_super.h mach-exec.h
EXTRA_DIST = def_pager_setup.c default_pager.c kalloc.c
target = serverboot
HURDLIBS = threads
installationdir = $(bootdir)

UNZIP_OBJS = unzip.o inflate.o util.o do-bunzip2.o
OBJS = $(subst .c,.o,$(SRCS)) \
       boot_script.o userland-boot.o \
       $(UNZIP_OBJS)

include ../Makeconf

vpath boot_script.c $(srcdir)/../boot
vpath userland-boot.c $(srcdir)/../boot

# Look for zip stuff
vpath unzip.c $(srcdir)/../exec
vpath inflate.c $(srcdir)/../exec
vpath util.c $(srcdir)/../exec
vpath do-bunzip2.c $(srcdir)/../exec

# If SMALL_BZIP2 is defined, use relatively small memory.
# It's crucial for serverboot, because swap is not enabled yet.
CPPFLAGS += -I$(srcdir)/../exec -DGZIP -DBZIP2 -DSMALL_BZIP2

LDFLAGS += -static

# Don't even bother.
CFLAGS := $(filter-out -Wall,$(CFLAGS))
