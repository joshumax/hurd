
#   Copyright (C) 1994 Free Software Foundation
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

dir := ufs

include ../Makeconf

VPATH=.:../machine

SRCS=alloc.c consts.c devio.c dir.c hyper.c inode.c main.c pager.c \
	sizes.c subr.c tables.c 

DIST_FILES=$(SRCS) ufs.h Makefile fs.h dinode.h dir.h ChangeLog

OBJS=$(subst .c,.o,$(SRCS)) exec_server_image.o

#LIBS = $(hurdinst)/lib/libdiskfscombined.a $(libthreads)
LIBS = $(libdiskfs) $(libports) $(libdiskfs) $(libpager) $(libioserver) \
	 $(libfshelp) $(libdiskfs) $(libthreads) $(libports)

LIBPARTS = $(libdiskfs) $(libports) $(libpager) $(libioserver) $(libfshelp)

all: ufs

LDFLAGS=--no-keep-memory

ufs: $(OBJS) $(LIBS)
	$(link)

exec_server_image.o: ../exec/exec ../mkbootfs/mkbootfs
	rsh -n $(mighost) cd `pwd` \; \
	../mkbootfs/mkbootfs ../exec/exec exec_server_image.o

install:

clean:
	rm -f *.o ufs
relink:
	rm -f ufs

$(OBJS): ufs.h 
$(OBJS): $(addprefix $(includedir)/hurd/,diskfs.h pager.h ioserver.h \
				         fshelp.h ports.h)
alloc.o: fs.h dinode.h
consts.o: dinode.h
dir.o: dir.h
hyper.o: fs.h
inode.o: dinode.h fs.h
main.o: fs.h
pager.o: fs.h dinode.h
sizes.o: fs.h dinode.h
subr.o: fs.h
tables.o: fs.h

$(hurdinst)/lib/libdiskfscombined.a: $(LIBPARTS)
	-mkdir foo
	cd foo; $(AR) x $(libdiskfs)
	cd foo; $(AR) x $(libports)
	cd foo; $(AR) x $(libpager)
	cd foo; $(AR) x $(libioserver)
	cd foo; $(AR) x $(libfshelp)
	cd foo; $(AR) cr $(hurdinst)/lib/libdiskfscombined.a *
	rm -rf foo
	$(RANLIB) $(hurdinst)/lib/libdiskfscombined.a

$(foreach dir,mkbootfs exec,../$(dir)/%): FORCE
	$(MAKE) -C $(@D) $(@F)
