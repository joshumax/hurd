# 
#   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.

dir := pfinet
makemode := server
LINUXSRCS= af_inet.c arp.c datagram.c dev.c dev_mcast.c devinet.c eth.c \
	icmp.c igmp.c ip.c  \
	proc.c protocol.c raw.c route.c skbuff.c sock.c \
	tcp.c timer.c udp.c utils.c
UNUSEDSRC = packet.c ipx.c ip_fw.c p8022.c p8023.c pe2.c psnap.c rarp.c
SRCS = sched.c timer-emul.c devices.c socket.c main.c ethernet.c \
	io-ops.c socket-ops.c misc.c
MIGSRCS = ioServer.c socketServer.c
OBJS= $(subst .c,.o,$(LINUXSRCS) $(SRCS) $(MIGSRCS))

pfinet: $(OBJS) ../libtrivfs/libtrivfs.a ../libthreads/libthreads.a ../libports/libports.a ../libihash/libihash.a

vpath %.c $(srcdir)/linux-inet

target = pfinet

include ../Makeconf

CPPFLAGS += -imacros $(srcdir)/config.h

io-MIGSFLAGS = -imacros $(srcdir)/mutations.h
socket-MIGSFLAGS = -imacros $(srcdir)/mutations.h

# cpp doesn't automatically make dependencies for -imacros dependencies. argh.
io_S.h ioServer.c socket_S.h socketServer.c: mutations.h
$(OBJS): config.h

