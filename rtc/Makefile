#   Makefile for rtc server
#
#   Copyright (C) 2024 Free Software Foundation, Inc.
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
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

dir := rtc
makemode := server

SRCS = main.c rtc-cmos_pioctl-ops.c
MIGSRCS = pioctlServer.c

OBJS = main.o pioctlServer.o rtc-cmos_pioctl-ops.o

HURDLIBS = trivfs shouldbeinlibc ports

target = rtc

include ../Makeconf

MIGCOMSFLAGS += -prefix rtc_
mig-sheader-prefix = rtc_
pioctl-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h

rtc_pioctl_S.h pioctlServer.c: mig-mutate.h
