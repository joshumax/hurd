# 
#   Copyright (C) 1995, 1996 Free Software Foundation
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
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := libnetfs
makemode := library
libname = libnetfs

FSSRCS= file-chauthor.c file-check-access.c file-chflags.c file-chmod.c	      \
	file-chown.c file-get-storage-info.c file-lock-stat.c file-lock.c     \
	file-set-size.c file-statfs.c file-sync.c file-syncfs.c file-utimes.c \
	dir-lookup.c dir-unlink.c dir-rename.c dir-rmdir.c dir-mkfile.c	      \
	dir-notice-changes.c dir-mkdir.c dir-link.c file-getcontrol.c	      \
	fsstubs.c file-getlinknode.c dir-readdir.c file-get-fs-options.c      \
	file-get-translator.c file-set-translator.c file-exec.c

IOSRCS=	io-read.c io-readable.c io-seek.c io-write.c io-stat.c io-async.c     \
	io-set-all-openmodes.c io-get-openmodes.c io-set-some-openmodes.c     \
	io-clear-some-openmodes.c io-mod-owner.c io-get-owner.c io-select.c   \
	io-get-icky-async-id.c io-reauthenticate.c io-restrict-auth.c	      \
	io-duplicate.c iostubs.c io-identity.c

FSYSSRCS= fsys-syncfs.c fsys-getroot.c fsys-get-options.c fsys-set-options.c \
	fsysstubs.c 

IFSOCKSRCS=
OTHERSRCS= drop-node.c init-init.c make-node.c make-peropen.c make-protid.c   \
	init-loop.c demuxer.c shutdown.c release-protid.c release-peropen.c   \
	init-startup.c startup-argp.c set-options.c get-options.c	      \
	runtime-argp.c std-runtime-argp.c std-startup-argp.c		      \
	append-std-options.c

SRCS= $(OTHERSRCS) $(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(IFSOCKSRCS)

LCLHDRS = netfs.h callbacks.h misc.h modes.h mutations.h priv.h execserver.h
installhdrs=netfs.h

MIGSTUBS= ioServer.o fsServer.o fsysServer.o ifsockServer.o

OBJS=$(sort $(SRCS:.c=.o) $(MIGSTUBS))

fsys-MIGSFLAGS = -imacros $(srcdir)/mutations.h
fs-MIGSFLAGS = -imacros $(srcdir)/mutations.h
io-MIGSFLAGS = -imacros $(srcdir)/mutations.h
ifsock-MIGSFLAGS = -imacros $(srcdir)/mutations.h
MIGCOMSFLAGS = -prefix netfs_


include ../Makeconf

fsysServer.c fsys_S.h fsServer.c fs_S.h ioServer.c io_S.h ifsockServer.c ifsock_S.h: mutations.h
