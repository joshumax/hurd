# 
#   Copyright (C) 1994, 1995 Free Software Foundation
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

dir := utils
makemode := utilities

targets = shd ps settrans showtrans clri sync reboot halt su mount fsysopts
special-targets = mount
SRCS = shd.c ps.c su.c settrans.c clri.c sync.c reboot.c halt.c showtrans.c mount.sh fsysopts.c
DIST_FILES = shd.ChangeLog

OBJS = $(filter-out mount.sh,$(SRCS:.c=.o))

all: $(targets)

ps: ../libps/libps.a ../libihash/libihash.a
settrans: start-trans.o
ps settrans showtrans fsysopts: ../libshouldbeinlibc/libshouldbeinlibc.a

CPPFLAGS += -I../lib
vpath %.c ../lib

include ../Makeconf

mount: mount.sh
	cp $< $@
