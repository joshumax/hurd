#
#   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2004,
#   2005, 2008, 2010 Free Software Foundation, Inc.
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
ifneq ($(LIBNCURSESW),)
NCURSESW_SO_SRCS = ncursesw.c
endif
SRCS = $(CONSOLE_SRCS) \
  $(VGA_SO_SRCS) $(PC_KBD_SO_SRCS) $(PC_MOUSE_SO_SRCS) \
  $(GENERIC_SPEAKER_SO_SRCS) $(CURRENT_VCS_SO_SRCS) $(NCURSESW_SO_SRCS) \
  $(XKB_SRCS)
LCLHDRS = timer.h driver.h display.h input.h bell.h \
	  unicode.h bdf.h mach-inputdev.h \
	  vga-dynafont.h vga-dynacolor.h vga-hw.h vga-support.h \
	  trans.h \
	  xkb/xkb.h

VPATH += $(srcdir)/xkb
OBJS = $(addsuffix .o,$(basename $(notdir $(SRCS)))) kdioctlServer.o
HURDLIBS = cons threads ports netfs fshelp iohelp ihash shouldbeinlibc
LDLIBS = -ldl
module-dir = $(libdir)/hurd/console
console-LDFLAGS = -Wl,-E

CPPFLAGS += -I$(srcdir)/xkb
LFLAGS = -i
YFLAGS = -by
XKB_DATA_FILES = keymap/hurd types/hurd symbols/hurd

# In seeking, thou shalt find it!
CPPFLAGS += -DQUAERENDO_INVENIETIS

DIST_FILES = xkb/HACKING xkb/MISSING-FEATURES xkb/README xkb/TODO \
             xkb/Compose xkb/default.xkb xkb/makeconf.sh xkb/kstoucs_map.sh \
             $(addprefix xkb/xkb-data/, $(XKB_DATA_FILES))

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
pc_kbd.so.$(hurd-version): $(patsubst %.c,%_pic.o,$(PC_KBD_SO_SRCS)) \
	kdioctlServer_pic.o
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

lex.c: lex.l parser.tab.h
parser.tab.h: parser.y
	if $(YACC) $(YFLAGS) -d $<; then \
	    mv y.tab.h $@; \
	    rm y.tab.c; \
	fi

XKB_SRCS = xkb/compose.c xkb/kstoucs.c xkb/parser.y xkb/lex.l \
           xkb/xkb.c xkb/xkbdata.c xkb/xkbdefaults.c \
           xkb/xkbtimer.c
ifeq ($(HAVE_X11),yes)
XKB_UNITS = $(basename $(notdir $(XKB_SRCS)))
pc_kbd.so.$(hurd-version): $(addsuffix _pic.o,$(XKB_UNITS))
pc-kbd-CFLAGS = -DXKB_SUPPORT -DXKB_DATA_DIR=\"$(XKB_BASE)\" $(X11_CFLAGS)
$(foreach XKB_UNIT, $(XKB_UNITS), $(eval $(XKB_UNIT)-CFLAGS = $(X11_CFLAGS)))
pc_kbd-LDLIBS = $(X11_LIBS)
install: $(XKB_BASE) $(addprefix $(XKB_BASE)/, $(XKB_DATA_FILES))

$(XKB_BASE):
	@$(MKINSTALLDIRS) $@

$(XKB_BASE)/%: xkb/xkb-data/%
	$(INSTALL_DATA) $< $@

# FIXME: dependency files won't catch this one.
$(srcdir)/xkb/kstoucs.c: kstoucs_map.c
kstoucs_map.c: $(includedir)/X11/keysymdef.h $(srcdir)/xkb/kstoucs_map.sh
	SED=$(SED) \
	AWK=$(AWK) \
	sh $(srcdir)/xkb/kstoucs_map.sh \
	     < $< \
	     > map.tmp && \
	mv map.tmp $@
endif
