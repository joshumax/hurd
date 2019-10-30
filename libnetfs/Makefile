#   Copyright (C) 1995-1997, 1999, 2001-2002, 2008, 2012, 2016-2019
#   Free Software Foundation
#
#   Written by Michael I. Bushnell.
#
#   This file is part of the GNU Hurd.
#
#   The GNU Hurd is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   The GNU Hurd is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.

#   You should have received a copy of the GNU General Public License
#   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.

dir := libnetfs
makemode := library
libname = libnetfs

HURDLIBS = fshelp iohelp ports shouldbeinlibc
LDLIBS += -lpthread

FSSRCS= dir-link.c dir-lookup.c dir-mkdir.c dir-mkfile.c \
	dir-notice-changes.c dir-readdir.c dir-rename.c \
	dir-rmdir.c dir-unlink.c file-chauthor.c \
	file-check-access.c file-chflags.c file-chmod.c file-chown.c \
	file-exec.c file-get-fs-options.c file-get-storage-info.c \
	file-get-translator.c file-getcontrol.c file-getlinknode.c \
	file-lock-stat.c file-lock.c file-set-size.c \
	file-set-translator.c file-statfs.c file-sync.c file-syncfs.c \
	file-utimes.c file-record-lock.c file-reparent.c fsstubs.c \
	file-get-transcntl.c

IOSRCS=	io-read.c io-readable.c io-seek.c io-write.c io-stat.c io-async.c     \
	io-set-all-openmodes.c io-get-openmodes.c io-set-some-openmodes.c     \
	io-clear-some-openmodes.c io-mod-owner.c io-get-owner.c io-select.c   \
	io-get-icky-async-id.c io-reauthenticate.c io-restrict-auth.c	      \
	io-duplicate.c iostubs.c io-identity.c io-revoke.c io-pathconf.c      \
	io-version.c

FSYSSRCS= fsys-syncfs.c fsys-getroot.c fsys-get-options.c fsys-set-options.c \
	fsys-goaway.c fsysstubs.c fsys-get-children.c fsys-get-source.c

IFSOCKSRCS=
OTHERSRCS= drop-node.c init-init.c make-node.c make-peropen.c make-protid.c   \
	init-loop.c demuxer.c shutdown.c release-protid.c release-peropen.c   \
	init-startup.c startup-argp.c set-options.c append-args.c	      \
	runtime-argp.c std-runtime-argp.c std-startup-argp.c		      \
	append-std-options.c trans-callback.c set-get-trans.c		      \
	nref.c nrele.c nput.c file-get-storage-info-default.c dead-name.c     \
	get-source.c

SRCS= $(OTHERSRCS) $(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(IFSOCKSRCS)

installhdrs=netfs.h

MIGSTUBS= ioServer.o fsServer.o fsysServer.o fsys_replyUser.o ifsockServer.o

OBJS=$(sort $(SRCS:.c=.o) $(MIGSTUBS))

fsys-MIGSFLAGS = -imacros $(srcdir)/mutations.h -DREPLY_PORTS
fs-MIGSFLAGS = -imacros $(srcdir)/mutations.h
io-MIGSFLAGS = -imacros $(srcdir)/mutations.h
ifsock-MIGSFLAGS = -imacros $(srcdir)/mutations.h
MIGCOMSFLAGS = -prefix netfs_


include ../Makeconf

fsysServer.c fsys_S.h fsServer.c fs_S.h ioServer.c io_S.h ifsockServer.c ifsock_S.h: mutations.h
