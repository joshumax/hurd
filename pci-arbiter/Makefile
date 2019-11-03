#   Copyright (C) 2017 Free Software Foundation, Inc.
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
#  You should have received a copy of the GNU General Public License
#  along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.

dir		= pci-arbiter
makemode	= server

PORTDIR = $(srcdir)/port

SRCS		= main.c pci-ops.c netfs_impl.c \
		  pcifs.c ncache.c options.c func_files.c startup.c \
		  startup-ops.c
MIGSRCS		= pciServer.c startup_notifyServer.c
OBJS		= $(patsubst %.S,%.o,$(patsubst %.c,%.o, $(SRCS) $(MIGSRCS)))

HURDLIBS= fshelp ports shouldbeinlibc netfs iohelp ihash
LDLIBS = -lpthread $(libpciaccess_LIBS)

target = pci-arbiter

include ../Makeconf

CFLAGS += -I$(PORTDIR)/include $(libpciaccess_CFLAGS)

pci-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h

# cpp doesn't automatically make dependencies for -imacros dependencies. argh.
pci_S.h pciServer.c: mig-mutate.h
