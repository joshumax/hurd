#
#   Copyright (C) 1994, 1995, 1996, 1997, 1999, 2000, 2001, 2002, 2006, 2007,
#   2008, 2013, 2014 Free Software Foundation, Inc.
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

dir := trans
makemode := servers

targets = symlink firmlink ifsock magic null fifo new-fifo fwd crash \
	  password hello hello-mt streamio fakeroot proxy-defpager remap \
	  mtab
SRCS = ifsock.c symlink.c magic.c null.c fifo.c new-fifo.c fwd.c \
	crash.c firmlink.c password.c hello.c hello-mt.c streamio.c \
	fakeroot.c proxy-defpager.c remap.c mtab.c
OBJS = $(SRCS:.c=.o) fsysServer.o ifsockServer.o passwordServer.o \
	crashServer.o crash_replyUser.o msgServer.o \
	default_pagerServer.o default_pagerUser.o \
	device_replyServer.o elfcore.o startup_notifyServer.o
HURDLIBS = ports netfs trivfs iohelp fshelp pipe ihash shouldbeinlibc
LDLIBS += -lpthread
password-MIGSFLAGS=\
    "-DIO_INTRAN=trivfs_protid_t trivfs_begin_using_protid (io_t)" \
    "-DIO_INTRAN_PAYLOAD=trivfs_protid_t trivfs_begin_using_protid_payload" \
    "-DIO_DESTRUCTOR=trivfs_end_using_protid (trivfs_protid_t)" \
    "-DPASSWORD_IMPORTS=import \"../libtrivfs/mig-decls.h\";"

ifsock-MIGSFLAGS=\
    "-DFILE_INTRAN=trivfs_protid_t trivfs_begin_using_protid (io_t)" \
    "-DFILE_INTRAN_PAYLOAD=trivfs_protid_t trivfs_begin_using_protid_payload" \
    "-DFILE_DESTRUCTOR=trivfs_end_using_protid (trivfs_protid_t)" \
    "-DIFSOCK_IMPORTS=import \"../libtrivfs/mig-decls.h\";"

# device_replyServer is used by the streamio translator.
device_reply-MIGSFLAGS=\
    "-DMACH_PAYLOAD_TO_PORT=ports_payload_get_name" \
    "-DDEVICE_IMPORTS=import \"$(srcdir)/../libports/ports.h\";"

# fsysServer is only used by the symlink translator which does not use
# libports.  Disable the default payload to port conversion.
fsys-MIGSFLAGS = "-DHURD_DEFAULT_PAYLOAD_TO_PORT=1"
fsysServer-CFLAGS = "-DMIG_EOPNOTSUPP=EOPNOTSUPP"

# If we have a configured tree, include the configuration so that we
# can conditionally build translators.
ifneq (,$(wildcard ../config.make))
 include ../config.make
endif

ifeq ($(HAVE_LIBGCRYPT),yes)
 SRCS += random.c
 targets += random
 random-LDLIBS = -lgcrypt
 random: startup_notifyServer.o mach_debugUser.o ../libtrivfs/libtrivfs.a
endif

include ../Makeconf

vpath elfcore.c $(top_srcdir)/exec

crash: crashServer.o crash_replyUser.o msgServer.o elfcore.o
ifsock: ifsockServer.o
mtab: fsysUser.o
password: passwordServer.o
proxy-defpager: default_pagerServer.o default_pagerUser.o
streamio: device_replyServer.o
symlink: fsysServer.o

fakeroot: ../libnetfs/libnetfs.a
fifo new-fifo: ../libpipe/libpipe.a
crash fifo firmlink hello hello-mt ifsock magic mtab new-fifo null password proxy-defpager remap streamio: ../libtrivfs/libtrivfs.a
$(targets): ../libfshelp/libfshelp.a \
	../libihash/libihash.a \
	../libiohelp/libiohelp.a \
	../libports/libports.a \
	../libshouldbeinlibc/libshouldbeinlibc.a

$(targets): %: %.o
