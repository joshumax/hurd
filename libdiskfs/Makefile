#   Copyright (C) 1994,95,96,97,98,99,2000,01,2006,2012,2016-2019
#     Free Software Foundation, Inc.
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
#   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.

dir := libdiskfs
makemode := library

libname = libdiskfs
FSSRCS= dir-chg.c dir-link.c dir-lookup.c dir-mkdir.c dir-mkfile.c \
	dir-readdir.c dir-rename.c dir-rmdir.c dir-unlink.c \
	file-access.c file-chauthor.c file-chflags.c file-chg.c \
	file-chmod.c file-chown.c file-exec.c file-get-fs-opts.c \
	file-get-trans.c file-get-transcntl.c file-getcontrol.c \
	file-getfh.c file-getlinknode.c file-lock-stat.c \
	file-lock.c file-set-size.c file-set-trans.c file-statfs.c \
	file-sync.c file-syncfs.c file-utimes.c file-record-lock.c \
	file-reparent.c
IOSRCS= io-async-icky.c io-async.c io-duplicate.c io-get-conch.c io-revoke.c \
	io-map-cntl.c io-map.c io-modes-get.c io-modes-off.c \
	io-modes-on.c io-modes-set.c io-owner-mod.c io-owner-get.c \
	io-pathconf.c io-prenotify.c io-read.c io-readable.c io-identity.c \
	io-reauthenticate.c io-rel-conch.c io-restrict-auth.c io-seek.c \
	io-select.c io-stat.c io-stubs.c io-write.c io-version.c io-sigio.c
FSYSSRCS=fsys-getroot.c fsys-goaway.c fsys-startup.c fsys-getfile.c \
	fsys-options.c fsys-syncfs.c fsys-forward.c \
	fsys-get-children.c fsys-get-source.c
IFSOCKSRCS=ifsock.c
OTHERSRCS = conch-fetch.c conch-set.c dir-clear.c dir-init.c dir-renamed.c \
	extern-inline.c \
	node-create.c node-drop.c node-make.c node-rdwr.c node-update.c \
	node-nref.c node-nput.c node-nrele.c node-lastref.c node-nrefl.c \
	node-nputl.c node-nrelel.c node-cache.c \
	peropen-make.c peropen-rele.c protid-make.c protid-rele.c \
	init-init.c init-startup.c init-first.c init-main.c \
	rdwr-internal.c boot-start.c demuxer.c node-times.c shutdown.c \
	sync-interval.c sync-default.c \
	opts-set.c opts-get.c opts-std-startup.c opts-std-runtime.c \
        opts-append-std.c opts-common.c opts-runtime.c opts-version.c \
	trans-callback.c readonly.c readonly-changed.c \
	remount.c console.c disk-pager.c \
	name-cache.c direnter.c dirrewrite.c dirremove.c lookup.c dead-name.c \
	validate-mode.c validate-group.c validate-author.c validate-flags.c \
	validate-rdev.c validate-owner.c extra-version.c get-source.c
SRCS = $(OTHERSRCS) $(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(IFSOCKSRCS)
installhdrs = diskfs.h diskfs-pager.h

MIGSTUBS = fsServer.o ioServer.o fsysServer.o exec_startupServer.o \
	fsys_replyUser.o fs_notifyUser.o ifsockServer.o \
	startup_notifyServer.o
OBJS = $(sort $(SRCS:.c=.o) $(MIGSTUBS))

HURDLIBS = fshelp iohelp store ports shouldbeinlibc pager ihash
LDLIBS += -lpthread

fsys-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h -DREPLY_PORTS
fs-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
io-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
ifsock-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
exec_startup-MIGSFLAGS = -imacros $(srcdir)/fsmutations.h
MIGCOMSFLAGS = -prefix diskfs_

include ../Makeconf
