# 
#   Copyright (C) 1994, 1995 Free Software Foundation
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

dir := libpager
makemode := library

COBJS = data-request.o data-return.o data-unlock.o pager-port.o \
	inhibit-term.o lock-completed.o lock-object.o mark-error.o \
	 no-senders.o object-init.o object-terminate.o pagemap.o \
	 pager-create.o pager-flush.o pager-shutdown.o pager-sync.o \
	 stubs.o seqnos.o demuxer.o chg-compl.o pager-attr.o clean.o \
	dropweak.o
OBJS = $(COBJS) memory_objectServer.o notifyServer.o
SRCS = $(subst .o,.c,$(COBJS))
LCLHDRS = pager.h priv.h
REMHDRS = ../hurd/ports.h
libname = libpager
installhdrs = pager.h

MIGSFLAGS = -DSEQNOS
MIGCOMSFLAGS = -prefix _pager_

include ../Makeconf

$(OBJS): pager.h priv.h ../hurd/ports.h
data-request.o data-return.o data-unlock.o lock-completed.o: memory_object_S.h
demuxer.o stubs.o object-init.o object-terminate.o: memory_object_S.h
demuxer.o: notify_S.h


