# 
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

dir := libtrivfs

include ../Makeconf

FSSRCS= dir-link.c dir-mkdir.c dir-mkfile.c dir-pathtrans.c \
	dir-readdir.c dir-rename.c dir-rmdir.c dir-unlink.c file-chauthor.c \
	file-chflags.c file-chmod.c file-chown.c file-get-trans.c \
	file-get-transcntl.c file-getcontrol.c file-getfh.c file-getlinknode.c\
	file-lock.c file-pathconf.c file-set-trans.c file-statfs.c \
	file-sync.c file-syncfs.c file-truncate.c file-utimes.c file-exec.c \
	file-access.c dir-chg.c file-chg.c file-inv.c

IOSRCS=io-async-icky.c \
	io-async.c io-duplicate.c io-map.c io-modes-get.c io-modes-off.c \
	io-modes-on.c io-modes-set.c io-owner-get.c io-owner-mod.c io-read.c \
	io-readable.c io-reauthenticate.c io-restrict-auth.c io-seek.c \
	io-select.c io-stat.c io-stubs.c io-write.c io-version.c

FSYSSRCS=fsys-getroot.c fsys-goaway.c fsys-stubs.c

NOTIFYSRCS=nosenders.c notify-stubs.c

INTSRCS=interrupt.c

OTHERSRCS=demuxer.c handle-port.c protid-clean.c cntl-clean.c migsupport.c \
	times.c

SRCS=$(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(NOTIFYSRCS) $(INTSRCS) $(OTHERSRCS)

TAGSHDRS = trivfs.h fsmutations.h

MIGSTUBS=fsServer.o ioServer.o fsysServer.o interruptServer.o notifyServer.o \
	fsys_replyUser.o

OBJS= $(subst .c,.o,$(SRCS)) $(MIGSTUBS)

DIST_FILES= $(SRCS) Makefile ChangeLog trivfs.h fsmutations.h priv.h

MIGSFLAGS=-imacros fsmutations.h

all: libtrivfs.a

libtrivfs.a: $(sort $(OBJS))
	rm -f libtrivfs.a
	$(AR) r $@ $^

$(hurdinst)/lib/libtrivfs.a: libtrivfs.a
	cp libtrivfs.a $@
	$(RANLIB) $@

$(includedir)/hurd/trivfs.h: trivfs.h
	cp $< $@

install: $(hurdinst)/lib/libtrivfs.a $(includedir)/hurd/trivfs.h

clean:
	rm -f *.o libtrivfs.a *_S.h *Server.c fsys_reply.h *User.c

$(subst .c,.o,$(FSSRCS)): fs_S.h
$(subst .c,.o,$(IOSRCS)): io_S.h
$(subst .c,.o,$(FSYSSRCS)): fsys_S.h
$(subst .c,.o,$(NOTIFYSRCS)): notify_S.h
$(subst .c,.o,$(INTSRCS)): interrupt_S.h
fsys-getroot.c: fsys_reply.h

$(OBJS): trivfs.h $(includedir)/hurd/ports.h

fs_S.h fsServer.c: $(includedir)/hurd/fs.defs $(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/fs.defs \
	| $(MIGCOM) -sheader fs_S.h -header /dev/null -user /dev/null
	sed -e 's/fs_server/trivfs_fs_server/' < fsServer.c > tmp.c
	mv tmp.c fsServer.c

io_S.h ioServer.c: $(includedir)/hurd/io.defs $(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/io.defs \
	| $(MIGCOM) -sheader io_S.h -header /dev/null -user /dev/null
	sed -e 's/io_server/trivfs_io_server/' < ioServer.c > tmp.c
	mv tmp.c ioServer.c

fsys_S.h fsysServer.c: $(includedir)/hurd/fsys.defs \
		$(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/fsys.defs \
	| $(MIGCOM) -sheader fsys_S.h -header /dev/null -user /dev/null
	sed -e 's/fsys_server/trivfs_fsys_server/' < fsysServer.c > tmp.c
	mv tmp.c fsysServer.c

fsys_reply.h fsys_replyUser.c: $(includedir)/hurd/fsys_reply.defs \
		$(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) $(includedir)/hurd/fsys_reply.defs \
	| $(MIGCOM) -server /dev/null

notify_S.h notifyServer.c: $(includedir)/mach/notify.defs
	$(CPP) $(CPPFLAGS) $(includedir)/mach/notify.defs \
	| $(MIGCOM) -prefix trivfs_ \
		 -sheader notify_S.h -header /dev/null -user /dev/null
	sed -e 's/notify_server/trivfs_notify_server/' < notifyServer.c > tmp.c
	mv tmp.c notifyServer.c

interrupt_S.h interruptServer.c: $(includedir)/hurd/interrupt.defs \
		$(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/interrupt.defs \
	| $(MIGCOM) -sheader interrupt_S.h -header /dev/null -user /dev/null
	sed -e 's/interrupt_server/trivfs_interrupt_server/' < interruptServer.c > tmp.c
	mv tmp.c interruptServer.c
