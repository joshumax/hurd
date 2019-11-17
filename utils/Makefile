#   Copyright (C) 1994,95,96,97,98,99,2000,01,02,12,14 Free Software Foundation,
#   Inc.
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

targets = shd ps settrans showtrans syncfs fsysopts \
	storeinfo login w uptime ids loginpr sush vmstat portinfo \
	devprobe vminfo addauth rmauth unsu setauth ftpcp ftpdir storecat \
	storeread msgport rpctrace mount gcore fakeauth fakeroot remap \
	umount nullauth rpcscan vmallocate

special-targets = loginpr sush uptime fakeroot remap
SRCS = shd.c ps.c settrans.c syncfs.c showtrans.c addauth.c rmauth.c \
	fsysopts.c storeinfo.c login.c loginpr.sh sush.sh w.c \
	uptime.sh psout.c ids.c vmstat.c portinfo.c devprobe.c vminfo.c \
	parse.c frobauth.c frobauth-mod.c setauth.c pids.c nonsugid.c \
	unsu.c ftpcp.c ftpdir.c storeread.c storecat.c msgport.c \
	rpctrace.c mount.c gcore.c fakeauth.c fakeroot.sh remap.sh \
	nullauth.c match-options.c msgids.c rpcscan.c

OBJS = $(filter-out %.sh,$(SRCS:.c=.o))
HURDLIBS = ps ihash store fshelp ports ftpconn shouldbeinlibc
LDLIBS += -lpthread
login-LDLIBS = -lutil $(and $(HAVE_LIBCRYPT),-lcrypt)
addauth-LDLIBS = $(and $(HAVE_LIBCRYPT),-lcrypt)
setauth-LDLIBS = $(and $(HAVE_LIBCRYPT),-lcrypt)
mount-LDLIBS = $(libblkid_LIBS)
mount-CPPFLAGS = $(libblkid_CFLAGS)

INSTALL-login-ops = -o root -m 4755
INSTALL-ids-ops = -o root -m 4755
INSTALL-ps-ops = -o root -m 4755
INSTALL-w-ops = -o root -m 4755

include ../Makeconf

ps addauth rmauth setauth unsu msgport: parse.o pids.o
login addauth setauth: nonsugid.o
addauth rmauth setauth unsu: frobauth.o
rmauth setauth unsu: frobauth-mod.o
ps w: psout.o ../libps/libps.a ../libihash/libihash.a
portinfo: ../libihash/libihash.a ../libps/libps.a

storeinfo storecat storeread: ../libstore/libstore.a
ftpcp ftpdir: ../libftpconn/libftpconn.a
mount umount: ../libihash/libihash.a
settrans: ../libfshelp/libfshelp.a ../libihash/libihash.a \
	../libports/libports.a
ps w ids settrans syncfs showtrans fsysopts storeinfo login vmstat portinfo \
  devprobe vminfo addauth rmauth setauth unsu ftpcp ftpdir storeread \
  storecat msgport mount umount nullauth rpctrace: \
	../libshouldbeinlibc/libshouldbeinlibc.a

$(filter-out $(special-targets), $(targets)): %: %.o

rpctrace: ../libports/libports.a
rpctrace rpcscan: msgids.o \
	  ../libihash/libihash.a \
	  ../libshouldbeinlibc/libshouldbeinlibc.a
msgids-CPPFLAGS = -DDATADIR=\"${datadir}\"

fakeauth: authServer.o auth_requestUser.o interruptServer.o \
	  ../libports/libports.a ../libihash/libihash.a \
	  ../libshouldbeinlibc/libshouldbeinlibc.a
auth-MIGSFLAGS = -imacros $(srcdir)/../auth/mig-mutate.h
fakeauth-CPPFLAGS = -I$(srcdir)/../auth
authServer-CPPFLAGS = -I$(srcdir)/../auth
auth_requestUser-CPPFLAGS = -I$(srcdir)/../auth

mount umount: ../sutils/fstab.o ../sutils/clookup.o match-options.o \
       $(foreach L,fshelp ports,../lib$L/lib$L.a)
../sutils/fstab.o ../sutils/clookup.o: FORCE
	$(MAKE) -C $(@D) $(@F)
FORCE:

shd vmallocate: ../libshouldbeinlibc/libshouldbeinlibc.a
