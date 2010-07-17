# Makefile for daemons
#
#   Copyright (C) 1996, 1999, 2008, 2010 Free Software Foundation, Inc.
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
#

dir := daemons
makemode := utilities

targets = rc getty mail.local console-run runttys runsystem
special-targets = rc runsystem
SRCS = rc.sh runsystem.sh getty.c lmail.c console-run.c runttys.c
installationdir = $(libexecdir)

HURDLIBS = fshelp ports shouldbeinlibc
OBJS = $(SRCS:.c=.o)
getty-LDLIBS = -lutil

INSTALL-mail.local-ops = -o root -m 4755

include ../Makeconf

rc: rc.sh
getty: getty.o ../libshouldbeinlibc/libshouldbeinlibc.a
mail.local: lmail.o ../libshouldbeinlibc/libshouldbeinlibc.a
console-run: console-run.o ../libfshelp/libfshelp.a ../libports/libports.a

runttys: runttys.o
runttys-LDLIBS = -lutil

runsystem: runsystem.sh
