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
makemode := library

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

MIGSTUBS=fsServer.o ioServer.o fsysServer.o interruptServer.o notifyServer.o \
	fsys_replyUser.o

OBJS= $(sort $(subst .c,.o,$(SRCS)) $(MIGSTUBS))
LCLHDRS = trivfs.h fsmutations.h priv.h
REMHDRS = ../hurd/ports.h
MIGSFLAGS=-imacros fsmutations.h
MIGCOMSFLAGS = -prefix trivfs_
libname = libtrivfs
installhdrs = trivfs.h

include ../Makeconf

$(subst .c,.o,$(FSSRCS)): fs_S.h
$(subst .c,.o,$(IOSRCS)): io_S.h
$(subst .c,.o,$(FSYSSRCS)): fsys_S.h
$(subst .c,.o,$(NOTIFYSRCS)): notify_S.h
$(subst .c,.o,$(INTSRCS)): interrupt_S.h
fsys-getroot.c: fsys_reply_U.h
$(OBJS): trivfs.h ../hurd/ports.h

