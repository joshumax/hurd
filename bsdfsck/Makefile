# 
#   Copyright (C) 1994, 1995, 2012 Free Software Foundation
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

dir := bsdfsck
makemode := utility

SRCS = dir.c inode.c main.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c setup.c utilities.c # preen.c
OBJS = dir.o inode.o main.o pass1.o pass1b.o pass2.o pass3.o pass4.o \
	pass5.o setup.o utilities.o tables.o # preen.o
target = bsdfsck

vpath tables.c ../ufs

include ../Makeconf

