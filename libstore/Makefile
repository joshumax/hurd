# Makefile for libstore
# 
#   Copyright (C) 1995, 1996 Free Software Foundation
#   Written by Miles Bader <miles@gnu.ai.mit.edu>
#
#   This file is part of the GNU Hurd.
#
#   The GNU Hurd is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   The GNU Hurd is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := libstore
makemode := library

libname = libstore
SRCS = create.c derive.c make.c rdwr.c set.c device.c file.c stripe.c \
	storeread.c storecat.c enc.c encode.c decode.c clone.c
LCLHDRS=store.h
installhdrs=store.h

UTIL_OBJS = storeread.o storecat.o
OBJS = $(filter-out $(UTIL_OBJS), $(SRCS:.c=.o))

include ../Makeconf

storeread: libstore.so ../libshouldbeinlibc/libshouldbeinlibc.so
storecat: libstore.so ../libshouldbeinlibc/libshouldbeinlibc.so
