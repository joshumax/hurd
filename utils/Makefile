# 
#   Copyright (C) 1994, 1995, 1996, 1997 Free Software Foundation
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

targets = shd ps settrans showtrans sync su mount fsysopts \
	storeinfo login w uptime hurdids loginpr sush vmstat portinfo \
	devprobe vminfo addauth
special-targets = mount loginpr sush uptime
SRCS = shd.c ps.c su.c settrans.c sync.c showtrans.c addauth.c \
	mount.sh fsysopts.c storeinfo.c login.c loginpr.sh sush.sh w.c \
	uptime.sh psout.c hurdids.c vmstat.c portinfo.c devprobe.c vminfo.c \
	parse.c
LCLHDRS = psout.h parse.h

OBJS = $(filter-out loginpr.sh mount.sh sush.sh uptime.sh,$(SRCS:.c=.o))
HURDLIBS = ps ihash store fshelp ports threads shouldbeinlibc
login-LDLIBS = -lutil $(LIBCRYPT)
addauth-LDLIBS = $(LIBCRYPT)

INSTALL-login-ops = -o root -m 4755
INSTALL-addauth-ops = -o root -m 4755
INSTALL-hurdids-ops = -o root -m 4755
INSTALL-ps-ops = -o root -m 4755
INSTALL-w-ops = -o root -m 4755
INSTALL-vmstat-ops = -o root -m 4755
INSTALL-vminfo-ops = -o root -m 4755

include ../Makeconf

ps addauth: parse.o
ps w addauth: psout.o ../libps/libps.a ../libihash/libihash.a
storeinfo: ../libstore/libstore.a
# We must include libthreads because of a bug in the way shared libraries
# work: all libraries that *any* routine in libfshelp uses must be defined.
settrans: ../libfshelp/libfshelp.a ../libports/libports.a ../libthreads/libthreads.a
ps w hurdids settrans sync showtrans fsysopts storeinfo login vmstat portinfo \
  devprobe vminfo addauth: ../libshouldbeinlibc/libshouldbeinlibc.a

$(filter-out $(special-targets), $(targets)): %: %.o
