/* display.c - The display component of a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann and Kalle Olavi Niemitalo.

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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <wchar.h>
#include <iconv.h>
#include <argp.h>
#include <string.h>

#include <sys/io.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <cthreads.h>

#ifndef __STDC_ISO_10646__
#error It is required that wchar_t is UCS-4.
#endif

#include "display.h"


struct screen
{
  /* A screen matrix, including the scroll back buffer.  */
  wchar_t *matrix;

  /* The size of the screen, in lines.  */
  int lines;

  /* The top most line of the screen in the video buffer.  */
  int current_line;

  /* Maximum number of lines scrolled back.  */
  int scrolling_max;

  int width;
  int height;
};
typedef struct screen *screen_t;

struct parse
{
  /* The parsing state of output characters, needed to handle escape
     character sequences.  */
  enum
    {
      STATE_NORMAL = 0,
      /* An escape character has just been parsed.  */
      STATE_ESC,
      STATE_ESC_BRACKET_INIT,
      STATE_ESC_BRACKET,
      STATE_ESC_BRACKET_QUESTION
    } state;

  /* How many parameters an escape sequence may have.  */
#define PARSE_MAX_PARAMS 10
  int params[PARSE_MAX_PARAMS];
  int nparams;
};

struct cursor
{
  /* The visibility of the cursor.  */
  int status;
#define CURSOR_INVISIBLE 1
#define CURSOR_STANDOUT 2

  size_t x;
  size_t y;
  size_t saved_x;
  size_t saved_y;
};

struct display
{
  /* The lock for the virtual console display structure.  */
  struct mutex lock;

  /* The state of the conversion of output characters.  */
  iconv_t cd;

  struct parse parse;
  struct screen screen;
  struct cursor cursor;

  struct
  {
    /* Current attribute.  */
    char current;
    int fg;
    int bg;
    int def_fg;
    int def_bg;
    int reverse : 1;
    int bold : 1;
    int blink : 1;
    int invisible : 1;
    int dim : 1;
    int underline : 1;
  } attr;

  /* Indicates if OWNER_ID is initialized.  */
  int has_owner;
  /* Specifies the ID of the process that should receive the WINCH
     signal for this virtual console.  */
  int owner_id;
};


static error_t
screen_init (screen_t screen)
{
  screen->width = 80;
  screen->height = 25;
  screen->lines = 200;	/* XXX For now.  */
  screen->current_line = 0;
  screen->matrix = calloc (screen->lines * screen->width, sizeof (wchar_t));
  if (!screen->matrix)
    return ENOMEM;

  wmemset (screen->matrix, L' ', screen->height * screen->width);
  /* XXX Set attribute flags.  */
  return 0;
}

static void
screen_deinit (screen_t screen)
{
  free (screen->matrix);
}

static void
screen_fill (screen_t screen, size_t x, size_t y, size_t w, size_t h,
	     wchar_t chr, char attr)
{
  wchar_t *matrixp = screen->matrix + y * screen->width + x;

  while (h--)
    {
      /* XXX Set attribute flags.  */
      wmemset (matrixp, L' ', w);
      matrixp += screen->width;
    }

  /* XXX Flag screen change, but here?  */
}

static void
screen_scroll_up (screen_t screen, size_t x, size_t y, size_t w, size_t h,
		  int amt, wchar_t chr, char attr)
{
  wchar_t *matrixp = screen->matrix + y * screen->width + x;

  if (amt < 0)
    return;

  while (h-- > amt)
    {
      wmemcpy (matrixp, matrixp + amt * screen->width, w);
      matrixp += screen->width;
    }
  screen_fill (screen, x, y, w, h, chr, attr);
}

static void
screen_scroll_down (screen_t screen, size_t x, size_t y, size_t w, size_t h,
		    int amt, wchar_t chr, char attr)
{
  wchar_t *matrixp = screen->matrix + (y + h - 1) * screen->width + x;

  if (amt < 0)
    return;

  while (h-- > amt)
    {
      wmemcpy (matrixp, matrixp - amt * screen->width, w);
      matrixp -= screen->width;
    }
  screen_fill (screen, x, y, w, h, chr, attr);
}

static void
screen_scroll_left (screen_t screen, size_t x, size_t y, size_t w, size_t h,
		    int amt, wchar_t chr, char attr)
{
  int y;
  wchar_t *matrixp = screen->matrix + y * screen->width + x;

  if (amt < 0)
    return;
  if (amt > w)
    amt = w;

  for (y = 0; y < y + h; y++)
    {
      wmemmov (matrixp, matrixp + amt, w - amt);
      matrixp += screen->width;
    }
  screen_fill (screen, x + w - amt, y, amt, h, chr, attr);
}

static void
screen_scroll_right (screen_t screen, size_t x, size_t y, size_t w, size_t h,
		     int amt, wchar_t chr, char attr)
{
  int y;
  wchar_t *matrixp = screen->matrix + y * screen->width + x;

  if (amt < 0)
    return;
  if (amt > w)
    amt = w;

  for (y = 0; y < y + h; y++)
    {
      wmemmov (matrixp + amt, matrixp, w - amt);
      matrixp += screen->width;
    }
  screen_fill (screen, x, y, amt, h, chr, attr);
}


/* Create a new virtual console display, with the system encoding
   being ENCODING.  */
error_t
display_create (display_t *r_display, const char *encoding)
{
  error_t err = 0;
  display_t display;

  *r_display = NULL;
  display = calloc (1, sizeof *display);
  if (!display)
    return ENOMEM;

  mutex_init (&display->lock);
  err = screen_init (&display->screen);
  if (err)
    {
      free (display);
      return err;
    }

  /* WCHAR_T happens to be UCS-4 on the GNU system.  */
  display->cd = iconv_open ("WCHAR_T", encoding);
  if (display->cd == (iconv_t) -1)
    {
      err = errno;
      screen_deinit (&display->screen);
      free (display);
    }
  *r_display = display;
  return err;
}


/* Destroy the display DISPLAY.  */
void
display_destroy (display_t display)
{
  iconv_close (display->cd);
  screen_deinit (&display->screen);
  free (display);
}


static void
handle_esc_bracket_hl (display_t display, int code, int flag)
{
  switch (code)
    {
    case 34:
      /* Cursor standout: <cnorm>, <cvvis>.  */
      if (flag)
	display->cursor.status |= CURSOR_STANDOUT;
      else
	display->cursor.status &= ~CURSOR_STANDOUT;
      /* XXX Flag cursor status change.  */
      break;
    }
}

static void
handle_esc_bracket_m (display_t display, int code)
{
  switch (code)
    {
    case 0:
      /* All attributes off: <sgr0>.  */
      display->attr.fg = display->attr.def_fg;
      display->attr.bg = display->attr.def_bg;
      display->attr.reverse = display->attr.bold = display->attr.blink
	= display->attr.invisible = display->attr.dim
	= display->attr.underline = 0;
      /* Cursor attributes aren't text attributes.  */
      break;
    case 1:
      /* Bold on: <bold>.  */
      display->attr.bold = 1;
      break;
    case 2:
      /* Dim on: <dim>.  */
      display->attr.dim = 1;
      break;
    case 4:
      /* Underline on: <smul>.  */
      display->attr.underline = 1;
      break;
    case 5:
      /* Blink on: <blink>.  */
      display->attr.blink = 1;
      break;
    case 7:
      /* Reverse video on: <rev>, <smso>.  */
      display->attr.reverse = 1;
      break;
    case 8:
      /* Concealed on: <invis>.  */
      display->attr.invisible = 1;
      break;
    case 21:
      /* Bold Off.  */
      display->attr.bold = 0;
      break;
    case 22:
      /* Dim off.  */
      display->attr.dim = 0;
      break;
    case 24:
      /* Underline off: <rmul>.  */
      display->attr.underline = 0;
      break;
    case 25:
      /* Blink off.  */
      display->attr.blink = 0;
      break;
    case 27:
      /* Reverse video off: <rmso>.  */
      display->attr.reverse = 0;
      break;
    case 28:
      /* Concealed off.  */
      display->attr.invisible = 0;
      break;
    case 30 ... 37:
      /* Set foreground color: <setaf>.  */
      display->attr.fg = code - 30;
      break;
    case 39:
      /* Default foreground color; ANSI?.  */
      display->attr.fg = display->attr.def_fg;
      break;
    case 40 ... 47:
      /* Set background color: <setab>.  */
      display->attr.bg = code - 40;
      break;
    case 49:
      /* Default background color; ANSI?.  */
      display->attr.bg = display->attr.def_bg;
      break;
    }
  /* XXX */
  /* recalc_attr (display); */
}

static void
handle_esc_bracket (display_t display, char op)
{
  int i;

  static void limit_cursor (void)
    {
      if (display->cursor.x >= display->screen.width)
	display->cursor.x = display->screen.width - 1;
      else if (display->cursor.x < 0)
	display->cursor.x = 0;
      
      if (display->cursor.y >= display->screen.height)
	display->cursor.y = display->screen.height - 1;
      else if (display->cursor.y < 0)
	display->cursor.y = 0;

      /* XXX Flag cursor change.  */
    }

  switch (op)
    {
    case 'H':
    case 'f':
      /* Cursor position: <cup>.  */
      display->cursor.x = display->parse.params[1] - 1;
      display->cursor.y = display->parse.params[0] - 1;
      limit_cursor ();
      break;
    case 'G':
      /* Horizontal position: <hpa>.  */
      display->cursor.x = display->parse.params[0] - 1;
      limit_cursor ();
      break;
    case 'F':
      /* Beginning of previous line.  */
      display->cursor.x = 0;
      /* fall through */
    case 'A':
      /* Cursor up: <cuu>, <cuu1>.  */
      display->cursor.y -= (display->parse.params[0] ?: 1);
      limit_cursor ();
      break;
    case 'E':
      /* Beginning of next line.  */
      display->cursor.x = 0;
      /* Fall through.  */
    case 'B':
      /* Cursor down: <cud1>, <cud>.  */
      display->cursor.y += (display->parse.params[0] ?: 1);
      limit_cursor ();
      break;
    case 'C':
      /* Cursor right: <cuf1>, <cuf>.  */
      display->cursor.x += (display->parse.params[0] ?: 1);
      limit_cursor ();
      break;
    case 'D':
      /* Cursor left: <cub>, <cub1>.  */
      display->cursor.x -= (display->parse.params[0] ?: 1);
      limit_cursor ();
      break;
    case 's':
      /* Save cursor position: <sc>.  */
      display->cursor.saved_x = display->cursor.x;
      display->cursor.saved_y = display->cursor.y;
      break;
    case 'u':
      /* Restore cursor position: <rc>.  */
      display->cursor.x = display->cursor.saved_x;
      display->cursor.y = display->cursor.saved_y;
      /* In case the screen was larger before:  */
      limit_cursor ();
      break;
    case 'h':
      /* Reset mode.  */
      for (i = 0; i < display->parse.nparams; i++)
	handle_esc_bracket_hl (display, display->parse.params[i], 0);
      break;
    case 'l':
      /* Set mode.  */
      for (i = 0; i < display->parse.nparams; i++)
	handle_esc_bracket_hl (display, display->parse.params[i], 1);
      break;
    case 'm':
      for (i = 0; i < display->parse.nparams; i++)
	handle_esc_bracket_m (display, display->parse.params[i]);
      break;
    case 'J':
      switch (display->parse.params[0])
	{
	case 0:
	  /* Clear to end of screen: <ed>.  */
	  screen_fill (&display->screen, display->cursor.x, display->cursor.y,
		       display->screen.width - display->cursor.x, 1, L' ',
		       display->attr.current);
	  screen_fill (&display->screen, 0, display->cursor.y + 1,
		       display->screen.width,
		       display->screen.height - display->cursor.y,
			L' ', display->attr.current);
	  break;
	case 1:
	  /* Clear to beginning of screen.  */
	  screen_fill (&display->screen, 0, 0,
		       display->screen.width, display->cursor.y,
		       L' ', display->attr.current);
	  screen_fill (&display->screen, 0, display->cursor.y,
		       display->cursor.x + 1, 1,
		       L' ', display->attr.current);
	  break;
	case 2:
	  /* Clear entire screen.  */
	  screen_fill (&display->screen, 0, 0,
		       display->screen.width, display->screen.height,
		       L' ', display->attr.current);
	  break;
	}
      break;
    case 'K':
      switch (display->parse.params[0])
	{
	case 0:
	  /* Clear to end of line: <el>.  */
	  screen_fill (&display->screen, display->cursor.x, display->cursor.y,
		       display->screen.width - display->cursor.x, 1,
		       L' ', display->attr.current);
	  break;
	case 1:
	  /* Clear to beginning of line: <el1>.  */
	  screen_fill (&display->screen, 0, display->cursor.y,
		       display->cursor.x + 1, 1,
		       L' ', display->attr.current);
	  break;
	case 2:
	  /* Clear entire line.  */
	  screen_fill (&display->screen, 0, display->cursor.y,
		       display->screen.width, 1,
		       L' ', display->attr.current);
	  break;
	}
      break;
    case 'L':
      /* Insert line(s): <il1>, <il>.  */
      screen_scroll_down (&display->screen, 0, display->cursor.y,
			  display->screen.width,
			  display->screen.height - display->cursor.y,
			  display->parse.params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'M':
      /* Delete line(s): <dl1>, <dl>.  */
      screen_scroll_up (&display->screen, 0, display->cursor.y,
			display->screen.width,
			display->screen.height - display->cursor.y,
			display->parse.params[0] ?: 1,
			L' ', display->attr.current);
      break;
    case '@':
      /* Insert character(s): <ich1>, <ich>.  */
      screen_scroll_right (&display->screen, display->cursor.x,
			   display->cursor.y,
			   display->screen.width - display->cursor.x, 1,
			   display->parse.params[0] ?: 1,
			   L' ', display->attr.current);
      break;
    case 'P':
      /* Delete character(s): <dch1>, <dch>.  */
      screen_scroll_left (&display->screen, display->cursor.x,
			  display->cursor.y,
			  display->screen.width - display->cursor.x, 1,
			  display->parse.params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'S':
      /* Scroll up: <ind>, <indn>.  */
      screen_scroll_up (&display->screen, 0, 0,
			display->screen.width, display->screen.height,
			display->parse.params[0] ?: 1,
			L' ', display->attr.current);
      break;
    case 'T':
      /* Scroll down: <ri>, <rin>.  */
      screen_scroll_down (&display->screen, 0, 0,
			  display->screen.width, display->screen.height,
			  display->parse.params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'X':
      /* Erase character(s): <ech>.  */
      screen_fill (&display->screen, display->cursor.x, display->cursor.y,
		   display->parse.params[0] ?: 1, 1,
		   L' ', display->attr.current);
      break;
    }
}

static void
handle_esc_bracket_question_hl (display_t display, int code, int flag)
{
  switch (code)
    {
    case 25:
      /* Cursor invisibility: <civis>, <cnorm>.  */
      if (flag)
	display->cursor.status |= CURSOR_INVISIBLE;
      else
	display->cursor.status &= ~CURSOR_INVISIBLE;
      /* XXX Flag cursor status change.  */
      break;
    }
}


static void
handle_esc_bracket_question (display_t display, char op)
{
  int i;
  switch (op)
    {
    case 'h':
      /* Reset mode.  */
      for (i = 0; i < display->parse.nparams; ++i)
	handle_esc_bracket_question_hl (display, display->parse.params[i], 0);
      break;
    case 'l':
      /* Set mode.  */
      for (i = 0; i < display->parse.nparams; ++i)
	handle_esc_bracket_question_hl (display, display->parse.params[i], 1);
      break;
    }
}

/* Display must be locked.  */
static void
display_output_one (display_t display, wchar_t chr)
{
  void newline (void)
    {
      if (display->cursor.y < display->screen.height - 1)
	{
	  display->cursor.y++;
	  /* XXX Flag cursor update.  */
	}
      else
	{
	  if (display->screen.current_line == display->screen.lines - 1)
	    display->screen.current_line = 0;
	  else
	    display->screen.current_line++;
	  /* XXX Set attribute flags.  */
	  screen_fill (&display->screen, 0, display->screen.height - 1,
		       display->screen.width, 1, L' ', display->screen.width);
	  if (display->screen.scrolling_max <
	      display->screen.lines - display->screen.height)
	    display->screen.scrolling_max++;
	  /* XXX Flag current line change.  */
	  /* XXX Flag change of last line.  */
	  /* XXX Possibly flag change of length of scroll back buffer.  */
	}
    }

  switch (display->parse.state)
    {
    case STATE_NORMAL:
      switch (chr)
	{
	case L'\r':
	  /* Carriage return: <cr>.  */
	  if (display->cursor.x)
	    {
	      display->cursor.x = 0;
	      /* XXX Flag cursor update.  */
	    }
	  break;
	case L'\n':
	  /* Cursor down: <cud1>, scroll up: <ind>.  */
	  newline ();
	  break;
	case L'\b':
	  /* Cursor backward: <cub1>.  */
	  if (display->cursor.x > 0 || display->cursor.y > 0)
	    {
	      if (display->cursor.x > 0)
		display->cursor.x--;
	      else
		{
		  /* XXX This implements the <bw> functionality.
		     The alternative is to cut off and set x to 0.  */
		  display->cursor.x = display->screen.width - 1;
		  display->cursor.y--;
		}
	      /* XXX Flag cursor update.  */
	    }
	  break;
	case L'\t':
	  /* Horizontal tab: <ht> */
	  display->cursor.x = (display->cursor.x | 7) + 1;
	  if (display->cursor.x >= display->screen.width)
	    {
	      display->cursor.x = 0;
	      newline ();
	    }
	  /* XXX Flag cursor update.  */
	  break;
	case L'\033':
	  display->parse.state = STATE_ESC;
	  break;
	case L'\0':
	  /* Padding character: <pad>.  */
	  break;
	default:
	  {
	    int line = (display->screen.current_line + display->cursor.y)
	      % display->screen.lines;

	    /* XXX Set attribute flags.  */
	    display->screen.matrix[line * display->screen.width
				   + display->cursor.x] = chr;

	    display->cursor.x++;
	    if (display->cursor.x == display->screen.height)
	      {
		display->cursor.x = 0;
		newline ();
	      }
	  }
	  break;
	}
      break;

    case STATE_ESC:
      switch (chr)
	{
	case L'[':
	  display->parse.state = STATE_ESC_BRACKET_INIT;
	  break;
	case L'c':
	  /* Clear screen and home cursor: <clear>.  */
	  screen_fill (&display->screen, 0, 0,
		       display->screen.width, display->screen.height,
		       L' ', display->attr.current);
	  display->cursor.x = display->cursor.y = 0;
	  /* XXX Flag cursor change.  */
	  display->parse.state = STATE_NORMAL;
	  break;
	default:
	  /* Unsupported escape sequence.  */
	  display->parse.state = STATE_NORMAL;
	  break;
	}
      break;
      
    case STATE_ESC_BRACKET_INIT:
      memset (&display->parse.params, 0, sizeof display->parse.params);
      display->parse.nparams = 0;
      if (chr == '?')
	{
	  display->parse.state = STATE_ESC_BRACKET_QUESTION;
	  break;	/* Consume the question mark.  */
	}
      else
	display->parse.state = STATE_ESC_BRACKET;
      /* Fall through.  */
    case STATE_ESC_BRACKET:
    case STATE_ESC_BRACKET_QUESTION:
      if (chr >= '0' && chr <= '9')
	display->parse.params[display->parse.nparams]
	    = display->parse.params[display->parse.nparams]*10 + chr - '0';
      else if (chr == ';')
	{
	  if (++(display->parse.nparams) >= PARSE_MAX_PARAMS)
	    display->parse.state = STATE_NORMAL; /* too many */
	}
      else
	{
	  display->parse.nparams++;
	  if (display->parse.state == STATE_ESC_BRACKET)
	    handle_esc_bracket (display, chr);
	  else
	    handle_esc_bracket_question (display, chr);
	  display->parse.state = STATE_NORMAL;
	}
      break;
    default:
      abort ();
    }
}


/* Output LENGTH bytes starting from BUFFER in the system encoding.
   Set BUFFER and LENGTH to the new values.  The exact semantics are
   just as in the iconv interface.  */
error_t
display_output (display_t display, char **buffer, size_t *length)
{
#define CONV_OUTBUF_SIZE 256
  error_t err = 0;

  mutex_lock (&display->lock);
  while (!err && *length > 0)
    {
      size_t nconv;
      wchar_t outbuf[CONV_OUTBUF_SIZE];
      char *outptr = (char *) outbuf;
      size_t outsize = CONV_OUTBUF_SIZE;
      error_t saved_err;
      int i;

      nconv = iconv (display->cd, buffer, length, &outptr, &outsize);
      saved_err = errno;

      /* First process all successfully converted characters.  */
      for (i = 0; i < CONV_OUTBUF_SIZE - outsize; i++)
	display_output_one (display, outbuf[i]);

      if (nconv == (size_t) -1)
	{
	  /* Conversion didn't work out.  */
	  if (saved_err == EINVAL)
	    /* This is only an unfinished byte sequence at the end of
	       the input buffer.  */
	    break;
	  else if (saved_err != E2BIG)
	    err = saved_err;
	}
    }
  mutex_unlock (&display->lock);
  return err;
}


void
display_getsize (display_t display, struct winsize *winsize)
{
  mutex_lock (&display->lock);
  winsize->ws_row = display->screen.height;
  winsize->ws_col = display->screen.width;
  winsize->ws_xpixel = 0;
  winsize->ws_ypixel = 0;
  mutex_unlock (&display->lock);
}
