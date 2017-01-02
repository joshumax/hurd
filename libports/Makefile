#   Copyright (C) 1995,96,97,99,2000,2012 Free Software Foundation, Inc.
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

dir := libports
makemode := library

libname = libports
SRCS = create-bucket.c create-class.c \
 reallocate-port.c reallocate-from-external.c destroy-right.c \
 lookup-port.c port-ref.c port-ref-weak.c port-deref.c port-deref-weak.c \
 no-senders.c begin-rpc.c end-rpc.c manage-one-thread.c manage-multithread.c \
 inhibit-port-rpcs.c inhibit-class-rpcs.c inhibit-bucket-rpcs.c \
 inhibit-all-rpcs.c resume-port-rpcs.c resume-class-rpcs.c \
 resume-bucket-rpcs.c resume-all-rpcs.c interrupt-rpcs.c \
 init.c complete-deallocate.c get-right.c get-send-right.c \
 count-class.c count-bucket.c \
 enable-class.c enable-bucket.c bucket-iterate.c class-iterate.c stubs.c \
 notify-dead-name.c notify-no-senders.c notify-port-destroyed.c \
 notify-msg-accepted.c notify-port-deleted.c notify-send-once.c \
 interrupt-operation.c interrupt-on-notify.c interrupt-notified-rpcs.c \
 dead-name.c create-port.c import-port.c default-uninhibitable-rpcs.c \
 claim-right.c transfer-right.c create-port-noinstall.c create-internal.c \
 interrupted.c extern-inline.c port-deref-deferred.c

installhdrs = ports.h port-deref-deferred.h

HURDLIBS= ihash shouldbeinlibc
LDLIBS += -lpthread
OBJS = $(SRCS:.c=.o) notifyServer.o interruptServer.o

MIGCOMSFLAGS = -prefix ports_
MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h

include ../Makeconf
