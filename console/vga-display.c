/* vga-display.c - The VGA device dependant part of a (virtual) console.
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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <iconv.h>
#include <argp.h>
#include <string.h>

#include <sys/io.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <cthreads.h>

#include "vga.h"
#include "dynafont.h"
#include "display-drv.h"


struct vga_display_console
{
  /* The lock for the vga display console structure.  */
  struct mutex lock;

  /* The state of the conversion of output characters.  */
  iconv_t cd;

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
    } parse_state;

  /* How many parameters an escape sequence may have.  */
#define PARSE_MAX_PARAMS 10
  int parse_params[PARSE_MAX_PARAMS];
  int parse_nparams;

  /* The VGA font for this virtual console.  */
  dynafont_t df;

  /* The complete video buffer, including the scroll back buffer.  */
  char *video_buffer;

  /* The size of the video buffer, in lines.  */
  int video_buffer_lines;

  /* The top most line of the screen in the video buffer.  */
  int current_line;

  /* The number of lines scrolled back.  */
  int scrolling;

  /* Maximum number of lines scrolled back.  */
  int scrolling_max;

  /* True if the cursor is not displayed.  */
  int cursor_off;

  int width;
  int height;
  int cursor_x;
  int cursor_y;
  /* Current attribute.  */
  char attr;
};


/* Protects the VGA hardware and all global variables, like the active
   console.  */
static struct mutex vga_lock;

/* The currently active console.  */
static struct vga_display_console *active_console;


/* Initialize the subsystem.  */
error_t
vga_display_init ()
{
  mutex_init (&vga_lock);
  return vga_init ();
}


/* Create a new (virtual) console display, with the system encoding
   being ENCODING.  A failure at the first time this is called is
   critical.  Subsequent calls might return an error if multiple
   virtual consoles are not supported.  Further operations on this
   console will be called with the first parameter being *HOOK, which
   should be set to some unique identifier for this console.  */
error_t
vga_display_create (void **console, const char *encoding)
{
  error_t err = 0;
  struct vga_display_console *cons;

  cons = calloc (1, sizeof *cons);
  if (cons)
    return ENOMEM;
  mutex_init (&cons->lock);
  err = dynafont_new (0 /*XXX*/, 256, &cons->df);
  if (err)
    {
      free (cons);
      return err;
    }

  cons->width = 80;
  cons->height = 25;
  cons->video_buffer_lines = 200;  /* XXX For now.  */
  cons->video_buffer = malloc (cons->video_buffer_lines * cons->width * 2);
  if (!cons->video_buffer)
    {
      dynafont_free (cons->df);
      free (cons);
      return ENOMEM;
    }
  cons->current_line = 0;
  /* XXX Fill out the first cons->height lines of the buffer here (or
     later).  */

  /* WCHAR_T happens to be UCS-4 on the GNU system.  */
  cons->cd = iconv_open ("WCHAR_T", encoding);
  if (cons->cd == (iconv_t) -1)
    {
      err = errno;
      dynafont_free (cons->df);
      free (cons->video_buffer);
      free (cons);
    }
  return err;
}


/* Destroy the console CONSOLE.  The caller will first activate a
   different console.  */
void
vga_display_destroy (void *console)
{
  struct vga_display_console *cons = console;
  iconv_close (cons->cd);
  dynafont_free (cons->df);
  free (cons);
}


/* Update the cursor position on the screen.  Expects CONS and the VGA
   hardware to be locked and CONS to be the active console.  */
static void
vga_display_update_cursor (struct vga_display_console *cons)
{
  int pos;

  if (cons->cursor_off)
    return;

  pos = cons->cursor_x + (cons->cursor_y + cons->scrolling) * cons->width;

  vga_set_cursor (pos);
}


/* Update the screen content.  Expects CONS and the VGA hardware to be
   locked and CONS to be the active console.  XXX Maybe inline/macro.  */
static void
vga_display_update_screen (struct vga_display_console *cons)
{
  int start_line = cons->current_line - cons->scrolling;
  int lines;

  if (start_line < 0)
    start_line += cons->video_buffer_lines;
  lines = cons->video_buffer_lines - start_line;
  if (lines > cons->height)
    lines = cons->height;

  memcpy (vga_videomem, cons->video_buffer + start_line * cons->width * 2,
	  lines * cons->width * 2);
  if (lines < cons->height)
    memcpy (vga_videomem, cons->video_buffer,
	    (cons->height - lines) * cons->width * 2);
}


/* Change the active console to CONSOLE.  */
void
vga_display_activate (void *console, int key)
{
  struct vga_display_console *cons = console;

  mutex_lock (&cons->lock);
  mutex_lock (&vga_lock);
  active_console = cons;
  dynafont_activate (cons->df);
  vga_display_update_screen (cons);
  vga_display_update_cursor (cons);
  vga_display_cursor (!cons->cursor_off);
  mutex_unlock (&vga_lock);
  mutex_unlock (&cons->lock);
}


/* Scroll the console CONSOLE by the desired amount.  This is only a
   hint, the actual amount scrolled might depend on the capability of
   the subsystem.  Negative AMOUNT scrolls back in history.  */
error_t
vga_display_scroll (void *console, int amount)
{
  struct vga_display_console *cons = console;
  int old_scrolling;
  int cursor_state_change = 0;

  mutex_lock (&cons->lock);
  old_scrolling = cons->scrolling;
  cons->scrolling -= amount;
  if (cons->scrolling < 0)
    cons->scrolling = 0;
  else if (cons->scrolling > cons->scrolling_max)
    cons->scrolling = cons->scrolling_max;

  if (old_scrolling != cons->scrolling)
    {
      if ((!cons->cursor_off
	   && cons->cursor_y + cons->scrolling >= cons->height)
	  || (cons->cursor_off
	      && cons->cursor_y + cons->scrolling < cons->height))
	{
	  cons->cursor_off = !cons->cursor_off;
	  cursor_state_change = 1;
	}

      mutex_lock (&vga_lock);
      if (active_console == cons)
	{
	  /* XXX Replace this with a fast memmove in the video
	     memory.  */
	  vga_display_update_screen (cons);
	  if (cursor_state_change)
	    vga_display_cursor (!cons->cursor_off);
	}
      mutex_unlock (&vga_lock);
    }
  mutex_unlock (&cons->lock);
  return 0;
}


/* Change the font on the console CONSOLE to font.  The old font will
   not be accessed by the vga console subsystem anymore after this
   call completed.  */
void
vga_display_change_font (void *console, bdf_font_t font)
{
  struct vga_display_console *cons = console;
  mutex_lock (&cons->lock);
  dynafont_change_font (cons->df, font);
  mutex_unlock (&cons->lock);
}


#if 0
static void
limit_cursor (struct emu *emu)
{
  if (emu->x >= emu->width)
    emu->x = emu->width - 1;
  else if (emu->x < 0)
    emu->x = 0;

  if (emu->y >= emu->height)
    emu->y = emu->height - 1;
  else if (emu->y < 0)
    emu->y = 0;
}

static void
handle_esc_bracket_hl (struct emu *emu, int code, int flag)
{
  switch (code)
    {
    case 34:			/* cursor standout: <cnorm>, <cvvis> */
      emu->cursor_standout = flag;
      recalc_cursor (emu);
      break;
    }
}

static void
handle_esc_bracket_m (struct emu *emu, int code)
{
  switch (code)
    {
    case 0:		/* all attributes off: <sgr0> */
      emu->fg = emu->def_fg;
      emu->bg = emu->def_bg;
      emu->reverse = emu->bold = emu->blink =
	emu->invisible = emu->dim = emu->underline = 0;
      /* Cursor attributes aren't text attributes.  */
      break;
    case 1:		/* bold on: <bold> */
      emu->bold = 1;
      break;
    case 2:		/* dim on: <dim> */
      emu->dim = 1;
      break;
    case 4:		/* underline on: <smul> */
      emu->underline = 1;
      break;
    case 5:		/* blink on: <blink> */
      emu->blink = 1;
      break;
    case 7:		/* reverse video on: <rev>, <smso> */
      emu->reverse = 1;
      break;
    case 8:		/* concealed on: <invis> */
      emu->invisible = 1;
      break;
    case 21:		/* bold off */
      emu->bold = 0;
      break;
    case 22:		/* dim off */
      emu->dim = 0;
      break;
    case 24:		/* underline off: <rmul> */
      emu->underline = 0;
      break;
    case 25:		/* blink off */
      emu->blink = 0;
      break;
    case 27:		/* reverse video off: <rmso> */
      emu->reverse = 0;
      break;
    case 28:		/* concealed off */
      emu->invisible = 0;
      break;
      /* Case ranges are a GCC extension.  */
    case 30 ... 37:	/* set foreground color: <setaf> */
      emu->fg = code - 30;
      break;
    case 39:		/* default foreground color; ANSI? */
      emu->fg = emu->def_fg;
      break;
    case 40 ... 47:	/* set background color: <setab> */
      emu->bg = code - 40;
      break;
    case 49:		/* default background color; ANSI? */
      emu->bg = emu->def_bg;
      break;
    }
}

static void
handle_esc_bracket (struct emu *emu, char op)
{
  int i;
  switch (op)
    {
    case 'H': case 'f':		/* cursor position: <cup> */
      emu->x = emu->params[1] - 1;
      emu->y = emu->params[0] - 1;
      limit_cursor (emu);
      break;
    case 'G':			/* horizontal position: <hpa> */
      emu->x = emu->params[0] - 1;
      limit_cursor (emu);
      break;
    case 'F':			/* beginning of previous line */
      emu->x = 0;
      /* fall through */
    case 'A':			/* cursor up: <cuu>, <cuu1> */
      /* (a ?: b) is a GCC extension meaning (a ? a : b).  */
      emu->y -= (emu->params[0] ?: 1);
      limit_cursor (emu);
      break;
    case 'E':			/* beginning of next line */
      emu->x = 0;
      /* fall through */
    case 'B':			/* cursor down: <cud1>, <cud> */
      emu->y += (emu->params[0] ?: 1);
      limit_cursor (emu);
      break;
    case 'C':			/* cursor right: <cuf1>, <cuf> */
      emu->x += (emu->params[0] ?: 1);
      limit_cursor (emu);
      break;
    case 'D':			/* cursor left: <cub>, <cub1> */
      emu->x -= (emu->params[0] ?: 1);
      limit_cursor (emu);
      break;
    case 's':			/* save cursor position: <sc> */
      emu->saved_x = emu->x;
      emu->saved_y = emu->y;
      break;
    case 'u':			/* restore cursor position: <rc> */
      emu->x = emu->saved_x;
      emu->y = emu->saved_y;
      limit_cursor (emu); /* in case the screen was larger before */
      break;
    case 'h':			/* reset mode */
      for (i = 0; i < emu->nparams; ++i)
	handle_esc_bracket_hl (emu, emu->params[i], 0);
      break;
    case 'l':			/* set mode */
      for (i = 0; i < emu->nparams; ++i)
	handle_esc_bracket_hl (emu, emu->params[i], 1);
      break;
    case 'm':
      for (i = 0; i < emu->nparams; ++i)
	handle_esc_bracket_m (emu, emu->params[i]);
      recalc_attr (emu);
      break;
    case 'J':
      switch (emu->params[0])
	{
	case 0:			/* clear to end of screen: <ed> */
	  screen_fill (emu->screen, emu->x, emu->y, emu->width - emu->x, 1,
		       emu->attr | ' ');
	  screen_fill (emu->screen, 0, emu->y+1,
		       emu->width, emu->height - emu->y,
		       emu->attr | ' ');
	  break;
	case 1:			/* clear to beginning of screen */
	  screen_fill (emu->screen, 0, 0, emu->width, emu->y,
		       emu->attr | ' ');
	  screen_fill (emu->screen, 0, emu->y, emu->x + 1, 1,
		       emu->attr | ' ');
	  break;
	case 2:			/* clear entire screen */
	  screen_fill (emu->screen, 0, 0, emu->width, emu->height,
		       emu->attr | ' ');
	  break;
	}
      break;
    case 'K':
      switch (emu->params[0])
	{
	case 0:			/* clear to end of line: <el> */
	  screen_fill (emu->screen, emu->x, emu->y, emu->width - emu->x, 1,
		       emu->attr | ' ');
	  break;
	case 1:			/* clear to beginning of line: <el1> */
	  screen_fill (emu->screen, 0, emu->y, emu->x + 1, 1,
		       emu->attr | ' ');
	  break;
	case 2:			/* clear entire line */
	  screen_fill (emu->screen, 0, emu->y, emu->width, 1,
		       emu->attr | ' ');
	  break;
	}
      break;
    case 'L':			/* insert line(s): <il1>, <il> */
      screen_scroll_down (emu->screen, 0, emu->y,
			  emu->width, emu->height - emu->y,
			  emu->params[0] ?: 1, emu->attr | ' ');
      break;
    case 'M':			/* delete line(s): <dl1>, <dl> */
      screen_scroll_up (emu->screen, 0, emu->y,
			emu->width, emu->height - emu->y,
			emu->params[0] ?: 1, emu->attr | ' ');
      break;
    case '@':			/* insert character(s): <ich1>, <ich> */
      screen_scroll_right (emu->screen, emu->x, emu->y,
			   emu->width - emu->x, 1,
			   emu->params[0] ?: 1, emu->attr | ' ');
      break;
    case 'P':			/* delete character(s): <dch1>, <dch> */
      screen_scroll_left (emu->screen, emu->x, emu->y,
			  emu->width - emu->x, 1,
			  emu->params[0] ?: 1, emu->attr | ' ');
      break;
    case 'S':			/* scroll up: <ind>, <indn> */
      screen_scroll_up (emu->screen, 0, 0, emu->width, emu->height,
			emu->params[0] ?: 1, emu->attr | ' ');
      break;
    case 'T':			/* scroll down: <ri>, <rin> */
      screen_scroll_down (emu->screen, 0, 0, emu->width, emu->height,
			  emu->params[0] ?: 1, emu->attr | ' ');
      break;
    case 'X':			/* erase character(s): <ech> */
      screen_fill (emu->screen, emu->x, emu->y, emu->params[0] ?: 1, 1,
		   emu->attr | ' ');
      break;
    }
}

static void
handle_esc_bracket_question_hl (struct emu *emu, int code, int flag)
{
  switch (code)
    {
    case 25:			/* cursor invisibility: <civis>, <cnorm> */
      emu->cursor_invisible = flag;
      recalc_cursor (emu);
      break;
    }
}

static void
handle_esc_bracket_question (struct emu *emu, char op)
{
  int i;
  switch (op)
    {
    case 'h':			/* reset mode */
      for (i = 0; i < emu->nparams; ++i)
	handle_esc_bracket_question_hl (emu, emu->params[i], 0);
      break;
    case 'l':			/* set mode */
      for (i = 0; i < emu->nparams; ++i)
	handle_esc_bracket_question_hl (emu, emu->params[i], 1);
      break;
    }
}
#endif

/* Console must be locked.  */
static void
vga_display_output_one (struct vga_display_console *cons, wchar_t chr)
{
#if 0
  switch (cons->parse_state)
    {
    case STATE_NORMAL:
      switch (chr)
	  {
	  case '\r':
	    /* Carriage return: <cr>.  */
	    if (cons->cursor_x)
	      {
		cons->cursor_x = 0;
		mutex_lock (&vga_lock);
		if (active_console == cons)
		  vga_display_update_cursor (cons);
		mutex_unlock (&vga_lock);
	      }
	    break;
	  case '\n':
	    /* Cursor down: <cud1>, scroll up: <ind>.  */
	    if (cons->cursor_y < cons->height - 1)
	      {
		cons->cursor_y++;
		mutex_lock (&vga_lock);
		if (active_console == cons)
		  vga_display_update_cursor (cons);
		mutex_unlock (&vga_lock);
	      }
	    else
	      {
		if (cons->current_line == cons->video_buffer_lines - 1)
		  cons->current_line = 0;
		else
		  cons->current_line++;
		/* XXX Empty out current line with spaces.  */
		if (cons->scrolling_max
		    < cons->video_buffer_lines - cons->height)
		  cons->scrolling_max++;
		mutex_lock (&vga_lock);
		if (active_console == cons)
		  vga_display_update_screen (cons);
		mutex_unlock (&vga_lock);
	      }
	    break;
	  case '\b':
	    /* Cursor backward: <cub1>.  */
	    if (cons->cursor_x > 0 || cons->cursor_y > 0)
	      {
		if (cons->cursor_x > 0)
		  cons->cursor_x--;
		else
		  {
		    /* XXX This implements the <bw> functionality.
		       The alternative is to cut off and set x to 0.  */
		    cons->cursor_x = cons->width - 1;
		    cons->cursor_y--;
		  }
		mutex_lock (&vga_lock);
		if (active_console == cons)
		  vga_display_update_cursor (cons);
		mutex_unlock (&vga_lock);
	      }
	    break;
	  case '\t':		/* horizontal tab: <ht> */
	    cons->cursor_x = (cons->cursor_x | 7) + 1;
	    if (cons->cursor_x >= cons->width)
	      {
		cons->cursor_x = 0;
		if (cons->cursor_y < cons->height - 1)
		  cons->cursor_y++;
		else
		  {
		    if (cons->current_line == cons->video_buffer_lines - 1)
		      cons->current_line = 0;
		    else
		      cons->current_line++;
		    /* XXX Empty out current line with spaces.  */
		    if (cons->scrolling_max
			< cons->video_buffer_lines - cons->height)
		      cons->scrolling_max++;
		    mutex_lock (&vga_lock);
		    if (active_console == cons)
		      {
			vga_display_update_screen (cons);
			vga_display_update_cursor (cons);
		      }
		    mutex_unlock (&vga_lock);
		    /* Out.  */
		    break;
		  }
	      }
	    mutex_lock (&vga_lock);
	    if (active_console == cons)
	      vga_display_update_cursor (cons);
	    mutex_unlock (&vga_lock);
	    break;
	  case '\033':
	    cons->parse_state = STATE_ESC;
	    break;
	  case '\0':
	    /* Padding character: <pad>.  */
	    break;
	  default:
	    {
	      int charval = dynafont_lookup (cons->df, chr);
	      int line = (cons->current_line + cons->cursor_y)
		% cons->video_buffer_lines;

	      cons->video_buffer[(line * cons->width + cons->cursor_x) * 2]
		= charval & 0xff;
	      cons->video_buffer[(line * cons->width + cons->cursor_x) * 2 + 1]
		= cons->attr | (cons->size == 512 ? (charval >> 5) & 0x8 : 0);

	      /* XXX Taking the lock twice, once here, and once below.  */
	      if (cons->cursor_y + cons->scrolling < cons->height)
		{
		  mutex_lock (&vga_lock);
		  if (active_console == cons)
		    {
		      vga_videomem[((cons->cursor_y + cons->scrolling)
				    * cons->width
				    + cons->cursor_x) * 2] = charval;
		      vga_videomem[((cons->cursor_y + cons->scrolling)
				    * cons->width
				    + cons->cursor_x) * 2 + 1] = cons->attr;
		    }
		  mutex_unlock (&vga_lock);
		}

	      cons->cursor_x++;
	      if (cons->cursor_x == cons->height)
		{
		  cons->cursor_x = 0;
		  if (cons->cursor_y < cons->height - 1)
		    cons->cursor_y++;
		  else
		    {
		      if (cons->current_line == cons->video_buffer_lines - 1)
			cons->current_line = 0;
		      else
			cons->current_line++;
		      /* XXX Empty out current line with spaces.  */
		      if (cons->scrolling_max
			  < cons->video_buffer_lines - cons->height)
			cons->scrolling_max++;
		      mutex_lock (&vga_lock);
		      if (active_console == cons)
			{
			  vga_display_update_screen (cons);
			  vga_display_update_cursor (cons);
			}
		      mutex_unlock (&vga_lock);
		      /* Out.  */
		      break;
		    }
		}
	      mutex_lock (&vga_lock);
	      if (active_console == cons)
		vga_display_update_cursor (cons);
	      mutex_unlock (&vga_lock);
	      break;
	    }
	  }
      break;

      case STATE_ESC:
	switch (chr)
	  {
	  case '[':
	    cons->parse_state = STATE_ESC_BRACKET_INIT;
	    break;
	  case 'c':
	    /* Clear screen and home cursor: <clear>.  */
	    /* XXX */
	    //	    screen_fill (cons->screen, 0, 0, cons->width, cons->height,
	    //			 emu->attr | ' ');
	    cons->cursor_x = cons->cursor_y = 0;
	    cons->parse_state = STATE_NORMAL;
	    break;
	  default:
	    /* Unsupported escape sequence.  */
	    cons->parse_state = STATE_NORMAL;
	    break;
	  }
	break;

      case STATE_ESC_BRACKET_INIT:
	memset (&cons->parse_params, 0, sizeof cons->parse_params);
	cons->parse_nparams = 0;
	if (chr == '?')
	  {
	    cons->parse_state = STATE_ESC_BRACKET_QUESTION;
	    break;	/* Consume the question mark.  */
	  }
	else
	  cons->parse_state = STATE_ESC_BRACKET;
	/* Fall through.  */
      case STATE_ESC_BRACKET:
      case STATE_ESC_BRACKET_QUESTION:
	if (chr >= '0' && chr <= '9')
	  cons->parse_params[cons->parse_nparams]
	    = cons->parse_params[cons->parse_nparams]*10 + chr - '0';
	else if (chr == ';')
	  {
	    if (++(cons->parse_nparams) >= PARSE_MAX_PARAMS)
	      cons->parse_state = STATE_NORMAL; /* too many */
	  }
	else
	  {
	    cons->parse_nparams++;
	    if (cons->parse_state == STATE_ESC_BRACKET)
	      handle_esc_bracket (emu, chr);
	    else
	      handle_esc_bracket_question (emu, chr);
	    cons->parse_state = STATE_NORMAL;
	  }
	break;
    default:
      abort ();
    }
#endif
}


/* Output LENGTH bytes starting from BUFFER in the system encoding.
   Set BUFFER and LENGTH to the new values.  The exact semantics are
   just as in the iconv interface.  */
error_t
vga_display_output (void *console, char **buffer, size_t *length)
{
#define CONV_OUTBUF_SIZE 256
  struct vga_display_console *cons = console;
  error_t err = 0;

  mutex_lock (&cons->lock);
  while (!err && *length > 0)
    {
      size_t nconv;
      wchar_t outbuf[CONV_OUTBUF_SIZE];
      char *outptr = (char *) outbuf;
      size_t outsize = CONV_OUTBUF_SIZE;
      error_t saved_err;
      int i;

      nconv = iconv (cons->cd, buffer, length, &outptr, &outsize);
      saved_err = errno;

      /* First process all successfully converted characters.  */
      for (i = 0; i < CONV_OUTBUF_SIZE - outsize; i++)
	vga_display_output_one (cons, outbuf[i]);

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
  mutex_unlock (&cons->lock);
  return err;
}


void
vga_display_getsize (void *console, struct winsize *winsize)
{
  struct vga_display_console *cons = console;
  mutex_lock (&cons->lock);
  winsize->ws_row = cons->height;
  winsize->ws_col = cons->width;
  winsize->ws_xpixel = 0;
  winsize->ws_ypixel = 0;
  mutex_unlock (&cons->lock);
}


struct display_ops vga_display_ops =
{
  vga_display_init,
  vga_display_create,
  vga_display_destroy,
  vga_display_activate,
  vga_display_scroll,
  vga_display_output,
  vga_display_getsize
};
