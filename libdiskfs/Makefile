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

dir := libdiskfs

include ../Makeconf

FSSRCS= dir-link.c dir-mkdir.c dir-mkfile.c dir-pathtrans.c dir-rename.c    \
	dir-rmdir.c dir-unlink.c file-chauthor.c file-chflags.c file-chown.c \
	file-get-trans.c file-get-transcntl.c file-getfh.c file-getlinknode.c \
	file-pathconf.c file-set-trans.c file-statfs.c file-sync.c 	    \
	file-syncfs.c file-truncate.c file-utimes.c file-getcontrol.c       \
	file-lock.c file-chmod.c dir-readdir.c file-lock-stat.c file-exec.c \
	dir-chg.c file-chg.c file-access.c file-inv-trans.c

IOSRCS= io-async-icky.c io-async.c io-duplicate.c io-get-conch.c 	    \
	io-interrupt.c io-map-cntl.c io-map.c io-modes-get.c io-modes-off.c \
	io-modes-on.c io-modes-set.c io-owner-mod.c io-owner-get.c 	    \
	io-prenotify.c io-read.c io-readable.c io-reauthenticate.c	    \
	io-rel-conch.c io-restrict-auth.c io-seek.c io-select.c io-stat.c   \
	io-stubs.c io-write.c io-version.c io-sigio.c

FSYSSRCS=fsys-getroot.c fsys-goaway.c fsys-startup.c fsys-getfile.c

IFSOCKSRCS=ifsock.c

NOTIFYSRCS=notify-nosenders.c notify-stubs.c

INTSRCS=interrupt.c

OTHERSRCS = conch-fetch.c conch-set.c dir-clear.c dir-init.c dir-renamed.c  \
	node-create.c node-drop.c node-make.c node-rdwr.c node-update.c     \
	peropen-make.c peropen-rele.c protid-make.c protid-rele.c	    \
	rdwr-internal.c trans-destroy.c trans-start.c init-init.c           \
	init-first.c init-loop.c boot-parse.c boot-start.c ports-clean.c    \
	ports-consts.c pager-consts.c ports-demuxer.c exc.c node-times.c    \
	trans-sync.c shutdown.c ports-noports.c ports-soft.c ports-idle.c

SRCS = $(NOTIFYSRCS) $(OTHERSRCS) $(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(INTSRCS) \
	$(IFSOCKSRCS)

TAGSHDRS = diskfs.h priv.h lithp.h fsmutations.h

MIGSTUBS = fsServer.o ioServer.o fsysServer.o notifyServer.o execServer.o \
	interruptServer.o fsys_replyUser.o msgUser.o ifsockServer.o

OBJS = $(subst .c,.o,$(SRCS)) $(MIGSTUBS)

DIST_FILES = $(SRCS) Makefile diskfs.h priv.h lithp.h fsmutations.h ChangeLog


MIGSFLAGS=-imacros fsmutations.h
CPPFLAGS+=-DMAKING_DISKFS

all: libdiskfs.a

libdiskfs.a: $(sort $(OBJS)) 
	rm -f libdiskfs.a
	$(AR) r $@ $^

$(hurdinst)/lib/libdiskfs.a: libdiskfs.a
	cp libdiskfs.a $@
	$(RANLIB) $@

$(includedir)/hurd/diskfs.h: diskfs.h $(includedir)/hurd/machine/diskfs_machdep.h
	cp $< $@

$(includedir)/hurd/machine/diskfs_machdep.h: ../machine/diskfs_machdep.h
	cp ../machine/diskfs_machdep.h $@

install: $(hurdinst)/lib/libdiskfs.a $(includedir)/hurd/diskfs.h

clean:
	rm -f *.o libdiskfs.a *_S.h *Server.h *Server.c fsys_reply.h *User.c


$(subst .c,.o,$(FSSRCS)): fs_S.h
dir-readdir.o $(subst .c,.o,$(IOSRCS)): io_S.h
$(subst .c,.o,$(FSYSSRCS)) boot-start.o: fsys_S.h
boot-start.o: fsys_reply.h
$(subst .c,.o,$(NOTIFYSRCS)): notify_S.h
$(subst .c,.o,$(INTSRCS)): interrupt_S.h
$(subst .c,.o,$(IFSOCKSRCS)): ifsock_S.h
dir-chg.o: msg.h

notify-nosenders.o ports-clean.o: $(includedir)/hurd/pager.h

$(OBJS): priv.h diskfs.h $(includedir)/hurd/ioserver.h $(includedir)/hurd/ports.h \
			 $(includedir)/hurd/fshelp.h
file-chauthor.o: lithp.h

fs_S.h fsServer.c: $(includedir)/hurd/fs.defs $(includedir)/hurd/hurd_types.defs \
		fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/fs.defs \
	| $(MIGCOM) -sheader fs_S.h -header /dev/null -user /dev/null

io_S.h ioServer.c: $(includedir)/hurd/io.defs $(includedir)/hurd/hurd_types.defs \
		fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/io.defs \
	| $(MIGCOM) -sheader io_S.h -header /dev/null -user /dev/null

fsys_S.h fsysServer.c: $(includedir)/hurd/fsys.defs \
		$(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/fsys.defs \
	| $(MIGCOM) -sheader fsys_S.h -header /dev/null -user /dev/null

ifsock_S.h ifsockServer.c: $(includedir)/hurd/ifsock.defs \
		$(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/ifsock.defs \
	| $(MIGCOM) -sheader ifsock_S.h -header /dev/null -user /dev/null

notify_S.h notifyServer.c: $(includedir)/mach/notify.defs
	$(CPP) $(CPPFLAGS) -DSEQNOS $(includedir)/mach/notify.defs \
	| $(MIGCOM) -prefix diskfs_ \
		 -sheader notify_S.h -header /dev/null -user /dev/null

exec_S.h execServer.c: $(includedir)/hurd/exec.defs \
		$(includedir)/hurd/hurd_types.defs fsmutations.h
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/exec.defs \
	| $(MIGCOM) -sheader exec_S.h -header /dev/null -user /dev/null

interrupt_S.h interruptServer.c: $(includedir)/hurd/interrupt.defs \
		$(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) $(MIGSFLAGS) $(includedir)/hurd/interrupt.defs \
	| $(MIGCOM) -sheader interrupt_S.h -header /dev/null -user /dev/null

fsys_reply.h fsys_replyUser.c: $(includedir)/hurd/fsys_reply.defs $(hurd/hurd_types.defs)
	$(CPP) $(CPPFLAGS) $(includedir)/hurd/fsys_reply.defs \
	| $(MIGCOM) -server /dev/null

# The reason for -Dout= is to prevent errors for get_init_port,
# get_init_ports, get_init_int, get_init_ints, get_dtable, and get_fd.
# We don't use those, so we're safe in breaking them.
msg.h msgUser.c: $(includedir)/hurd/msg.defs $(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -Droutine=simpleroutine -Dout= $(includedir)/hurd/msg.defs \
	| $(MIGCOM) -prefix nowait_ -server /dev/null
