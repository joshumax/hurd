/* display.h - Interface to the display component of a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <sys/ioctl.h>

struct display;
typedef struct display *display_t;

error_t display_create (display_t *r_display, const char *encoding);
void display_destroy (display_t display);
error_t display_output (display_t display, char **buffer, size_t *length);
void display_getsize (display_t display, struct winsize *winsize);

#endif	/* DISPLAY_H */
