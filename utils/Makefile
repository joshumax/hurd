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

SRCS = shd.c ps.c su.c settrans.c update.c clri.c sync.c reboot.c halt.c showtrans.c mount.sh fsysopts.c
targets = shd ps settrans showtrans update clri sync reboot halt su mount fsysopts
DIST_FILES = sh.ChangeLog

HURDLIBS=libps  # Only used by ps.  

CPPFLAGS += -I../lib
CPPFLAGS += $(CPPFLAGS-$(notdir $<))
vpath %.c ../lib

include ../Makeconf

settrans: settrans.o start-trans.o argz.o error.o
showtrans: showtrans.o argz.o error.o

fsysopts: fsysopts.o argz.o error.o

ps: ps.o ihash.o primes.o error.o $(HURDLIBS-files) $(libc)

CPPFLAGS-error.c = -Dprogram_name=program_invocation_name -DHAVE_VPRINTF -DSTDC_HEADERS -DHAVE_STRERROR
