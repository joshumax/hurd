#
#   Copyright (C) 1994, 1998, 1999 Free Software Foundation
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

dir := doc
makemode := misc

DIST_FILES = navigating gpl.texinfo hurd.texi version.texi \
	hurd.info $(wildcard hurd.info-*)
targets = hurd.info

DVIPS = dvips

include ../Makeconf

# For each .info file we need a .d file.
-include $(subst .info,.d,$(filter %.info,$(targets))) /dev/null

# Build dependencies from included files.
%.d: %.texi
	set -e; (echo "$*.info $*.dvi: \\"; grep '^@include ' $< | \
		 sed -e 's/^[^ 	]*[ 	]*\([^ 	]*\).*$$/	\1 \\/'; \
		 echo) > $@.new
	mv -f $@.new $@

%.info: %.texi
	@cd $(srcdir) && rm -f $@ $@-[0-9] $@-[0-9][0-9]
	cd $(srcdir) && $(MAKEINFO) $(notdir $<)

.PRECIOUS: %.dvi
%.dvi: %.texi
	TEXINPUTS=$(srcdir):$$TEXINPUTS \
	  MAKEINFO='$(MAKEINFO) -I $(srcdir)' $(TEXI2DVI) $<

%.ps: %.dvi
	$(DVIPS) $< -o $@

# A header which contains the latest package version.
$(srcdir)/version.texi: $(top_srcdir)/version.h
	set -e; (echo "@c version.texi - automatically generated from ../version.h"; \
	         grep '^#define HURD_VERSION' $< | \
		 sed 's/^[^"]*"\([^"]*\).*$$/@set VERSION \1/') > $@.new
	mv -f $@.new $@

all: $(targets)
