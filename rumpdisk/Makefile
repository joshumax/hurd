#
#   Copyright (C) 2019 Free Software Foundation, Inc.
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

RUMPPATH=/usr/lib
RUMPLIBS=rump rumpuser rumpdev rumpdev_disk rumpdev_pci rumpvfs rumpdev_ahcisata
RUMPEXTRA=rumpdev_scsipi

# If we have a configured tree, include the configuration so that we
# can conditionally build translators.
ifneq (,$(wildcard ../config.make))
 include ../config.make
endif

ifeq ($(HAVE_LIBRUMP_VFSNOFIFO),yes)
RUMPLIBS += rumpvfs_nofifofs
endif

dir := rumpdisk
makemode := server

SRCS = main.c block-rump.c
LCLHDRS = block-rump.h ioccom-rump.h
target = rumpdisk
OBJS = $(SRCS:.c=.o)
HURDLIBS = machdev ports trivfs shouldbeinlibc iohelp ihash fshelp
LDLIBS += -lpthread -lpciaccess -ldl
LDLIBS += -Wl,--whole-archive $(RUMPLIBS:%=$(RUMPPATH)/lib%_pic.a) -Wl,--no-whole-archive $(RUMPEXTRA:%=$(RUMPPATH)/lib%_pic.a)

include ../Makeconf
