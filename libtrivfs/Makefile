#   Copyright (C) 1994-1997, 1999, 2001-2003, 2008, 2012-2019
#   Free Software Foundation, Inc.
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

dir := libtrivfs
makemode := library

FSSRCS= dir-link.c dir-mkdir.c dir-mkfile.c dir-lookup.c dir-readdir.c \
	dir-rename.c dir-rmdir.c dir-unlink.c file-chauthor.c \
	file-chflags.c file-chmod.c file-chown.c file-get-trans.c \
	file-get-transcntl.c file-getcontrol.c file-getfh.c \
	file-getlinknode.c file-lock.c file-lock-stat.c  file-record-lock.c \
	file-set-trans.c file-statfs.c \
	file-sync.c file-syncfs.c file-set-size.c file-utimes.c file-exec.c \
	file-access.c dir-chg.c file-chg.c file-get-storage-info.c \
	file-get-fs-options.c file-reparent.c \

IOSRCS=io-async-icky.c io-async.c io-duplicate.c io-map.c io-modes-get.c \
	io-modes-off.c io-modes-on.c io-modes-set.c io-owner-get.c \
	io-owner-mod.c io-pathconf.c io-read.c io-readable.c io-revoke.c \
	io-reauthenticate.c io-restrict-auth.c io-seek.c io-select.c \
	io-stat.c io-stubs.c io-write.c io-version.c io-identity.c

FSYSSRCS=fsys-getroot.c fsys-goaway.c fsys-stubs.c fsys-syncfs.c \
	fsys-forward.c fsys-set-options.c fsys-get-options.c \
	fsys-get-children.c fsys-get-source.c \

OTHERSRCS=demuxer.c protid-clean.c protid-dup.c cntl-create.c \
	cntl-clean.c times.c startup.c make-node.c make-peropen.c open.c \
	runtime-argp.c set-options.c append-args.c dyn-classes.c \
	get-source.c

SRCS=$(FSSRCS) $(IOSRCS) $(FSYSSRCS) $(OTHERSRCS)

MIGSTUBS=fsServer.o ioServer.o fsysServer.o fsys_replyUser.o

libname = libtrivfs
HURDLIBS = fshelp iohelp ports shouldbeinlibc
OBJS= $(sort $(subst .c,.o,$(SRCS)) $(MIGSTUBS))
MIGSFLAGS=-imacros $(srcdir)/mig-mutate.h
MIGCOMSFLAGS = -prefix trivfs_
installhdrs := trivfs.h
mig-sheader-prefix = trivfs_

include ../Makeconf

$(MIGSTUBS:%Server.o=%.sdefsi): $(srcdir)/mig-mutate.h
