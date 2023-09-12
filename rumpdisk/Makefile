#
#   Copyright (C) 2019, 2023 Free Software Foundation, Inc.
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

RUMPLIBS=rump rumpuser rumpdev rumpdev_disk rumpdev_pci rumpvfs
RUMPSATA=rumpdev_ahcisata rumpdev_piixide rumpdev_ata
RUMPUSB=rumpdev_usb rumpdev_pci_usbhc rumpdev_umass
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
makemode := servers

SRCS = main.c block-rump.c
LCLHDRS = block-rump.h ioccom-rump.h
targets = rumpdisk rumpusbdisk
HURDLIBS = machdev ports trivfs shouldbeinlibc iohelp ihash fshelp
LDLIBS += -lpthread -lpciaccess -ldl $(RUMPEXTRA:%=-l%_pic) \
	  -Wl,--whole-archive $(RUMPLIBS:%=-l%_pic) -Wl,--no-whole-archive

%.disk.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_RUMP_SATA -c $< -o $@
rumpdisk-OBJS = $(SRCS:.c=.disk.o)
rumpdisk-LDLIBS += -Wl,--whole-archive $(RUMPSATA:%=-l%_pic) -Wl,--no-whole-archive $(HURDLIBS:%=-l%)
rumpdisk rumpdisk.static: $(rumpdisk-OBJS)

%.usb.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
rumpusbdisk-OBJS = $(SRCS:.c=.usb.o)
rumpusbdisk-LDLIBS += -Wl,--whole-archive $(RUMPUSB:%=-l%_pic) -Wl,--no-whole-archive $(HURDLIBS:%=-l%)
rumpusbdisk rumpusbdisk.static: $(rumpusbdisk-OBJS)

include ../Makeconf
