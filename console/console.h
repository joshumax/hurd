/* console.h -- Public interface to the console server.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef _HURD_CONSOLE_H
#define _HURD_CONSOLE_H

#include <stdint.h>

struct cons_display
{
#define CONS_MAGIC 0x48555244	/* Hex for "HURD".  */
  uint32_t magic;		/* CONS_MAGIC, use to detect
				   endianess.  */
#define CONS_VERSION_MAJ 0x0
#define CONS_VERSION_MAJ_SHIFT 16
#define CONS_VERSION_AGE 0x0
  uint32_t version;		/* Version of interface.  Lower 16
				   bits define the age, upper 16 bits
				   the major version.  */
  struct
  {
    uint32_t width;	/* Width of screen matrix.  */
    uint32_t lines;	/* Length of whole matrix.  */
    uint32_t cur_line;	/* Beginning of visible area.  This is only
			   ever increased by the server, so clients
			   can optimize scrolling.  */
    uint32_t scr_lines;/* Number of lines in scrollback buffer
			   preceeding CUR_LINE.  */
    uint32_t height;	/* Number of lines in visible area following
			   (and including) CUR_LINE.  */
    uint32_t matrix;	/* Index (in wchar_t) of the beginning of
			   screen matrix in this structure.  */
  } screen;

  struct
  {
    uint32_t col;	/* Current column (x-position) of cursor.  */
    uint32_t row;	/* Current row (y-position) of cursor.  */

#define CONS_CURSOR_INVISIBLE 0
#define CONS_CURSOR_NORMAL 1
#define CONS_CURSOR_VERY_VISIBLE 2
    uint32_t status;	/* Visibility status of cursor.  */
  } cursor;

  /* Don't use this, use ((wchar_t *) cons_display +
     cons_display.screen.matrix) instead.  This will make your client
     upward compatible with future versions of this interface.  */
  wchar_t _matrix[0];
};


#endif	/* _HURD_CONSOLE_H */
