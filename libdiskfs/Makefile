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

dir := libdiskfs
makemode := library

libname = libdiskfs
FSSRCS= dir-link.c dir-mkdir.c dir-mkfile.c dir-lookup.c dir-rename.c \
	dir-rmdir.c dir-unlink.c file-chauthor.c file-chflags.c file-chown.c \
	file-get-trans.c file-get-transcntl.c file-getfh.c file-getlinknode.c \
	file-get-fs-opts.c \
	file-set-trans.c file-statfs.c file-sync.c file-syncfs.c \
	file-set-size.c file-utimes.c file-getcontrol.c file-lock.c \
	file-chmod.c dir-readdir.c file-lock-stat.c file-exec.c dir-chg.c \
	file-chg.c file-access.c file-inv-trans.c
IOSRCS= io-async-icky.c io-async.c io-duplicate.c io-get-conch.c \
	io-interrupt.c io-map-cntl.c io-map.c io-modes-get.c io-modes-off.c \
	io-modes-on.c io-modes-set.c io-owner-mod.c io-owner-get.c \
	io-pathconf.c io-prenotify.c io-read.c io-readable.c io-identity.c \
	io-reauthenticate.c io-rel-conch.c io-restrict-auth.c io-seek.c \
	io-select.c io-stat.c io-stubs.c io-write.c io-version.c io-sigio.c
FSYSSRCS=fsys-getroot.c fsys-goaway.c fsys-startup.c fsys-getfile.c \
	fsys-options.c fsys-syncfs.c fsys-forward.c
IFSOCKSRCS=ifsock.c
OTHERSRCS = conch-fetch.c conch-set.c dir-clear.c dir-init.c dir-renamed.c \
	node-create.c node-drop.c node-make.c node-rdwr.c node-update.c \
	peropen-make.c peropen-rele.c protid-make.c protid-rele.c \
	init-init.c init-startup.c init-first.c  \
	rdwr-internal.c boot-start.c demuxer.c node-times.c shutdown.c \
	sync-interval.c sync-default.c \
	opts-set.c opts-get.c opts-std-startup.c opts-runtime-parse.c \
	opts-runtime-unparse.c opts-common.c \
	trans-callback.c readonly.c remount.c filedev.c machdev.c \
	dev-open.c dev-io.c dev-globals.c console.c disk-pager.c \
	name-cache.c direnter.c dirrewrite.c dirremove.c lookup.c dead-name.c
SRCS = $(OTHERSRCS) $(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(IFSOCKSRCS)
LCLHDRS = diskfs.h priv.h lithp.h fsmutations.h
installhdrs = diskfs.h diskfs-pager.h
DIST_FILES = ourfs_notify.defs

MIGSTUBS = fsServer.o ioServer.o fsysServer.o exec_startupServer.o \
	fsys_replyUser.o ourfs_notifyUser.o ifsockServer.o \
	startup_notifyServer.o
OBJS = $(sort $(SRCS:.c=.o) $(MIGSTUBS))

fsys-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h -DREPLY_PORTS
fs-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
io-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
ifsock-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
MIGCOMSFLAGS = -prefix diskfs_

include ../Makeconf

libdiskfs.so: $(foreach lib,threads ports fshelp shouldbeinlibc iohelp,\
			../lib$(lib)/lib$(lib).so)
