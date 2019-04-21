#   Copyright (C) 2018 Free Software Foundation, Inc.
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
#  along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.

dir            = acpi
makemode       = server

PORTDIR = $(srcdir)/port

SRCS           = main.c netfs_impl.c acpi.c \
                 acpifs.c ncache.c options.c func_files.c
MIGSRCS        =
OBJS           = $(patsubst %.S,%.o,$(patsubst %.c,%.o, $(SRCS) $(MIGSRCS)))

HURDLIBS= fshelp ports shouldbeinlibc netfs iohelp ihash
LDLIBS = -lpthread

target = acpi

include ../Makeconf

CFLAGS += -I$(PORTDIR)/include
