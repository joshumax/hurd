# 
#   Copyright (C) 1994, 1995, 1996 Free Software Foundation
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

targets = shd ps settrans showtrans sync reboot halt su mount fsysopts \
	storeinfo login w uptime hurdids loginpr sush vmstat portinfo \
	devprobe
special-targets = mount loginpr sush uptime
SRCS = shd.c ps.c su.c settrans.c sync.c reboot.c halt.c showtrans.c \
	mount.sh fsysopts.c storeinfo.c login.c loginpr.sh sush.sh w.c \
	uptime.sh psout.c hurdids.c vmstat.c portinfo.c devprobe.c
DIST_FILES = shd.ChangeLog

OBJS = $(filter-out loginpr.sh mount.sh sush.sh uptime.sh,$(SRCS:.c=.o))

all: $(targets)

# This hack to get around ickiness in Makeconf
libutil-libsubst = -lutil

login: /gd4/hurdinst/lib/libutil.a	# ick ick ick ick
ps w: psout.o ../libps/libps.a ../libihash/libihash.a
# We must include libthreads because of a bug in the way shared libraries
# work: all libraries that *any* routine in libfshelp uses must be defined.
settrans: ../libfshelp/libfshelp.a ../libthreads/libthreads.a
ps w hurdids settrans showtrans fsysopts storeinfo login vmstat portinfo \
  devprobe: ../libshouldbeinlibc/libshouldbeinlibc.a

include ../Makeconf

%: %.sh
	cp $< $@
