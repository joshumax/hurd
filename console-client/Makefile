#
#   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2004,
#   2005, 2008, 2010, 2011, 2012, 2013, 2014, 2015, 2016 Free Software
#   Foundation, Inc.
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
PC_KBD_SO_SRCS = pc-kbd.c kbd-repeat.c
PC_MOUSE_SO_SRCS = pc-mouse.c
GENERIC_SPEAKER_SO_SRCS = generic-speaker.c
CURRENT_VCS_SO_SRCS = current-vcs.c
SRCS = $(CONSOLE_SRCS) \
  $(VGA_SO_SRCS) $(PC_KBD_SO_SRCS) $(PC_MOUSE_SO_SRCS) \
  $(GENERIC_SPEAKER_SO_SRCS) $(CURRENT_VCS_SO_SRCS) $(XKB_SRCS)

VPATH += $(srcdir)/xkb
OBJS = $(addsuffix .o,$(basename $(notdir $(SRCS)))) kdioctlServer.o
HURDLIBS = cons ports netfs fshelp iohelp ihash shouldbeinlibc
LDLIBS = -ldl -lpthread $(libdaemon_LIBS)
module-dir = $(libdir)/hurd/console
console-LDFLAGS = -Wl,-E

CPPFLAGS += -I$(CURDIR)/xkb -I$(srcdir)/xkb $(libdaemon_CFLAGS)
LFLAGS = -i
YFLAGS = -by
XKB_DATA_FILES = keymap/hurd types/hurd symbols/hurd

# In seeking, thou shalt find it!
CPPFLAGS += -DQUAERENDO_INVENIETIS

include ../Makeconf

driver-CPPFLAGS = -D'CONSOLE_DEFPATH="$(module-dir)\0"' \
	-D'CONSOLE_SONAME_SUFFIX=".so.$(hurd-version)"'
driver-DEPS = $(..)config.make

console: $(CONSOLE_SRCS:.c=.o) \
	../libcons/libcons.a ../libfshelp/libfshelp.a \
	../libnetfs/libnetfs.a ../libiohelp/libiohelp.a \
	../libihash/libihash.a ../libports/libports.a \
	../libshouldbeinlibc/libshouldbeinlibc.a

modules = vga pc_kbd generic_speaker pc_mouse current_vcs

vga-CPPFLAGS = -DDEFAULT_VGA_FONT_DIR=\"${datadir}/hurd/\"
vga.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(VGA_SO_SRCS))
pc_kbd.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(PC_KBD_SO_SRCS)) \
	kdioctlServer_pic.o
pc_mouse.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(PC_MOUSE_SO_SRCS))
generic_speaker.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(GENERIC_SPEAKER_SO_SRCS))
current_vcs.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(CURRENT_VCS_SO_SRCS))

ifneq ($(LIBNCURSESW),)
NCURSESW_SO_SRCS = ncursesw.c
SRCS += $(NCURSESW_SO_SRCS)
modules += ncursesw
ncursesw.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(NCURSESW_SO_SRCS))
ncursesw-CPPFLAGS = $(NCURSESW_INCLUDE)
ncursesw-LDLIBS = $(LIBNCURSESW)
endif

all: $(addsuffix .so.$(hurd-version), $(modules))

cleantarg += $(addsuffix .so.$(hurd-version), $(modules))

install: $(DESTDIR)$(module-dir) $(addprefix $(DESTDIR)$(module-dir)/,$(addsuffix .so.$(hurd-version),$(modules)))

$(DESTDIR)$(module-dir):
	@$(MKINSTALLDIRS) $@

$(DESTDIR)$(module-dir)/%: %
	$(INSTALL_DATA) $< $@

# You can use this rule to make a dynamically-loadable version of any
# of the modules.
%.so.$(hurd-version): 
	$(CC) -shared -Wl,-soname=$@ -o $@ $(lpath) \
		$(CFLAGS) $($*-CFLAGS) $(LDFLAGS) \
		$^ '-Wl,-(' $($*-LDLIBS) '-Wl,-)'

lex.c: lex.l parser.tab.h
%.tab.h %.c: %.y
	if $(YACC) $(YFLAGS) -d $<; then \
	    mv y.tab.h $*.tab.h; \
	    mv y.tab.c $*.c; \
	fi

XKB_SRCS = xkb/compose.c xkb/kstoucs.c xkb/parser.y xkb/lex.l \
           xkb/xkb.c xkb/xkbdata.c xkb/xkbtimer.c
ifeq ($(HAVE_X11),yes)
XKB_UNITS = $(basename $(notdir $(XKB_SRCS)))
pc_kbd.so.$(hurd-version): $(addsuffix _pic.o,$(XKB_UNITS))
pc-kbd-CFLAGS = -DXKB_SUPPORT -DXKB_DATA_DIR=\"$(XKB_BASE)\" $(X11_CFLAGS)
$(foreach XKB_UNIT, $(XKB_UNITS), $(eval $(XKB_UNIT)-CFLAGS = $(X11_CFLAGS)))
compose-CFLAGS += -DDATADIR=\"$(datadir)\"
pc_kbd-LDLIBS = $(X11_LIBS)
install: $(DESTDIR)$(XKB_BASE) $(addprefix $(DESTDIR)$(XKB_BASE)/, $(XKB_DATA_FILES))

$(DESTDIR)$(XKB_BASE):
	@$(MKINSTALLDIRS) $@

$(DESTDIR)$(XKB_BASE)/%: xkb/xkb-data/%
	$(INSTALL_DATA) $< $@

kstoucs.o: xkb/kstoucs_map.c
kstoucs_pic.o: xkb/kstoucs_map.c
xkb/kstoucs_map.c: $(X11_KEYSYMDEF_H) $(srcdir)/xkb/kstoucs_map.sh
	mkdir -p xkb ; \
	SED=$(SED) \
	AWK=$(AWK) \
	sh $(srcdir)/xkb/kstoucs_map.sh \
	     < $< \
	     > map.tmp && \
	mv map.tmp $@
endif
