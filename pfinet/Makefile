#
#   Copyright (C) 1995, 1996, 1997, 2000, 2007 Free Software Foundation, Inc.
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

dir		:= pfinet
makemode	:= server

core-srcs	:= datagram.c						      \
		   dev.c						      \
		   dev_mcast.c						      \
		   dst.c						      \
		   iovec.c						      \
		   neighbour.c						      \
		   skbuff.c						      \
		   sock.c						      \
		   utils.c
arch-lib-srcs   := checksum.c old-checksum.c csum_partial_copy.c
ethernet-srcs	:= eth.c
ipv4-srcs	:= af_inet.c						      \
		   arp.c						      \
		   devinet.c						      \
		   fib_frontend.c					      \
		   fib_hash.c						      \
		   fib_semantics.c					      \
		   icmp.c						      \
		   igmp.c						      \
		   ip_forward.c						      \
		   ip_fragment.c					      \
		   ip_input.c						      \
		   ip_options.c						      \
		   ip_output.c						      \
		   ip_sockglue.c					      \
		   protocol.c						      \
		   raw.c						      \
		   route.c						      \
		   syncookies.c						      \
		   sysctl_net_ipv4.c					      \
		   tcp.c						      \
		   tcp_input.c						      \
		   tcp_ipv4.c						      \
		   tcp_output.c						      \
		   tcp_timer.c						      \
		   timer.c						      \
		   udp.c						      \
		   utils.c
ipv6-srcs      :=  addrconf.c						      \
		   af_inet6.c						      \
		   datagram_ipv6.c					      \
		   exthdrs.c						      \
		   icmpv6.c						      \
		   ip6_fib.c      					      \
		   ip6_flowlabel.c					      \
		   ip6_input.c    					      \
		   ip6_output.c   					      \
		   ipv6_sockglue.c					      \
		   mcast.c						      \
		   ndisc.c						      \
		   protocol_ipv6.c					      \
		   raw_ipv6.c						      \
		   reassembly.c						      \
		   route_ipv6.c						      \
		   tcp_ipv6.c						      \
		   udp_ipv6.c
LINUXSRCS	= $(core-srcs) $(ethernet-srcs) $(ipv4-srcs) $(ipv6-srcs) \
		  $(notdir $(wildcard $(addprefix \
			   $(srcdir)/linux-src/arch/$(asm_syntax)/lib/,\
			   $(arch-lib-srcs) $(arch-lib-srcs:.c=.S))))
SRCS		= sched.c timer-emul.c socket.c main.c ethernet.c \
		  io-ops.c socket-ops.c misc.c time.c options.c loopback.c \
		  kmem_cache.c stubs.c dummy.c tunnel.c pfinet-ops.c \
		  iioctl-ops.c
MIGSRCS		= ioServer.c socketServer.c startup_notifyServer.c \
		  pfinetServer.c iioctlServer.c
OBJS		= $(patsubst %.S,%.o,$(patsubst %.c,%.o,\
			     $(LINUXSRCS) $(SRCS) $(MIGSRCS)))
LCLHDRS		= config.h mapped-time.h mutations.h pfinet.h
LINUXHDRS	= arp.h datalink.h eth.h icmp.h ip.h ipx.h ipxcall.h p8022.h \
		  p8022call.h protocol.h psnap.h psnapcall.h \
		  rarp.h raw.h route.h snmp.h sock.h tcp.h udp.h
FROBBEDLINUXHEADERS = autoconf.h config.h errno.h etherdevice.h fcntl.h \
	icmp.h if.h if_arp.h if_ether.h igmp.h in.h inet.h interrupt.h \
	ip.h ip_fw.h ipx.h kernel.h major.h malloc.h mm.h net.h netdevice.h \
	notifier.h param.h route.h sched.h skbuff.h socket.h sockios.h stat.h \
	string.h tcp.h termios.h time.h timer.h types.h udp.h un.h wait.h
ASMHEADERS=bitops.h segment.h system.h

HURDLIBS=trivfs fshelp threads ports ihash shouldbeinlibc iohelp

target = pfinet

include ../Makeconf

vpath %.c $(addprefix $(srcdir)/linux-src/net/,core ethernet ipv4 ipv6)
vpath %.c $(srcdir)/linux-src/arch/$(asm_syntax)/lib
vpath %.S $(srcdir)/linux-src/arch/$(asm_syntax)/lib

CPPFLAGS += -imacros $(srcdir)/config.h		\
	    -I$(srcdir)/glue-include		\
	    -I$(srcdir)/linux-src/include

# Don't ask...  We use Linux code.  The problem was first noticed when
# compiling `pfinet' with GCC 4.2.
CFLAGS += -fno-strict-aliasing

asm/checksum.h: ../config.status
	mkdir -p $(@D)
	echo > $@.new \
	     '#include "../linux-src/include/asm-$(asm_syntax)/checksum.h"'
	mv -f $@.new $@

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
