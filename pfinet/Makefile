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
	io-ops.c socket-ops.c misc.c time.c options.c
MIGSRCS = ioServer.c socketServer.c startup_notifyServer.c
OBJS= $(subst .c,.o,$(LINUXSRCS) $(SRCS) $(MIGSRCS))
LCLHDRS= config.h mapped-time.h mutations.h pfinet.h
LINUXHDRS = arp.h datalink.h eth.h icmp.h ip.h ipx.h ipxcall.h p8022.h \
	p8022call.h protocol.h psnap.h psnapcall.h rarp.h raw.h route.h \
	snmp.h sock.h tcp.h udp.h
FROBBEDLINUXHEADERS = autoconf.h config.h errno.h etherdevice.h fcntl.h \
	icmp.h if.h if_arp.h if_ether.h igmp.h in.h inet.h interrupt.h \
	ip.h ip_fw.h ipx.h kernel.h major.h malloc.h mm.h net.h netdevice.h \
	notifier.h param.h route.h sched.h skbuff.h socket.h sockios.h stat.h \
	string.h tcp.h termios.h time.h timer.h types.h udp.h un.h wait.h
ASMHEADERS=bitops.h segment.h system.h

pfinet: $(OBJS) ../libtrivfs/libtrivfs.a ../libfshelp/libfshelp.a \
	../libthreads/libthreads.a ../libports/libports.a \
	../libihash/libihash.a ../libshouldbeinlibc/libshouldbeinlibc.a

vpath %.c $(srcdir)/linux-inet

target = pfinet

include ../Makeconf

CPPFLAGS += -imacros $(srcdir)/config.h

io-MIGSFLAGS = -imacros $(srcdir)/mutations.h
socket-MIGSFLAGS = -imacros $(srcdir)/mutations.h

# cpp doesn't automatically make dependencies for -imacros dependencies. argh.
io_S.h ioServer.c socket_S.h socketServer.c: mutations.h
$(OBJS): config.h

lndist: lndist-linux-inet-files lndist-linux-files lndist-asm-files

lndist-linux-inet-files: $(top_srcdir)/hurd-snap/$(dir)/linux-inet
	ln $(addprefix $(srcdir)/linux-inet/,$(LINUXSRCS) $(UNUSEDSRC) $(LINUXHDRS)) $<

lndist-linux-files: $(top_srcdir)/hurd-snap/$(dir)/linux
	ln $(addprefix $(srcdir)/linux/,$(FROBBEDLINUXHEADERS)) $<

lndist-asm-files: $(top_srcdir)/hurd-snap/$(dir)/asm
	ln $(addprefix $(srcdir)/asm/,$(ASMHEADERS)) $<

$(top_srcdir)/hurd-snap/$(dir)/linux-inet:
	mkdir $@
$(top_srcdir)/hurd-snap/$(dir)/linux:
	mkdir $@
$(top_srcdir)/hurd-snap/$(dir)/asm:
	mkdir $@
