# 
#   Copyright (C) 1994 Free Software Foundation
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
include ../Makeconf

DIST_FILES = Makefile $(SRCS) ChangeLog

TRANS = ifsock symlink

SRCS = $(addsuffix .c,$(TRANS))

TRANS_INST = $(addprefix $(hurdroot)/hurd/,$(TRANS))

all: $(TRANS)

install: $(TRANS_INST)

clean:
	rm -f $(TRANS) *.o

relink:
	rm -f $(UTILS)

$(hurdroot)/hurd/%: %
	$(INSTALL_BIN) $^ %@
