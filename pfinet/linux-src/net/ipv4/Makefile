#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ipv4.o
IPV4_OBJS := utils.o route.o proc.o timer.o protocol.o \
	     ip_input.o ip_fragment.o ip_forward.o ip_options.o \
	     ip_output.o ip_sockglue.o \
	     tcp.o tcp_input.o tcp_output.o tcp_timer.o tcp_ipv4.o\
	     raw.o udp.o arp.o icmp.o devinet.o af_inet.o igmp.o \
	     sysctl_net_ipv4.o fib_frontend.o fib_semantics.o fib_hash.o
IPV4X_OBJS :=

MOD_LIST_NAME := IPV4_MODULES
M_OBJS :=

ifeq ($(CONFIG_IP_FIREWALL),y)
IPV4_OBJS += ip_fw.o
endif

ifeq ($(CONFIG_IP_MULTIPLE_TABLES),y)
IPV4_OBJS += fib_rules.o
endif

ifeq ($(CONFIG_IP_ROUTE_NAT),y)
IPV4_OBJS += ip_nat_dumb.o
endif

ifeq ($(CONFIG_IP_MROUTE),y)
IPV4_OBJS += ipmr.o
endif

ifeq ($(CONFIG_INET_RARP),y)
IPV4_OBJS += rarp.o
else
  ifeq ($(CONFIG_INET_RARP),m)
  M_OBJS += rarp.o
  endif
endif

ifeq ($(CONFIG_NET_IPIP),y)
IPV4X_OBJS += ipip.o
else
  ifeq ($(CONFIG_NET_IPIP),m)
  MX_OBJS += ipip.o
  endif
endif

ifeq ($(CONFIG_NET_IPGRE),y)
IPV4X_OBJS += ip_gre.o
else
  ifeq ($(CONFIG_NET_IPGRE),m)
  MX_OBJS += ip_gre.o
  endif
endif

ifeq ($(CONFIG_IP_MASQUERADE),y)
IPV4X_OBJS += ip_masq.o ip_masq_app.o 

ifeq ($(CONFIG_IP_MASQUERADE_MOD),y)
  IPV4X_OBJS += ip_masq_mod.o 
  
  ifeq ($(CONFIG_IP_MASQUERADE_IPAUTOFW),y)
  IPV4_OBJS += ip_masq_autofw.o
  else
    ifeq ($(CONFIG_IP_MASQUERADE_IPAUTOFW),m)
    M_OBJS += ip_masq_autofw.o
    endif
  endif
  
  ifeq ($(CONFIG_IP_MASQUERADE_IPPORTFW),y)
  IPV4_OBJS += ip_masq_portfw.o
  else
    ifeq ($(CONFIG_IP_MASQUERADE_IPPORTFW),m)
    M_OBJS += ip_masq_portfw.o
    endif
  endif
  
  ifeq ($(CONFIG_IP_MASQUERADE_MFW),y)
  IPV4_OBJS += ip_masq_mfw.o
  else
    ifeq ($(CONFIG_IP_MASQUERADE_MFW),m)
    M_OBJS += ip_masq_mfw.o
    endif
  endif

endif

M_OBJS += ip_masq_user.o
M_OBJS += ip_masq_ftp.o ip_masq_irc.o ip_masq_raudio.o ip_masq_quake.o
M_OBJS += ip_masq_vdolive.o ip_masq_cuseeme.o
endif

ifeq ($(CONFIG_SYN_COOKIES),y)
IPV4_OBJS += syncookies.o
# module not supported, because it would be too messy.
endif

ifeq ($(CONFIG_IP_PNP),y)
IPV4_OBJS += ipconfig.o
endif

ifdef CONFIG_INET
O_OBJS := $(IPV4_OBJS)
OX_OBJS := $(IPV4X_OBJS)
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
