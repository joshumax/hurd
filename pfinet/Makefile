# 
#   Copyright (C) 1995 Free Software Foundation, Inc.
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
	icmp.c igmp.c ip.c ip_fw.c ipx.c p8022.c p8023.c packet.c pe2.c \
	proc.c protocol.c psnap.c rarp.c raw.c route.c skbuff.c sock.c \
	tcp.c timer.c udp.c utils.c
OBJS= $(subst .c,.o,$(LINUXSRCS))

vpath %.c linux-inet

target = pfinet

include ../Makeconf

CPPFLAGS += -I.


