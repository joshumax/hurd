#
#   Copyright (C) 1994,95,96,97,98,99,2000,01,02 Free Software Foundation, Inc.
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
makemode := utility

target = console
SRCS = console.c timer.c driver.c
LCLHDRS = timer.h driver.h display.h input.h bell.h \
	  unicode.h bdf.h \
	  vga-dynafont.h vga-dynacolor.h vga-hw.h vga.h

OBJS = $(filter-out %.sh,$(SRCS:.c=.o))
HURDLIBS = cons threads ports
LDLIBS = -ldl
module-dir = $(libdir)/hurd/console
console-LDFLAGS = -Wl,-E

# In seeking, thou shalt find it!
CPPFLAGS += -DQUAERENDO_INVENIETIS

include ../Makeconf

driver-CPPFLAGS = -D'CONSOLE_DEFPATH="$(module-dir)\0"' \
	-D'CONSOLE_SONAME_SUFFIX=".so.$(hurd-version)"'

console: ../libcons/libcons.a ../libports/libports.a \
	../libthreads/libthreads.a ../libshouldbeinlibc/libshouldbeinlibc.a

modules = vga pc_kbd generic_speaker

vga.so.$(hurd-version): $(patsubst %.c,%_pic.o,bdf.c vga-dynafont.c \
	vga-dynacolor.c vga-support.c vga.c)
pc_kbd.so.$(hurd-version): $(patsubst %.c,%_pic.o,pc-kbd.c)
generic_speaker.so.$(hurd-version): $(patsubst %.c,%_pic.o,generic-speaker.c)

ifneq ($(LIBNCURSESW),)
modules += ncursesw
ncursesw.so.$(hurd-version): $(patsubst %.c,%_pic.o,ncursesw.c)
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
