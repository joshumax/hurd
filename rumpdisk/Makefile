#
#   Copyright (C) 2019, 2023, 2026 Free Software Foundation, Inc.
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
RUMPSATA=rumpdev_ahcisata rumpdev_piixide rumpdev_ata rumpdev_nvme rumpdev_ld_nvme rumpdev_ld
RUMPUSB=rumpdev_usb rumpdev_pci_usbhc rumpdev_umass rumpdev_scsipi
RUMPEXTRA=rumpdev_scsipi

# If we have a configured tree, include the configuration so that we
# can conditionally build translators.
ifneq (,$(wildcard ../config.make))
 include ../config.make
endif

# TODO: install .so version of rumpvfs_nofifofs so we don't have to do this!
ifeq ($(HAVE_LIBRUMP_VFSNOFIFO),yes)
RUMPSTATIC=-Wl,--whole-archive -lrumpvfs_nofifofs_pic -Wl,--no-whole-archive
else
RUMPSTATIC=
endif

dir := rumpdisk
makemode := servers

SRCS = main.c block-rump.c
LCLHDRS = block-rump.h ioccom-rump.h
targets = rumpdisk rumpusbdisk
HURDLIBS = machdev ports trivfs shouldbeinlibc iohelp ihash fshelp irqhelp
LDLIBS += -lpthread -lpciaccess -ldl -lz

%.disk.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_RUMP_SATA -c $< -o $@
rumpdisk-OBJS = $(SRCS:.c=.disk.o)
rumpdisk-LDLIBS += $(HURDLIBS:%=-l%) $(RUMPSTATIC) $(RUMPEXTRA:%=-l%) \
		-Wl,--no-as-needed $(RUMPSATA:%=-l%) $(RUMPLIBS:%=-l%) -Wl,--as-needed
rumpdisk.static-LDLIBS += $(HURDLIBS:%=-l%) $(RUMPSTATIC) $(RUMPEXTRA:%=-l%_pic) \
		-Wl,--whole-archive $(RUMPSATA:%=-l%_pic) $(RUMPLIBS:%=-l%_pic) -Wl,--no-whole-archive
rumpdisk rumpdisk.static: $(rumpdisk-OBJS)

%.usb.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
rumpusbdisk-OBJS = $(SRCS:.c=.usb.o)
rumpusbdisk-LDLIBS += $(HURDLIBS:%=-l%) $(RUMPSTATIC) \
		-Wl,--no-as-needed $(RUMPUSB:%=-l%) $(RUMPLIBS:%=-l%) -Wl,--as-needed
rumpusbdisk.static-LDLIBS += $(HURDLIBS:%=-l%) $(RUMPSTATIC) \
		-Wl,--whole-archive $(RUMPUSB:%=-l%_pic) $(RUMPLIBS:%=-l%_pic) -Wl,--no-whole-archive
rumpusbdisk rumpusbdisk.static: $(rumpusbdisk-OBJS)

include ../Makeconf
