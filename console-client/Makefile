#
#   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2004,
#   2005, 2008 Free Software Foundation, Inc.
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

dir := console-client
makemode := utilities

targets = console
CONSOLE_SRCS = console.c timer.c driver.c trans.c
VGA_SO_SRCS = bdf.c vga-dynafont.c vga-dynacolor.c vga-support.c vga.c
#PC_KBD_SO_SRCS = pc-kbd.c kdioctlServer.o kbd-repeat.c
PC_KBD_SO_SRCS = pc-kbd.c kdioctlServer.c kbd-repeat.c
PC_MOUSE_SO_SRCS = pc-mouse.c
GENERIC_SPEAKER_SO_SRCS = generic-speaker.c
CURRENT_VCS_SO_SRCS = current-vcs.c
ifneq ($(LIBNCURSESW),)
NCURSESW_SO_SRCS = ncursesw.c
endif
SRCS = $(CONSOLE_SRCS) \
  $(VGA_SO_SRCS) $(PC_KBD_SO_SRCS) $(PC_MOUSE_SO_SRCS) \
  $(GENERIC_SPEAKER_SO_SRCS) $(CURRENT_VCS_SO_SRCS) $(NCURSESW_SO_SRCS)
LCLHDRS = timer.h driver.h display.h input.h bell.h \
	  unicode.h bdf.h mach-inputdev.h \
	  vga-dynafont.h vga-dynacolor.h vga-hw.h vga.h

OBJS = $(SRCS:.c=.o)
HURDLIBS = cons threads ports netfs fshelp iohelp ihash shouldbeinlibc
LDLIBS = -ldl
module-dir = $(libdir)/hurd/console
console-LDFLAGS = -Wl,-E

# In seeking, thou shalt find it!
CPPFLAGS += -DQUAERENDO_INVENIETIS

include ../Makeconf

driver-CPPFLAGS = -D'CONSOLE_DEFPATH="$(module-dir)\0"' \
	-D'CONSOLE_SONAME_SUFFIX=".so.$(hurd-version)"'

console: $(CONSOLE_SRCS:.c=.o) \
	../libnetfs/libnetfs.a ../libfshelp/libfshelp.a \
	../libcons/libcons.a ../libports/libports.a \
	../libthreads/libthreads.a ../libshouldbeinlibc/libshouldbeinlibc.a

modules = vga pc_kbd generic_speaker pc_mouse current_vcs

vga-CPPFLAGS = -DDEFAULT_VGA_FONT_DIR=\"${datadir}/hurd/\"
vga.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(VGA_SO_SRCS))
pc_kbd.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(PC_KBD_SO_SRCS))
pc_mouse.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(PC_MOUSE_SO_SRCS))
generic_speaker.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(GENERIC_SPEAKER_SO_SRCS))
current_vcs.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(CURRENT_VCS_SO_SRCS))

ifneq ($(LIBNCURSESW),)
modules += ncursesw
ncursesw.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(NCURSESW_SO_SRCS))
ncursesw-CPPFLAGS = $(NCURSESW_INCLUDE)
ncursesw-LDLIBS = $(LIBNCURSESW)
endif

all: $(addsuffix .so.$(hurd-version), $(modules))

cleantarg += $(addsuffix .so.$(hurd-version), $(modules))

install: $(module-dir) $(addprefix $(module-dir)/,$(addsuffix .so.$(hurd-version),$(modules)))

$(module-dir):
	@$(MKINSTALLDIRS) $@

$(module-dir)/%: %
	$(INSTALL_DATA) $< $@

# You can use this rule to make a dynamically-loadable version of any
# of the modules.
%.so.$(hurd-version): 
	$(CC) -shared -Wl,-soname=$@ -o $@ $(rpath) \
		$(CFLAGS) $($*-CFLAGS) $(LDFLAGS) \
		'-Wl,-(' $($*-LDLIBS) '-Wl,-)' $^
