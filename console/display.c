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
#include <assert.h>
#include <error.h>

#include <sys/io.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <cthreads.h>

#include <hurd.h>
#include <hurd/pager.h>

#ifndef __STDC_ISO_10646__
#error It is required that wchar_t is UCS-4.
#endif

#include "console.h"
#include "display.h"


struct cursor
{
  u_int32_t saved_x;
  u_int32_t saved_y;
};
typedef struct cursor *cursor_t;

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
typedef struct parse *parse_t;

struct output
{
  /* The state of the conversion of output characters.  */
  iconv_t cd;
  /* The output queue holds the characters that are to be outputted.
     The conversion routine might refuse to handle some incomplete
     multi-byte or composed character at the end of the buffer, so we
     have to keep them around.  */
  int stopped;
  struct condition resumed;
  char *buffer;
  size_t allocated;
  size_t size;

  /* The parsing state of output characters.  */
  struct parse parse;
};
typedef struct output *output_t;

struct attr
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
};
typedef struct attr *attr_t;

struct user_pager_info
{
  display_t display;
  struct pager *p;
};

struct display
{
  /* The lock for the virtual console display structure.  */
  struct mutex lock;

  /* Indicates if OWNER_ID is initialized.  */
  int has_owner;
  /* Specifies the ID of the process that should receive the WINCH
     signal for this virtual console.  */
  int owner_id;

  struct cursor cursor;
  struct output output;
  struct attr attr;
  struct cons_display *user;

  struct user_pager_info *upi;  
  memory_object_t memobj;
  size_t memobj_size;
};


/* We need a separate bucket for the pager ports.  */
struct port_bucket *pager_bucket;

mach_port_t
display_get_filemap (display_t display, vm_prot_t prot)
{
  error_t err;

  /* Add a reference for each call, the caller will deallocate it.  */
  err = mach_port_mod_refs (mach_task_self (), display->memobj,
                            MACH_PORT_RIGHT_SEND, +1);
  assert_perror (err);

  return display->memobj;
}

/* Implement the pager_clear_user_data callback from the pager library. */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  free (upi);
}

/* XXX This is not good enough.  We actually need to provide a backing
   store.  */
error_t
pager_read_page (struct user_pager_info *pager, vm_offset_t page,
                 vm_address_t *buf, int *writelock)
{
  /* This is a read-only medium */
  *writelock = 1;

  *buf = (vm_address_t) mmap (0, vm_page_size, PROT_READ|PROT_WRITE,
			      MAP_ANON, 0, 0);
  return 0;
}

error_t
pager_write_page (struct user_pager_info *pager,
                  vm_offset_t page,
                  vm_address_t buf)
{
  /* XXX Implement me.  */
  assert (0);
}

error_t
pager_unlock_page (struct user_pager_info *pager,
                   vm_offset_t address)
{
  return 0;
}

/* Tell how big the file is. */
error_t
pager_report_extent (struct user_pager_info *upi,
                     vm_address_t *offset,
                     vm_size_t *size)
{
  display_t display = upi->display;
  *offset = 0;
  *size = display->memobj_size;
  return 0;
}

void
pager_dropweak (struct user_pager_info *upi)
{
}

/* A top-level function for the paging thread that just services paging
   requests.  */
static void
service_paging_requests (any_t arg)
{
  struct port_bucket *pager_bucket = arg;
  for (;;)
    ports_manage_port_operations_multithread (pager_bucket,
                                              pager_demuxer,
                                              1000 * 60 * 2,
                                              1000 * 60 * 10, 0);
}    


static error_t
user_create (display_t display, u_int32_t width, u_int32_t height,
	     u_int32_t lines)
{
  error_t err;
  struct cons_display *user;
  display->memobj_size = round_page (sizeof (struct cons_display) +
				   sizeof (u_int32_t) * width * lines);

  display->upi = malloc (sizeof (struct user_pager_info));
  if (!display->upi)
    return MACH_PORT_NULL;
  display->upi->display = display;
  /* 1 & MOCD correct? */
  display->upi->p = pager_create (display->upi, pager_bucket,
				  1, MEMORY_OBJECT_COPY_DELAY);
  if (display->upi->p == 0)
    {
      free (display->upi);
      return errno;
    }
  display->memobj = pager_get_port (display->upi->p);
  ports_port_deref (display->upi->p);

  mach_port_insert_right (mach_task_self (), display->memobj, display->memobj,
                          MACH_MSG_TYPE_MAKE_SEND);

  err = vm_map (mach_task_self (),
		(vm_address_t *) &user, (vm_size_t) display->memobj_size,
		(vm_address_t) 0,
		1 /* ! (flags & MAP_FIXED) */,
		display->memobj, 0 /* (vm_offset_t) offset */,
		0 /* ! (flags & MAP_SHARED) */,
                VM_PROT_READ | VM_PROT_WRITE,
                VM_PROT_READ | VM_PROT_WRITE,
                VM_INHERIT_NONE);
  if (err)
    {
      /* UPI will be cleaned up by libpager.  */
      mach_port_deallocate (mach_task_self (), display->memobj);
      return err;
    }
    
  user->magic = CONS_MAGIC;
  user->version = CONS_VERSION_MAJ << 16 | CONS_VERSION_AGE;
  user->screen.width = width;
  user->screen.height = height;
  user->screen.lines = lines;
  user->screen.cur_line = 0;
  user->screen.scr_lines = 0;
  user->screen.matrix = sizeof (struct cons_display) / sizeof (u_int32_t);
  user->cursor.col = 0;
  user->cursor.row = 0;
  user->cursor.status = CONS_CURSOR_NORMAL;
  wmemset (user->_matrix, L' ', user->screen.width * user->screen.lines);

  /* XXX Set attribute flags.  */
  display->user = user;
  return 0;
}

static void
user_destroy (display_t display)
{
  /* The pager will be deallocated by libpager.  */
  mach_port_deallocate (mach_task_self (), display->memobj);
}


#define MATRIX_POS(user,x,y) ((user)->_matrix \
    + (((user)->screen.cur_line + (y)) % (user)->screen.height) \
    * (user)->screen.width + (x))

static void
screen_fill (display_t display, size_t x, size_t y, size_t w, size_t h,
	     wchar_t chr, char attr)
{
  struct cons_display *user = display->user;
  wchar_t *matrixp = MATRIX_POS (user, x, y);

  while (h--)
    {
      /* XXX Set attribute flags.  */
      wmemset (matrixp, L' ', w);
      matrixp += user->screen.width;
    }

  /* XXX Flag screen change, but here or where else?  */
}

static void
screen_scroll_up (display_t display, size_t x, size_t y, size_t w, size_t h,
		  int amt, wchar_t chr, char attr)
{
  struct cons_display *user = display->user;
  wchar_t *matrixp = MATRIX_POS (user, x, y);

  if (amt < 0)
    return;

  while (h-- > amt)
    {
      wmemcpy (matrixp, matrixp + amt * user->screen.width, w);
      matrixp += user->screen.width;
    }
  screen_fill (display, x, y, w, h, chr, attr);
}

static void
screen_scroll_down (display_t display, size_t x, size_t y, size_t w, size_t h,
		    int amt, wchar_t chr, char attr)
{
  struct cons_display *user = display->user;
  wchar_t *matrixp = MATRIX_POS (user, x, y + h - 1);

  if (amt < 0)
    return;

  while (h-- > amt)
    {
      wmemcpy (matrixp, matrixp - amt * user->screen.width, w);
      matrixp -= user->screen.width;
    }
  screen_fill (display, x, y, w, h, chr, attr);
}

static void
screen_scroll_left (display_t display, size_t x, size_t y, size_t w, size_t h,
		    int amt, wchar_t chr, char attr)
{
  struct cons_display *user = display->user;
  wchar_t *matrixp = MATRIX_POS (user, x, y);
  int i;

  if (amt < 0)
    return;
  if (amt > w)
    amt = w;

  for (i = 0; i < y + h; i++)
    {
      wmemmove (matrixp, matrixp + amt, w - amt);
      matrixp += user->screen.width;
    }
  screen_fill (display, x + w - amt, y, amt, h, chr, attr);
}

static void
screen_scroll_right (display_t display, size_t x, size_t y, size_t w, size_t h,
		     int amt, wchar_t chr, char attr)
{
  struct cons_display *user = display->user;
  wchar_t *matrixp = MATRIX_POS (user, x, y);
  int i;

  if (amt < 0)
    return;
  if (amt > w)
    amt = w;

  for (i = 0; i < y + h; i++)
    {
      wmemmove (matrixp + amt, matrixp, w - amt);
      matrixp += user->screen.width;
    }
  screen_fill (display, x, y, amt, h, chr, attr);
}


static error_t
output_init (output_t output, const char *encoding)
{
  condition_init (&output->resumed);
  output->stopped = 0;
  output->buffer = NULL;
  output->allocated = 0;
  output->size = 0;

  /* WCHAR_T happens to be UCS-4 on the GNU system.  */
  output->cd = iconv_open ("WCHAR_T", encoding);
  if (output->cd == (iconv_t) -1)
    return errno;
  return 0;
}

static void
output_deinit (output_t output)
{
  iconv_close (output->cd);
}


static void
handle_esc_bracket_hl (display_t display, int code, int flag)
{
  switch (code)
    {
    case 34:
      /* Cursor standout: <cnorm>, <cvvis>.  */
      if (flag)
	display->user->cursor.status = CONS_CURSOR_VERY_VISIBLE;
      else
	display->user->cursor.status = CONS_CURSOR_NORMAL;
      /* XXX Flag cursor status change.  */
      break;
    }
}

static void
handle_esc_bracket_m (attr_t attr, int code)
{
  switch (code)
    {
    case 0:
      /* All attributes off: <sgr0>.  */
      attr->fg = attr->def_fg;
      attr->bg = attr->def_bg;
      attr->reverse = attr->bold = attr->blink
	= attr->invisible = attr->dim
	= attr->underline = 0;
      /* Cursor attributes aren't text attributes.  */
      break;
    case 1:
      /* Bold on: <bold>.  */
      attr->bold = 1;
      break;
    case 2:
      /* Dim on: <dim>.  */
      attr->dim = 1;
      break;
    case 4:
      /* Underline on: <smul>.  */
      attr->underline = 1;
      break;
    case 5:
      /* Blink on: <blink>.  */
      attr->blink = 1;
      break;
    case 7:
      /* Reverse video on: <rev>, <smso>.  */
      attr->reverse = 1;
      break;
    case 8:
      /* Concealed on: <invis>.  */
      attr->invisible = 1;
      break;
    case 21:
      /* Bold Off.  */
      attr->bold = 0;
      break;
    case 22:
      /* Dim off.  */
      attr->dim = 0;
      break;
    case 24:
      /* Underline off: <rmul>.  */
      attr->underline = 0;
      break;
    case 25:
      /* Blink off.  */
      attr->blink = 0;
      break;
    case 27:
      /* Reverse video off: <rmso>.  */
      attr->reverse = 0;
      break;
    case 28:
      /* Concealed off.  */
      attr->invisible = 0;
      break;
    case 30 ... 37:
      /* Set foreground color: <setaf>.  */
      attr->fg = code - 30;
      break;
    case 39:
      /* Default foreground color; ANSI?.  */
      attr->fg = attr->def_fg;
      break;
    case 40 ... 47:
      /* Set background color: <setab>.  */
      attr->bg = code - 40;
      break;
    case 49:
      /* Default background color; ANSI?.  */
      attr->bg = attr->def_bg;
      break;
    }
  /* XXX */
  /* recalc_attr (display); */
}

static void
handle_esc_bracket (display_t display, char op)
{
  struct cons_display *user = display->user;
  parse_t parse = &display->output.parse;
  int i;

  static void limit_cursor (void)
    {
      if (user->cursor.col >= user->screen.width)
	user->cursor.col = user->screen.width - 1;
      else if (user->cursor.col < 0)
	user->cursor.col = 0;
      
      if (user->cursor.row >= user->screen.height)
	user->cursor.row = user->screen.height - 1;
      else if (user->cursor.row < 0)
	user->cursor.row = 0;

      /* XXX Flag cursor change.  */
    }

  switch (op)
    {
    case 'H':
    case 'f':
      /* Cursor position: <cup>.  */
      user->cursor.col = parse->params[1] - 1;
      user->cursor.row = parse->params[0] - 1;
      limit_cursor ();
      break;
    case 'G':
      /* Horizontal position: <hpa>.  */
      user->cursor.col = parse->params[0] - 1;
      limit_cursor ();
      break;
    case 'F':
      /* Beginning of previous line.  */
      user->cursor.col = 0;
      /* fall through */
    case 'A':
      /* Cursor up: <cuu>, <cuu1>.  */
      user->cursor.row -= (parse->params[0] ?: 1);
      limit_cursor ();
      break;
    case 'E':
      /* Beginning of next line.  */
      user->cursor.col = 0;
      /* Fall through.  */
    case 'B':
      /* Cursor down: <cud1>, <cud>.  */
      user->cursor.row += (parse->params[0] ?: 1);
      limit_cursor ();
      break;
    case 'C':
      /* Cursor right: <cuf1>, <cuf>.  */
      user->cursor.col += (parse->params[0] ?: 1);
      limit_cursor ();
      break;
    case 'D':
      /* Cursor left: <cub>, <cub1>.  */
      user->cursor.col -= (parse->params[0] ?: 1);
      limit_cursor ();
      break;
    case 's':
      /* Save cursor position: <sc>.  */
      display->cursor.saved_x = user->cursor.col;
      display->cursor.saved_y = user->cursor.row;
      break;
    case 'u':
      /* Restore cursor position: <rc>.  */
      user->cursor.col = display->cursor.saved_x;
      user->cursor.row = display->cursor.saved_y;
      /* In case the screen was larger before:  */
      limit_cursor ();
      break;
    case 'h':
      /* Reset mode.  */
      for (i = 0; i < parse->nparams; i++)
	handle_esc_bracket_hl (display, parse->params[i], 0);
      break;
    case 'l':
      /* Set mode.  */
      for (i = 0; i < parse->nparams; i++)
	handle_esc_bracket_hl (display, parse->params[i], 1);
      break;
    case 'm':
      for (i = 0; i < parse->nparams; i++)
	handle_esc_bracket_m (&display->attr, parse->params[i]);
      break;
    case 'J':
      switch (parse->params[0])
	{
	case 0:
	  /* Clear to end of screen: <ed>.  */
	  screen_fill (display, user->cursor.col, user->cursor.row,
		       user->screen.width - user->cursor.col, 1, L' ',
		       display->attr.current);
	  screen_fill (display, 0, user->cursor.row + 1,
		       user->screen.width,
		       user->screen.height - user->cursor.row,
			L' ', display->attr.current);
	  break;
	case 1:
	  /* Clear to beginning of screen.  */
	  screen_fill (display, 0, 0,
		       user->screen.width, user->cursor.row,
		       L' ', display->attr.current);
	  screen_fill (display, 0, user->cursor.row,
		       user->cursor.col + 1, 1,
		       L' ', display->attr.current);
	  break;
	case 2:
	  /* Clear entire screen.  */
	  screen_fill (display, 0, 0,
		       user->screen.width, user->screen.height,
		       L' ', display->attr.current);
	  break;
	}
      break;
    case 'K':
      switch (parse->params[0])
	{
	case 0:
	  /* Clear to end of line: <el>.  */
	  screen_fill (display, user->cursor.col, user->cursor.row,
		       user->screen.width - user->cursor.col, 1,
		       L' ', display->attr.current);
	  break;
	case 1:
	  /* Clear to beginning of line: <el1>.  */
	  screen_fill (display, 0, user->cursor.row,
		       user->cursor.col + 1, 1,
		       L' ', display->attr.current);
	  break;
	case 2:
	  /* Clear entire line.  */
	  screen_fill (display, 0, user->cursor.row,
		       user->screen.width, 1,
		       L' ', display->attr.current);
	  break;
	}
      break;
    case 'L':
      /* Insert line(s): <il1>, <il>.  */
      screen_scroll_down (display, 0, user->cursor.row,
			  user->screen.width,
			  user->screen.height - user->cursor.row,
			  parse->params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'M':
      /* Delete line(s): <dl1>, <dl>.  */
      screen_scroll_up (display, 0, user->cursor.row,
			user->screen.width,
			user->screen.height - user->cursor.row,
			parse->params[0] ?: 1,
			L' ', display->attr.current);
      break;
    case '@':
      /* Insert character(s): <ich1>, <ich>.  */
      screen_scroll_right (display, user->cursor.col,
			   user->cursor.row,
			   user->screen.width - user->cursor.col, 1,
			   parse->params[0] ?: 1,
			   L' ', display->attr.current);
      break;
    case 'P':
      /* Delete character(s): <dch1>, <dch>.  */
      screen_scroll_left (display, user->cursor.col,
			  user->cursor.row,
			  user->screen.width - user->cursor.col, 1,
			  parse->params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'S':
      /* Scroll up: <ind>, <indn>.  */
      screen_scroll_up (display, 0, 0,
			user->screen.width, user->screen.height,
			parse->params[0] ?: 1,
			L' ', display->attr.current);
      break;
    case 'T':
      /* Scroll down: <ri>, <rin>.  */
      screen_scroll_down (display, 0, 0,
			  user->screen.width, user->screen.height,
			  parse->params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'X':
      /* Erase character(s): <ech>.  */
      screen_fill (display, user->cursor.col, user->cursor.row,
		   parse->params[0] ?: 1, 1,
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
	display->user->cursor.status = CONS_CURSOR_INVISIBLE;
      else
	display->user->cursor.status = CONS_CURSOR_NORMAL;
      /* XXX Flag cursor status change.  */
      break;
    }
}


static void
handle_esc_bracket_question (display_t display, char op)
{
  parse_t parse = &display->output.parse;

  int i;
  switch (op)
    {
    case 'h':
      /* Reset mode.  */
      for (i = 0; i < parse->nparams; ++i)
	handle_esc_bracket_question_hl (display, parse->params[i], 0);
      break;
    case 'l':
      /* Set mode.  */
      for (i = 0; i < parse->nparams; ++i)
	handle_esc_bracket_question_hl (display, parse->params[i], 1);
      break;
    }
}

/* Display must be locked.  */
static void
display_output_one (display_t display, wchar_t chr)
{
  struct cons_display *user = display->user;
  parse_t parse = &display->output.parse;

  void newline (void)
    {
      if (user->cursor.row < user->screen.height - 1)
	{
	  user->cursor.row++;
	  /* XXX Flag cursor update.  */
	}
      else
	{
	  user->screen.cur_line++;
	  user->screen.cur_line %= user->screen.lines;

	  /* XXX Set attribute flags.  */
	  screen_fill (display, 0, user->screen.height - 1,
		       user->screen.width, 1, L' ', user->screen.width);
	  if (user->screen.scr_lines <
	      user->screen.lines - user->screen.height)
	    user->screen.scr_lines++;
	  /* XXX Flag current line change.  */
	  /* XXX Flag change of last line.  */
	  /* XXX Possibly flag change of length of scroll back buffer.  */
	}
    }

  switch (parse->state)
    {
    case STATE_NORMAL:
      switch (chr)
	{
	case L'\r':
	  /* Carriage return: <cr>.  */
	  if (user->cursor.col)
	    {
	      user->cursor.col = 0;
	      /* XXX Flag cursor update.  */
	    }
	  break;
	case L'\n':
	  /* Cursor down: <cud1>, scroll up: <ind>.  */
	  newline ();
	  break;
	case L'\b':
	  /* Cursor backward: <cub1>.  */
	  if (user->cursor.col > 0 || user->cursor.row > 0)
	    {
	      if (user->cursor.col > 0)
		user->cursor.col--;
	      else
		{
		  /* XXX This implements the <bw> functionality.
		     The alternative is to cut off and set x to 0.  */
		  user->cursor.col = user->screen.width - 1;
		  user->cursor.row--;
		}
	      /* XXX Flag cursor update.  */
	    }
	  break;
	case L'\t':
	  /* Horizontal tab: <ht> */
	  user->cursor.col = (user->cursor.col | 7) + 1;
	  if (user->cursor.col >= user->screen.width)
	    {
	      user->cursor.col = 0;
	      newline ();
	    }
	  /* XXX Flag cursor update.  */
	  break;
	case L'\033':
	  parse->state = STATE_ESC;
	  break;
	case L'\0':
	  /* Padding character: <pad>.  */
	  break;
	default:
	  {
	    int line = (user->screen.cur_line + user->cursor.row)
	      % user->screen.lines;

	    /* XXX Set attribute flags.  */
	    user->_matrix[line * user->screen.width
			  + user->cursor.col] = chr;

	    user->cursor.col++;
	    if (user->cursor.col == user->screen.width)
	      {
		user->cursor.col = 0;
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
	  parse->state = STATE_ESC_BRACKET_INIT;
	  break;
	case L'c':
	  /* Clear screen and home cursor: <clear>.  */
	  screen_fill (display, 0, 0,
		       user->screen.width, user->screen.height,
		       L' ', display->attr.current);
	  user->cursor.col = user->cursor.row = 0;
	  /* XXX Flag cursor change.  */
	  parse->state = STATE_NORMAL;
	  break;
	default:
	  /* Unsupported escape sequence.  */
	  parse->state = STATE_NORMAL;
	  break;
	}
      break;
      
    case STATE_ESC_BRACKET_INIT:
      memset (&parse->params, 0, sizeof parse->params);
      parse->nparams = 0;
      if (chr == '?')
	{
	  parse->state = STATE_ESC_BRACKET_QUESTION;
	  break;	/* Consume the question mark.  */
	}
      else
	parse->state = STATE_ESC_BRACKET;
      /* Fall through.  */
    case STATE_ESC_BRACKET:
    case STATE_ESC_BRACKET_QUESTION:
      if (chr >= '0' && chr <= '9')
	parse->params[parse->nparams]
	    = parse->params[parse->nparams]*10 + chr - '0';
      else if (chr == ';')
	{
	  if (++(parse->nparams) >= PARSE_MAX_PARAMS)
	    parse->state = STATE_NORMAL; /* too many */
	}
      else
	{
	  parse->nparams++;
	  if (parse->state == STATE_ESC_BRACKET)
	    handle_esc_bracket (display, chr);
	  else
	    handle_esc_bracket_question (display, chr);
	  parse->state = STATE_NORMAL;
	}
      break;
    default:
      abort ();
    }
}

/* Output LENGTH bytes starting from BUFFER in the system encoding.
   Set BUFFER and LENGTH to the new values.  The exact semantics are
   just as in the iconv interface.  */
static error_t
display_output_some (display_t display, char **buffer, size_t *length)
{
#define CONV_OUTBUF_SIZE 256
  error_t err = 0;

  while (!err && *length > 0)
    {
      size_t nconv;
      wchar_t outbuf[CONV_OUTBUF_SIZE];
      char *outptr = (char *) outbuf;
      size_t outsize = CONV_OUTBUF_SIZE * sizeof (wchar_t);
      error_t saved_err;
      int i;

      nconv = iconv (display->output.cd, buffer, length, &outptr, &outsize);
      saved_err = errno;

      /* First process all successfully converted characters.  */
      for (i = 0; i < CONV_OUTBUF_SIZE - outsize / sizeof (wchar_t); i++)
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
  return err;
}

void
display_init (void)
{
  /* Create the pager bucket, and start to serve paging requests.  */
  pager_bucket = ports_create_bucket ();
  if (! pager_bucket)
    error (5, errno, "Cannot create pager bucket");

  /* Make a thread to service paging requests.  */
  cthread_detach (cthread_fork ((cthread_fn_t) service_paging_requests,
                                (any_t)pager_bucket));
}

/* Create a new virtual console display, with the system encoding
   being ENCODING.  */
error_t
display_create (display_t *r_display, const char *encoding)
{
  error_t err = 0;
  display_t display;
  int width = 80;
  int height = 25;
  int lines = 25; 	/* XXX For now.  */

  *r_display = NULL;
  display = calloc (1, sizeof *display);
  if (!display)
    return ENOMEM;

  mutex_init (&display->lock);
  err = user_create (display, width, height, lines);
  if (err)
    {
      free (display);
      return err;
    }

  err = output_init (&display->output, encoding);
  if (err)
    {
      user_destroy (display);
      free (display);
    }
  *r_display = display;
  return err;
}


/* Destroy the display DISPLAY.  */
void
display_destroy (display_t display)
{
  output_deinit (&display->output);
  user_destroy (display);
  free (display);
}


/* Return the dimensions of the display DISPLAY in *WINSIZE.  */
void
display_getsize (display_t display, struct winsize *winsize)
{
  mutex_lock (&display->lock);
  winsize->ws_row = display->user->screen.height;
  winsize->ws_col = display->user->screen.width;
  winsize->ws_xpixel = 0;
  winsize->ws_ypixel = 0;
  mutex_unlock (&display->lock);
}


/* Set the owner of the display DISPLAY to PID.  The owner receives
   the SIGWINCH signal when the terminal size changes.  */
error_t
display_set_owner (display_t display, pid_t pid)
{
  mutex_lock (&display->lock);
  display->has_owner = 1;
  display->owner_id = pid;
  mutex_unlock (&display->lock);
  return 0;
}


/* Return the owner of the display DISPLAY in PID.  If there is no
   owner, return ENOTTY.  */
error_t
display_get_owner (display_t display, pid_t *pid)
{
  error_t err = 0;
  mutex_lock (&display->lock);
  if (!display->has_owner)
    err = ENOTTY;
  else
    *pid = display->owner_id;
  mutex_unlock (&display->lock);
  return err;
}

/* Output DATALEN characters from the buffer DATA on display DISPLAY.
   The DATA must be supplied in the system encoding configured for
   DISPLAY.  The function returns the amount of bytes written (might
   be smaller than DATALEN) or -1 and the error number in errno.  If
   NONBLOCK is not zero, return with -1 and set errno to EWOULDBLOCK
   if operation would block for a long time.  */
ssize_t
display_output (display_t display, int nonblock, char *data, size_t datalen)
{
  output_t output = &display->output;
  error_t err;
  char *buffer;
  size_t buffer_size;
  ssize_t amount;

  error_t ensure_output_buffer_size (size_t new_size)
    {
      /* Must be a power of two.  */
#define OUTPUT_ALLOCSIZE 32

      if (output->allocated < new_size)
	{
	  char *new_buffer;
	  new_size = (new_size + OUTPUT_ALLOCSIZE - 1)
	    & ~(OUTPUT_ALLOCSIZE - 1);
	  new_buffer = realloc (output->buffer, new_size);
	  if (!new_buffer)
	    return ENOMEM;
	  output->buffer = new_buffer;
	  output->allocated = new_size;
	}
      return 0;
    }

  mutex_lock (&display->lock);
  while (output->stopped)
    {
      if (nonblock)
        {
          mutex_unlock (&display->lock);
          errno = EWOULDBLOCK;
          return -1;
        }
      if (hurd_condition_wait (&output->resumed, &display->lock))
        {
          mutex_unlock (&display->lock);
          errno = EINTR;
          return -1;
        }
    }

  if (output->size)
    {
      err = ensure_output_buffer_size (output->size + datalen);
      if (err)
        {
          mutex_unlock (&display->lock);
          errno = ENOMEM;
          return -1;
        }
      buffer = output->buffer;
      buffer_size = output->size;
      memcpy (buffer + buffer_size, data, datalen);
      buffer_size += datalen;
    }
  else
    {
      buffer = data;
      buffer_size = datalen;
    }
  amount = buffer_size;
  err = display_output_some (display, &buffer, &buffer_size);
  amount -= buffer_size;

  if (err && !amount)
    {
      mutex_unlock (&display->lock);
      errno = err;
      return err;
    }
  /* XXX What should be done with invalid characters etc?  */
  if (buffer_size)
    {
      /* If we used the caller's buffer DATA, the remaining bytes
         might not fit in our internal output buffer.  In this case we
         can reallocate the buffer in VCONS without needing to update
         OUTPUT (as it points into DATA). */
      err = ensure_output_buffer_size (buffer_size);
      if (err)
        {
          mutex_unlock (&display->lock);
          return err;
        }
      memmove (output->buffer, buffer, buffer_size);
    }
  output->size = buffer_size;
  amount += buffer_size;

  mutex_unlock (&display->lock);
  return amount;
}

ssize_t
display_read (display_t display, int nonblock, off_t off,
	      char *data, size_t len)
{
  mutex_lock (&display->lock);
  memcpy (data, ((char *) display->user) + off, len);
  mutex_unlock (&display->lock);
  return len;
}

/* Resume the output on the display DISPLAY.  */
void
display_start_output (display_t display)
{
  mutex_lock (&display->lock);
  if (display->output.stopped)
    {
      display->output.stopped = 0;
      condition_broadcast (&display->output.resumed);
    }
  mutex_unlock (&display->lock);
}


/* Stop all output on the display DISPLAY.  */
void
display_stop_output (display_t display)
{
  mutex_lock (&display->lock);
  display->output.stopped = 1;
  mutex_unlock (&display->lock);
}


/* Return the number of pending output bytes for DISPLAY.  */
size_t
display_pending_output (display_t display)
{
  int output_size;
  mutex_lock (&display->lock);
  output_size = display->output.size;
  mutex_unlock (&display->lock);
  return output_size;
}


/* Flush the output buffer, discarding all pending data.  */
void
display_discard_output (display_t display)
{
  mutex_lock (&display->lock);
  display->output.size = 0;
  mutex_unlock (&display->lock);
}
