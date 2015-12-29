/* console.h -- Public interface to the console server.
   Copyright (C) 2002,10 Free Software Foundation, Inc.
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
#include <string.h>
#include <wchar.h>

typedef enum
  {
    CONS_COLOR_BLACK = 0, CONS_COLOR_RED, CONS_COLOR_GREEN, CONS_COLOR_YELLOW,
    CONS_COLOR_BLUE, CONS_COLOR_MAGENTA, CONS_COLOR_CYAN, CONS_COLOR_WHITE
  } cons_color_t;
#define CONS_COLOR_MAX (CONS_COLOR_WHITE)

typedef struct
{
  /* The intensity is traditionally a color attribute.  */
#define CONS_ATTR_INTENSITY_NORMAL	000000000000
#define CONS_ATTR_INTENSITY_BOLD	000000000001
#define CONS_ATTR_INTENSITY_DIM		000000000002
  uint32_t intensity : 2;

  uint32_t underlined : 1;
  uint32_t blinking : 1;
  uint32_t reversed : 1;
  uint32_t concealed : 1;

  /* Color attributes.  */
  uint32_t bgcol : 3;
  uint32_t fgcol : 3;

  /* Font attributes.  */
  uint32_t italic : 1;
  uint32_t bold : 1;
} conchar_attr_t; 

static inline int
conchar_attr_equal (conchar_attr_t *c1, conchar_attr_t *c2)
{
  return !memcmp (c1, c2, sizeof (conchar_attr_t));
}

/* We support double-width characters by using an extra bit to identify the
   continuation in the character matrix.  The constants below document our
   usage of wchar_t.  */
#define CONS_WCHAR_MASK       ((wchar_t) 0x401fffff)
#define CONS_WCHAR_CONTINUED  ((wchar_t) 0x40000000)

typedef struct
{
  wchar_t chr;
  conchar_attr_t attr;
} conchar_t;

typedef union
{
  struct
  {
    /* Only the first 31 bits are available (see WHAT.not_matrix).  */
    uint32_t start;
    uint32_t end;
  } matrix;
  struct
  {
    uint32_t cursor_pos : 1;
    uint32_t cursor_status : 1;
    uint32_t screen_cur_line : 1;
    uint32_t screen_scr_lines : 1;
    uint32_t bell_audible : 1;
    uint32_t bell_visible : 1;
    uint32_t flags : 1;
    uint32_t _unused : 24;
    uint32_t not_matrix : 1;
    /* Here are 32 more unused bits.  */
  } what;
} cons_change_t;

struct cons_display
{
#define CONS_MAGIC 0x48555244	/* Hex for "HURD".  */
  uint32_t magic;		/* CONS_MAGIC, use to detect
				   endianness.  */
#define CONS_VERSION_MAJ 0x0
#define CONS_VERSION_MAJ_SHIFT 16
#define CONS_VERSION_AGE 0x0
  uint32_t version;		/* Version of interface.  Lower 16
				   bits define the age, upper 16 bits
				   the major version.  */


  /* Various one bit flags that don't deserve their own field.  */

  /* The output is stopped.  The client can display the status of this
     flag, but should not otherwise interpret it.  */
#define CONS_FLAGS_SCROLL_LOCK	0x00000001

  /* Tracking mouse events is requested.  See CONS_MOUSE_* macros
     further down.  */
#define CONS_FLAGS_TRACK_MOUSE	0x00000002

  uint32_t flags;


  struct
  {
    uint32_t width;	/* Width of screen matrix.  */
    uint32_t lines;	/* Length of whole matrix.  */
    uint32_t cur_line;	/* Virtual start of visible area.  Needs to be
			   taken module LINES to get the real start of
			   visible area in the matrix.  This is only
			   ever increased by the server, so clients
			   can optimize scrolling.  */
    uint32_t scr_lines;	/* Number of lines in scrollback buffer
			   preceding CUR_LINE.  */
    uint32_t height;	/* Number of lines in visible area following
			   (and including) CUR_LINE.  */
    uint32_t matrix;	/* Index (in uint32_t) of the beginning of
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

  struct
  {
    uint32_t audible;	/* Audible bell.  */
    uint32_t visible;	/* Visible bell.  */
  } bell;

  struct
  {
    uint32_t buffer;	/* Index (in uint32_t) of the beginning of the
			   changes buffer in this structure.  */
    uint32_t length;	/* Length of buffer.  */
    uint32_t written;	/* Number of records written by server.  */

#define _CONS_CHANGES_LENGTH 512
    cons_change_t _buffer[_CONS_CHANGES_LENGTH];
  } changes;

  /* Don't use this, use ((wchar_t *) cons_display +
     cons_display.screen.matrix) instead.  This will make your client
     upward compatible with future versions of this interface.  */
  conchar_t _matrix[0];
};


/* The console server will use the following UCS-4 characters for the
   specified terminal graphic characters.  If the display driver is
   UCS-4 capable, it can print them without interpretation.  */

/* ACS_BLOCK maps to FULL BLOCK.  */
#define CONS_CHAR_BLOCK		((wchar_t) 0x2588)
/* ACS_DIAMOND maps to BLACK DIAMOND.  */
#define CONS_CHAR_DIAMOND	((wchar_t) 0x25c6)
/* ACS_CKBOARD maps to MEDIUM SHADE.  */
#define CONS_CHAR_CKBOARD	((wchar_t) 0x2592)
/* ACS_BOARD maps to LIGHT SHADE.  */
#define CONS_CHAR_BOARD		((wchar_t) 0x2591)
/* ACS_BULLET maps to BULLET (Linux maps it to MIDDLE DOT 0x00b7).  */
#define CONS_CHAR_BULLET	((wchar_t) 0x2022)
/* ACS_STERLING maps to POUND STERLING.  */
#define CONS_CHAR_STERLING	((wchar_t) 0x00a3)
/* ACS_DEGREE maps to DEGREE SIGN.  */
#define CONS_CHAR_DEGREE	((wchar_t) 0x00b0)
/* ACS_PLMINUS maps to PLUS-MINUS SIGN.  */
#define CONS_CHAR_PLMINUS	((wchar_t) 0x00b1)
/* ACS_PI maps to GREEK SMALL LETTER PI.  */
#define CONS_CHAR_PI		((wchar_t) 0x03c0)
/* ACS_LANTERN maps to BLACK HOURGLASS.  XXX Is this appropriate?  */
#define CONS_CHAR_LANTERN	((wchar_t) 0x29d7)

/* ACS_RARROW maps to RIGHTWARDS ARROW.  */
#define CONS_CHAR_RARROW	((wchar_t) 0x2192)
/* ACS_LARROW maps to LEFTWARDS ARROW.  */
#define CONS_CHAR_LARROW	((wchar_t) 0x2190)
/* ACS_UARROW maps to UPWARDS ARROW.  */
#define CONS_CHAR_UARROW	((wchar_t) 0x2191)
/* ACS_DARROW maps to DOWNWARDS ARROW.  */
#define CONS_CHAR_DARROW	((wchar_t) 0x2193)

/* ACS_LRCORNER maps to BOX DRAWINGS LIGHT UP AND LEFT.  */
#define CONS_CHAR_LRCORNER	((wchar_t) 0x2518)
/* ACS_URCORNER maps to BOX DRAWINGS LIGHT DOWN AND LEFT.  */
#define CONS_CHAR_URCORNER	((wchar_t) 0x2510)
/* ACS_ULCORNER maps to BOX DRAWINGS LIGHT DOWN AND RIGHT.  */
#define CONS_CHAR_ULCORNER	((wchar_t) 0x250c)
/* ACS_LLCORNER maps to BOX DRAWINGS LIGHT UP AND RIGHT.  */
#define CONS_CHAR_LLCORNER	((wchar_t) 0x2514)
/* ACS_PLUS maps to BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL.  */
#define CONS_CHAR_PLUS		((wchar_t) 0x253c)
/* ACS_HLINE maps to BOX DRAWINGS LIGHT HORIZONTAL.  */
#define CONS_CHAR_HLINE		((wchar_t) 0x2500)
/* ACS_LTEE maps to BOX DRAWINGS LIGHT VERTICAL AND RIGHT.  */
#define CONS_CHAR_LTEE		((wchar_t) 0x251c)
/* ACS_RTEE maps to BOX DRAWINGS LIGHT VERTICAL AND LEFT.  */
#define CONS_CHAR_RTEE		((wchar_t) 0x2524)
/* ACS_BTEE maps to BOX DRAWINGS LIGHT UP AND HORIZONTAL.  */
#define CONS_CHAR_BTEE		((wchar_t) 0x2534)
/* ACS_TTEE maps to BOX DRAWINGS LIGHT DOWN AND HORIZONTAL.  */
#define CONS_CHAR_TTEE		((wchar_t) 0x252c)
/* ACS_VLINE maps to BOX DRAWINGS LIGHT VERTICAL.  */
#define CONS_CHAR_VLINE		((wchar_t) 0x2502)

/* ACS_S1 maps to HORIZONTAL SCAN LINE-1.  */
#define CONS_CHAR_S1		((wchar_t) 0x23ba)
/* ACS_S3 maps to HORIZONTAL SCAN LINE-3.  */
#define CONS_CHAR_S3		((wchar_t) 0x23bb)
/* ACS_S7 maps to HORIZONTAL SCAN LINE-1.  */
#define CONS_CHAR_S7		((wchar_t) 0x23bc)
/* ACS_S9 maps to HORIZONTAL SCAN LINE-1.  */
#define CONS_CHAR_S9		((wchar_t) 0x23bd)

/* ACS_NEQUAL maps to NOT EQUAL TO.  */
#define CONS_CHAR_NEQUAL	((wchar_t) 0x2260)
/* ACS_LEQUAL maps to LESS-THAN OR EQUAL TO.  */
#define CONS_CHAR_LEQUAL	((wchar_t) 0x2264)
/* ACS_GEQUAL maps to GREATER-THAN OR EQUAL TO.  */
#define CONS_CHAR_GEQUAL	((wchar_t) 0x2265)



/* The input driver should emit these escape sequences for special
   keys which don't represent characters in UTF-8.  */
#define CONS_KEY_UP		"\eOA"		/* Cursor up.  */
#define CONS_KEY_DOWN		"\eOB"		/* Cursor down.  */
#define CONS_KEY_RIGHT		"\eOC"		/* Cursor right.  */
#define CONS_KEY_LEFT		"\eOD"		/* Cursor left.  */
#define CONS_KEY_BACKSPACE	"\177"		/* Backspace key.  */
#define CONS_KEY_F1		"\eOP"		/* Function key 1.  */
#define CONS_KEY_F2		"\eOQ"		/* Function key 2.  */
#define CONS_KEY_F3		"\eOR"		/* Function key 3.  */
#define CONS_KEY_F4		"\eOS"		/* Function key 4.  */
#define CONS_KEY_F5		"\e[15~"	/* Function key 5.  */
#define CONS_KEY_F6		"\e[17~"	/* Function key 6.  */
#define CONS_KEY_F7		"\e[18~"	/* Function key 7.  */
#define CONS_KEY_F8		"\e[19~"	/* Function key 8.  */
#define CONS_KEY_F9		"\e[20~"	/* Function key 9.  */
#define CONS_KEY_F10		"\e[21~"	/* Function key 10.  */
#define CONS_KEY_F11		"\e[23~"	/* Function key 11.  */
#define CONS_KEY_F12		"\e[24~"	/* Function key 12.  */
#define CONS_KEY_F13		"\e[25~"	/* Function key 13.  */
#define CONS_KEY_F14		"\e[26~"	/* Function key 14.  */
#define CONS_KEY_F15		"\e[28~"	/* Function key 15.  */
#define CONS_KEY_F16		"\e[29~"	/* Function key 16.  */
#define CONS_KEY_F17		"\e[31~"	/* Function key 17.  */
#define CONS_KEY_F18		"\e[32~"	/* Function key 18.  */
#define CONS_KEY_F19		"\e[33~"	/* Function key 19.  */
#define CONS_KEY_F20		"\e[34~"	/* Function key 20.  */
#define CONS_KEY_HOME		"\e[1~"		/* Home key.  */
#define CONS_KEY_IC		"\e[2~"		/* Insert char mode.  */
#define CONS_KEY_DC		"\e[3~"		/* Delete character.  */
#define CONS_KEY_END		"\e[4~"		/* End key.  */
#define CONS_KEY_PPAGE		"\e[5~"		/* Previous page.  */
#define CONS_KEY_NPAGE		"\e[6~"		/* Next page.  */
#define CONS_KEY_BTAB		"\e[Z"		/* Back tab key.  */
#define CONS_KEY_B2		"\e[G"		/* Center of keypad.  */

/* Mouse support is compatible to xterm's mouse tracking feature.  */

#define CONS_MOUSE_BUTTON_MASK	0x03
#define CONS_MOUSE_BUTTON1	0x00
#define CONS_MOUSE_BUTTON2	0x01
#define CONS_MOUSE_BUTTON3	0x02
#define CONS_MOUSE_RELEASE	0x03
#define CONS_MOUSE_MOD_MASK	0x1c
#define CONS_MOUSE_MOD_SHIFT	0x04
#define CONS_MOUSE_MOD_META	0x08
#define CONS_MOUSE_MOD_CTRL	0x10

/* Screen positions are offset by this value.  */
#define CONS_MOUSE_OFFSET_BASE	0x20

#define CONS_MOUSE_EVENT_LENGTH 6
#define CONS_MOUSE_EVENT_PREFIX "\e[M"

/* This macro populates STR with the mouse event EVENT at position X
   and Y, and returns 1 if successul and 0 if X or Y is out of
   range.  X and Y start from 0.  */
#define CONS_MOUSE_EVENT(str,event,x,y)					\
  (((int)(x) < 0 || (int)(x) + CONS_MOUSE_OFFSET_BASE > 255		\
   || (int)(y) < 0 || (int)(y) + CONS_MOUSE_OFFSET_BASE > 255) ? 0	\
   : ((*(str) = CONS_MOUSE_EVENT_PREFIX[0]),				\
     (*((str) + 1) = CONS_MOUSE_EVENT_PREFIX[1]),			\
     (*((str) + 2) = CONS_MOUSE_EVENT_PREFIX[2]),			\
     (*((str) + 3) = (char)((int)(event) + CONS_MOUSE_OFFSET_BASE)),	\
     (*((str) + 4) = (char)((int)(x) + CONS_MOUSE_OFFSET_BASE)),	\
     (*((str) + 5) = (char)((int)(y) + CONS_MOUSE_OFFSET_BASE), 1)))

#endif	/* _HURD_CONSOLE_H */
