#   Copyright (C) 1995, 1996, 1997, 2000, 2007, 2011, 2012 Free Software
#   Foundation, Inc.
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
ARCHS		= alpha arm i386 m68k ppc s390 sparc sparc64

LINUXSRCS	= $(core-srcs) $(ethernet-srcs) $(ipv4-srcs) $(ipv6-srcs)
ARCHSRCS	= $(notdir $(wildcard $(addprefix \
			   $(srcdir)/linux-src/arch/$(asm_syntax)/lib/,\
			   $(arch-lib-srcs) $(arch-lib-srcs:.c=.S))))
SRCS		= sched.c timer-emul.c socket.c main.c ethernet.c \
		  io-ops.c socket-ops.c misc.c time.c options.c loopback.c \
		  kmem_cache.c stubs.c dummy.c tunnel.c pfinet-ops.c \
		  iioctl-ops.c
MIGSRCS		= ioServer.c socketServer.c startup_notifyServer.c \
		  pfinetServer.c iioctlServer.c
OBJS		= $(patsubst %.S,%.o,$(patsubst %.c,%.o,\
			     $(LINUXSRCS) $(ARCHSRCS) $(SRCS) $(MIGSRCS)))
LINUXHDRS	= bitops.h capability.h delay.h errqueue.h etherdevice.h \
		  fddidevice.h firewall.h icmp.h icmpv6.h if_arp.h if_ether.h \
		  if_fddi.h if_packet.h if_ppp.h if_tunnel.h if_tr.h \
		  igmp.h in.h in_route.h inet.h inetdevice.h init.h ip.h \
		  ip_fw.h ipsec.h ipv6_route.h ipx.h kmod.h linkage.h lists.h \
		  module.h mroute.h net.h netdevice.h netlink.h notifier.h \
		  pkt_cls.h pkt_sched.h ppp_defs.h random.h route.h \
		  rtnetlink.h skbuff.h stddef.h sysctl.h tasks.h tcp.h times.h \
		  trdevice.h udp.h
LINUXNETHDRS	= addrconf.h arp.h br.h checksum.h datalink.h dst.h flow.h \
		  icmp.h if_inet6.h inet_common.h ip.h ip_fib.h ip6_fib.h \
		  ip6_route.h ipip.h ipv6.h ipx.h ipxcall.h ndisc.h \
		  neighbour.h p8022.h p8022call.h pkt_cls.h pkt_sched.h \
		  profile.h protocol.h psnap.h psnapcall.h rarp.h raw.h \
		  rawv6.h route.h slhc.h snmp.h sock.h tcp.h transp_v6.h udp.h
ARCHHDRS	= checksum.h
FROBBEDLINUXHEADERS = autoconf.h binfmts.h config.h errno.h fcntl.h fs.h \
	if.h in.h in6.h interrupt.h ioctl.h ipv6.h \
	kernel.h limits.h major.h malloc.h mm.h param.h personality.h \
	poll.h proc_fs.h sched.h slab.h socket.h sockios.h stat.h \
	string.h termios.h time.h timer.h timex.h types.h un.h version.h wait.h
ASMHEADERS = atomic.h bitops.h byteorder.h delay.h errno.h hardirq.h init.h \
	segment.h spinlock.h system.h types.h uaccess.h

HURDLIBS=trivfs fshelp ports ihash shouldbeinlibc iohelp
LDLIBS = -lpthread

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

io-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h
socket-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h
iioctl-MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h

# cpp doesn't automatically make dependencies for -imacros dependencies. argh.
io_S.h ioServer.c socket_S.h socketServer.c: mig-mutate.h
$(OBJS): config.h
