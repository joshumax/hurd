# Copyright (C) 1991 Free Software Foundation, Inc.
# This file is part of the GNU Hurd.
# 
# The GNU Hurd is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 1, or (at your option)
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

GCC_EXEC_PREFIX=/usr/local/lib/gcc-
MK_INCLUDES=/usr1/mk/release/i386_mach/latest/include
CC=gcc
MACHSA_LIB=/usr1/mk/release/i386_mach/latest/lib/libmach_sa.a
MIGCOM=../mig/migcom
CTHREAD_LIB=/usr1/mk/release/i386_mach/latest/lib/libthreads.a
CPPFLAGS=-nostdinc -I. -I.. -I${MK_INCLUDES} \
	 -D__USE_GNU -D__GNU_SYSTEM
CFLAGS=-Wall -O -g

AUTHMIGOPTS="-DAUTH_INTRAN=idblockptr_t convert_auth_to_idblock (auth_t)"

OFILES=auth.o authServer.o auth_replyUser.o

auth.o: auth.h auth_reply.h

auth.h authServer.c: ../hurd/auth.defs ../hurd/hurd_types.defs
	$(GCC_EXEC_PREFIX)cpp $(CPPFLAGS) $(AUTHMIGOPTS) ../hurd/auth.defs \
	| $(MIGCOM) -sheader auth.h -header /dev/null -user /dev/null

auth_reply.h auth_replyUser.c: ../hurd/auth_reply.defs ../hurd/hurd_types.defs
	$(GCC_EXEC_PREFIX)cpp $(CPPFLAGS) ../hurd/auth_reply.defs \
	| $(MIGCOM) -server /dev/null
