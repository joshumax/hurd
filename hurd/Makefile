#   Copyright (C) 1993,94,95,96,99,2002,2012 Free Software Foundation
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

dir := hurd
makemode := misc

hdrs = $(wildcard $(srcdir)/*.defs $(srcdir)/*.h)

INSTHDRS = hurd_types.h version.h ioctl_types.h paths.h shared.h console.h \
	$(notdir $(wildcard $(srcdir)/*.defs))
MSGIDS := hurd.msgids $(patsubst %.defs,%.msgids,$(filter %.defs,$(INSTHDRS)))

include ../Makeconf

all: $(MSGIDS)

install: install-msgids install-headers
install-headers: $(DESTDIR)$(includedir)/hurd \
	$(addprefix $(DESTDIR)$(includedir)/hurd/,$(INSTHDRS))
install-msgids: $(MSGIDS) $(DESTDIR)$(datadir)/msgids; $(INSTALL_DATA) $^

$(DESTDIR)$(includedir)/hurd/%: $(srcdir)/%; $(INSTALL_DATA) $< $@

$(DESTDIR)$(datadir)/msgids $(DESTDIR)$(includedir)/hurd:;mkdir -p $@

%.msgids: $(srcdir)/%.defs
	if grep -q '^subsystem' $<; \
	then $(CPP) $(CPPFLAGS) $< | $(MIGCOM) -n -list $@; \
	else > $@; fi
hurd.msgids: $(patsubst %.defs,%.msgids,$(filter %.defs,$(INSTHDRS)))
	cat $^ > $@

#
# The following rules assist in creating an `Xioctl.defs' file
# to define RPCs that are sent primarily by ioctl commands.
# To use them, write a file `Xioctl-headers.h', e.g. for `mioctl-headers.h':
#	#include <sys/mtio.h>
# with an #include for each header that defines ioctl request macros
# using _IO('X') et al.  Then `make Xioctl-proto.defs' will create
# a prototype file for you to hand-edit into `Xioctl.defs'.

# Building foo.h from foo.sym:
%.symc: %.sym
	$(AWK) -f $(srcdir)/gensym.awk $< >$*.symc
%.symc.o: %.symc
	$(CC) -S $(CPPFLAGS) $(CFLAGS) $(CPPFLAGS-$@) -x c -o $@ $<
%.h: %.symc.o
	sed <$< -e 's/^[[:space:]]*//' -e 's/^[^*].*$$//' | \
	    sed -e 's/^[*]/#define/' -e 's/mAgIc[^-0-9]*//' -e '/^ *$$/d' >$@

%-ioctls.sym: tmpl-ioctls.sym
	sed 's|HEADER|<$(subst +,/,$*)>|' $< > $@

cpp = $(CC) $(CPPFLAGS) $(CFLAGS) $(CPPFLAGS-$@) -E -x c

%ioctl-requests.list: %ioctl-headers.h
	$(cpp) $< | sed -n 's/^#.*"\([^"]*\)".*$$/\1/p' | sort | uniq | \
	while read f; do \
	  sed -n 's/^[ 	]*#[ 	]*define[ 	]*\([A-Z0-9_]*\)[^A-Z0-9_][^A-Z0-9_]*_IO.*'\'$*\''.*$$/\1/p' $$f; \
	done | sort | uniq > $@

%ioctl.defs: %ioctl.sym

%ioctl-values.sym: %ioctl-headers.h %ioctl-requests.list ioctl-tmpl.sym
	(sed 's%@HEADER_LIST@%$<%;s/@GROUP@/$*/g' < $(filter %.sym,$^); \
	 while read r; do \
	   for x in CMD SUBID INOUT TYPE \
		    TYPE0 TYPE1 TYPE2 COUNT0 COUNT1 COUNT2; do \
	     echo "expr $${x}($${r}) $${r}_$${x}"; \
	   done; \
	 done < $(filter %.list,$^)) > $@

%ioctl-proto.defs: %ioctl-values.h ioctl.awk
	sed 's/^#define//;s/_/ /g' $< | $(AWK) -f $(filter %.awk,$^) > $@
