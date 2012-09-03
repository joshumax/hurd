#   Copyright (C) 1994,95,96,97,2000,2010,2012 Free Software Foundation, Inc.
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

dir := libthreads
makemode := library

# In GNU mig_support.c, sync.c and machine/lock.s are omitted; that work is
# all done in libc.
#SRCS := call.c cprocs.c cthread_data.c cthreads.c stack.c \
#	cancel-cond.c rwlock.c lockfile.c
#I386SRCS := i386/csw.S i386/thread.c

# In GNU machine/cthreads.h is omitted; that work is done in libc headers.

#OBJS = $(addsuffix .o,$(basename $(notdir $(SRCS) $(I386SRCS))))

#OTHERTAGS = $(I386SRCS) $(I386HDRS)

libname = libthreads
installhdrs = cthreads.h rwlock.h
installhdrsubdir = .

VPATH += $(srcdir)/$(asm_syntax)

include ../Makeconf

# The threads library was written by CMU.  If you've ever experienced
# what that means, you'll understand this line.
#CFLAGS := $(filter-out -Wall,$(CFLAGS))

#ifeq ($(VERSIONING),yes)
# Adding this dependency gets it included in the command line,
# where ld will read it as a linker script.
#$(libname).so.$(hurd-version): $(srcdir)/$(libname).map
#endif
