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

#define CONS_COLOR_BLACK	0
#define CONS_COLOR_RED		1
#define CONS_COLOR_GREEN	2
#define CONS_COLOR_YELLOW	3
#define CONS_COLOR_BLUE		4
#define CONS_COLOR_MAGENTA	5
#define CONS_COLOR_CYAN		6
#define CONS_COLOR_WHITE	7

typedef struct
{
#define CONS_ATTR_INTENSITY_NORMAL	000000000000
#define CONS_ATTR_INTENSITY_BOLD	000000000001
#define CONS_ATTR_INTENSITY_DIM		000000000002
  uint32_t intensity : 2;
  uint32_t underlined : 1;
  uint32_t blinking : 1;
  uint32_t reversed : 1;
  uint32_t concealed : 1;
  uint32_t bgcol : 3;
  uint32_t fgcol : 3;
} conchar_attr_t;
  
typedef struct
{
  wchar_t chr;
  conchar_attr_t attr;
} conchar_t;

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
  conchar_t _matrix[0];
};


#define CONS_KEY_UP		"\e[A"		/* Cursor up.  */
#define CONS_KEY_DOWN		"\e[B"		/* Cursor down.  */
#define CONS_KEY_RIGHT		"\e[C"		/* Cursor right.  */
#define CONS_KEY_LEFT		"\e[D"		/* Cursor left.  */
#define CONS_KEY_HOME		"\e[H"		/* Home key.  */
#define CONS_KEY_BACKSPACE	"\177"		/* Backspace key.  */
#define CONS_KEY_F1		"\eOP"		/* Function key 1.  */
#define CONS_KEY_F2		"\eOQ"		/* Function key 2.  */
#define CONS_KEY_F3		"\eOR"		/* Function key 3.  */
#define CONS_KEY_F4		"\eOS"		/* Function key 4.  */
#define CONS_KEY_F5		"\eOT"		/* Function key 5.  */
#define CONS_KEY_F6		"\eOU"		/* Function key 6.  */
#define CONS_KEY_F7		"\eOV"		/* Function key 7.  */
#define CONS_KEY_F8		"\eOW"		/* Function key 8.  */
#define CONS_KEY_F9		"\eOX"		/* Function key 9.  */
#define CONS_KEY_F10		"\eOY"		/* Function key 10.  */
#define CONS_KEY_DC		"\e[9"		/* Delete character.  */
#define CONS_KEY_NPAGE		"\e[U"		/* Next page.  */
#define CONS_KEY_PPAGE		"\e[V"		/* Previous page.  */
#define CONS_KEY_BTAB		"\e[Z"		/* Back tab key.  */
#define CONS_KEY_IC		"\e[@"		/* Insert char mode.  */
#define CONS_KEY_END		"\e[Y"		/* End key.  */

#endif	/* _HURD_CONSOLE_H */
