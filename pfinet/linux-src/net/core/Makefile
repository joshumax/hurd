#
# Makefile for the Linux networking core.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := core.o

O_OBJS	:= sock.o skbuff.o iovec.o datagram.o scm.o

ifeq ($(CONFIG_SYSCTL),y)
ifeq ($(CONFIG_NET),y)
O_OBJS += sysctl_net_core.o
endif
endif

ifdef CONFIG_FILTER
O_OBJS += filter.o
endif

ifdef CONFIG_NET

O_OBJS	+= dev.o dev_mcast.o dst.o neighbour.o rtnetlink.o utils.o

ifdef CONFIG_FIREWALL
OX_OBJS += firewall.o
endif

endif

ifdef CONFIG_NET_PROFILE
OX_OBJS += profile.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
