/* ncursesw.c - The ncursesw console driver.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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

#include <assert-backtrace.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <wchar.h>

/* The makefiles make sure that this program is compiled with
   -I${prefix}/ncursesw.  */
#include <curses.h>

#include <pthread.h>
#include <hurd/console.h>

#include "driver.h"


/* ncurses is not thread-safe.  This lock protects all calls into the
   ncurses library.  */
static pthread_mutex_t ncurses_lock;

/* The current width and height the ncursesw driver is using.  */
static unsigned int current_width;
static unsigned int current_height;

/* The window on which the console is shown.  */
static WINDOW *conspad;

/* The upper left corner shown in the pad.  */
static unsigned int padx;
static unsigned int pady;

/* Autoscroll is on or off.  Autoscroll makes scrolling dependent on
   the cursor position.  */
static int autoscroll;

/* Forward declaration.  */
static struct display_ops ncursesw_display_ops;
static struct input_ops ncursesw_input_ops;
static struct bell_ops ncursesw_bell_ops;

struct curses_kc_to_cons_kc
{
  int curses;
  char *cons;
};

static struct curses_kc_to_cons_kc keycodes[] =
  {
    { KEY_BREAK, NULL },		/* XXX */
    { KEY_DOWN, CONS_KEY_DOWN },
    { KEY_UP, CONS_KEY_UP },
    { KEY_RIGHT, CONS_KEY_RIGHT },
    { KEY_LEFT, CONS_KEY_LEFT },
    { KEY_HOME, CONS_KEY_HOME },
    { KEY_BACKSPACE, CONS_KEY_BACKSPACE },
    { KEY_F(1), CONS_KEY_F1 },
    { KEY_F(2), CONS_KEY_F2 },
    { KEY_F(3), CONS_KEY_F3 },
    { KEY_F(4), CONS_KEY_F4 },
    { KEY_F(5), CONS_KEY_F5 },
    { KEY_F(6), CONS_KEY_F6 },
    { KEY_F(7), CONS_KEY_F7 },
    { KEY_F(8), CONS_KEY_F8 },
    { KEY_F(9), CONS_KEY_F9 },
    { KEY_F(10), CONS_KEY_F10 },
    { KEY_DL, NULL },		/* XXX Delete line.  */
    { KEY_IL, NULL },		/* XXX Insert line.  */
    { KEY_DC, CONS_KEY_DC },
    { KEY_IC, CONS_KEY_IC },
    { KEY_EIC, NULL },		/* XXX Exit insert mode.  */
    { KEY_CLEAR, NULL },	/* XXX Clear screen.  */
    { KEY_EOS, NULL },		/* XXX Clear to end of screen.  */
    { KEY_EOL, NULL },		/* XXX Clear to end of line.  */
    { KEY_SF, NULL },		/* XXX Scroll one line forward.  */
    { KEY_SR, NULL },		/* XXX Scroll one line backward.  */
    { KEY_NPAGE, CONS_KEY_NPAGE },
    { KEY_PPAGE, CONS_KEY_PPAGE },
    { KEY_STAB, NULL },		/* XXX Set tab.  */
    { KEY_CTAB, NULL },		/* XXX Clear tab.  */
    { KEY_CATAB, NULL },	/* XXX Clear all tabs.  */
    { KEY_ENTER, NULL },	/* XXX Enter or send.  */
    { KEY_SRESET, NULL },	/* XXX Soft (partial) reset.  */
    { KEY_RESET, NULL },	/* XXX Reset or hard reset.  */
    { KEY_PRINT, NULL },	/* XXX Print or copy.  */
    { KEY_LL, NULL },		/* XXX Home down or bottom (lower left).  */
    { KEY_A1, NULL },		/* XXX Upper left of keypad.  */
    { KEY_A3, NULL },		/* XXX Upper right of keypad.  */
    { KEY_B2, NULL },		/* XXX Center of keypad.  */
    { KEY_C1, NULL },		/* XXX Lower left of keypad.  */
    { KEY_C3, NULL },		/* XXX Lower right of keypad.  */
    { KEY_BTAB, CONS_KEY_BTAB },
    { KEY_BEG, NULL },		/* XXX Beg(inning) key.  */
    { KEY_CANCEL, NULL },	/* XXX Cancel key.  */
    { KEY_CLOSE, NULL },	/* XXX Close key.  */
    { KEY_COMMAND, NULL },	/* XXX Cmd (command) key.  */
    { KEY_COPY, NULL },		/* XXX Copy key.  */
    { KEY_CREATE, NULL },	/* XXX Create key.  */
    { KEY_END, CONS_KEY_END },
    { KEY_EXIT, NULL },		/* XXX Exit key.  */
    { KEY_FIND, NULL },		/* XXX Find key.  */
    { KEY_HELP, NULL },		/* XXX Help key.  */
    { KEY_MARK, NULL },		/* XXX Mark key.  */
    { KEY_MESSAGE, NULL },	/* XXX Message key.  */
    { KEY_MOUSE, NULL },	/* XXX Mouse event read.  */
    { KEY_MOVE, NULL },		/* XXX Move key.  */
    { KEY_NEXT, NULL },		/* XXX Next object key.  */
    { KEY_OPEN, NULL },		/* XXX Open key.  */
    { KEY_OPTIONS, NULL },	/* XXX Options key.  */
    { KEY_PREVIOUS, NULL },	/* XXX Previous object key.  */
    { KEY_REDO, NULL },		/* XXX Redo key.  */
    { KEY_REFERENCE, NULL },	/* XXX Ref(erence) key.  */
    { KEY_REFRESH, NULL },	/* XXX Refresh key.  */
    { KEY_REPLACE, NULL },	/* XXX Replace key.  */
    { KEY_RESIZE, NULL },	/* XXX Screen resized.  */
    { KEY_RESTART, NULL },	/* XXX Restart key.  */
    { KEY_RESUME, NULL },	/* XXX Resume key.  */
    { KEY_SAVE, NULL },		/* XXX Save key.  */
    { KEY_SBEG, NULL },		/* XXX Shifted beginning key.  */
    { KEY_SCANCEL, NULL },	/* XXX Shifted cancel key.  */
    { KEY_SCOMMAND, NULL },	/* XXX Shifted command key.  */
    { KEY_SCOPY, NULL },	/* XXX Shifted copy key.  */
    { KEY_SCREATE, NULL },	/* XXX Shifted create key.  */
    { KEY_SDC, NULL },		/* XXX Shifted delete char key.  */
    { KEY_SDL, NULL },		/* XXX Shifted delete line key.  */
    { KEY_SELECT, NULL },	/* XXX Select key.  */
    { KEY_SEND, NULL },		/* XXX Shifted end key.  */
    { KEY_SEOL, NULL },		/* XXX Shifted clear line key.  */
    { KEY_SEXIT, NULL },	/* XXX Shifted exit key.  */
    { KEY_SFIND, NULL },	/* XXX Shifted find key.  */
    { KEY_SHELP, NULL },	/* XXX Shifted help key.  */
    { KEY_SHOME, NULL },	/* XXX Shifted home key.  */
    { KEY_SIC, NULL },		/* XXX Shifted input key.  */
    { KEY_SLEFT, NULL },	/* XXX Shifted left arrow key.  */
    { KEY_SMESSAGE, NULL },	/* XXX Shifted message key.  */
    { KEY_SMOVE, NULL },	/* XXX Shifted move key.  */
    { KEY_SNEXT, NULL },	/* XXX Shifted next key.  */
    { KEY_SOPTIONS, NULL },	/* XXX Shifted options key.  */
    { KEY_SPREVIOUS, NULL },	/* XXX Shifted prev key.  */
    { KEY_SPRINT, NULL },	/* XXX Shifted print key.  */
    { KEY_SREDO, NULL },	/* XXX Shifted redo key.  */
    { KEY_SREPLACE, NULL },	/* XXX Shifted replace key.  */
    { KEY_SRIGHT, NULL },	/* XXX Shifted right arrow.  */
    { KEY_SRSUME, NULL },	/* XXX Shifted resume key.  */
    { KEY_SSAVE, NULL },	/* XXX Shifted save key.  */
    { KEY_SSUSPEND, NULL },	/* XXX Shifted suspend key.  */
    { KEY_SUNDO, NULL },	/* XXX Shifted undo key.  */
    { KEY_SUSPEND, NULL },	/* XXX Suspend key.  */
    { KEY_UNDO, NULL }		/* XXX Undo key.  */
  };

static int
ucs4_to_altchar (wchar_t chr, chtype *achr)
{
  switch (chr)
    {
    case CONS_CHAR_RARROW:
      *achr = ACS_RARROW;
      break;
    case CONS_CHAR_LARROW:
      *achr = ACS_LARROW;
      break;
    case CONS_CHAR_UARROW:
      *achr = ACS_UARROW;
      break;
    case CONS_CHAR_DARROW:
      *achr = ACS_DARROW;
      break;
    case CONS_CHAR_BLOCK:
      *achr = ACS_BLOCK;
      break;
    case CONS_CHAR_LANTERN:
      *achr = ACS_LANTERN;
      break;
    case CONS_CHAR_DIAMOND:
      *achr = ACS_DIAMOND;
      break;
    case CONS_CHAR_CKBOARD:
      *achr = ACS_CKBOARD;
      break;
    case CONS_CHAR_DEGREE:
      *achr = ACS_DEGREE;
      break;
    case CONS_CHAR_PLMINUS:
      *achr = ACS_PLMINUS;
      break;
    case CONS_CHAR_BOARD:
      *achr = ACS_BOARD;
      break;
    case CONS_CHAR_LRCORNER:
      *achr = ACS_LRCORNER;
      break;
    case CONS_CHAR_URCORNER:
      *achr = ACS_URCORNER;
      break;
    case CONS_CHAR_ULCORNER:
      *achr = ACS_ULCORNER;
      break;
    case CONS_CHAR_LLCORNER:
      *achr = ACS_LLCORNER;
      break;
    case CONS_CHAR_PLUS:
      *achr = ACS_PLUS;
      break;
    case CONS_CHAR_S1:
      *achr = ACS_S1;
      break;
    case CONS_CHAR_S3:
      *achr = ACS_S3;
      break;
    case CONS_CHAR_HLINE:
      *achr = ACS_HLINE;
      break;
    case CONS_CHAR_S7:
      *achr = ACS_S7;
      break;
    case CONS_CHAR_S9:
      *achr = ACS_S9;
      break;
    case CONS_CHAR_LTEE:
      *achr = ACS_LTEE;
      break;
    case CONS_CHAR_RTEE:
      *achr = ACS_RTEE;
      break;
    case CONS_CHAR_BTEE:
      *achr = ACS_BTEE;
      break;
    case CONS_CHAR_TTEE:
      *achr = ACS_TTEE;
      break;
    case CONS_CHAR_VLINE:
      *achr = ACS_VLINE;
      break;
    case CONS_CHAR_LEQUAL:
      *achr = ACS_LEQUAL;
      break;
    case CONS_CHAR_GEQUAL:
      *achr = ACS_GEQUAL;
      break;
    case CONS_CHAR_PI:
      *achr = ACS_PI;
      break;
    case CONS_CHAR_NEQUAL:
      *achr = ACS_NEQUAL;
      break;
    case CONS_CHAR_STERLING:
      *achr = ACS_STERLING;
      break;
    case CONS_CHAR_BULLET:
      *achr = ACS_BULLET;
      break;
    default:
      return 0;
   }
  return 1;
}

static error_t
refresh_screen (void)
{
  /* It is possible */
  if (!current_width && !current_height)
    return 0;
  return prefresh (conspad, pady, padx, 0, 0, 
		   (current_height <= (unsigned int) LINES 
		    ? current_height : (unsigned int) LINES) - 1,
		   (current_width <= (unsigned int) COLS 
		    ? current_width : (unsigned int) COLS) - 1); 
}

static void *
input_loop (void *unused)
{
  int fd = 0;
  fd_set rfds;
  int w_escaped = 0;

  FD_ZERO (&rfds);
  FD_SET (fd, &rfds);

  while (1)
    {
      int ret;

      FD_SET (fd, &rfds);

      ret = select (fd + 1, &rfds, 0, 0, 0);
      if (ret == 1)
	{
	  char buffer[100];
	  char *buf = buffer;
	  size_t size = 0;

	  pthread_mutex_lock (&ncurses_lock);
	  while ((ret = wgetch (conspad)) != ERR)
	    {
	      unsigned int i;
	      int found;

	      if (w_escaped)
		{
		  switch (ret)
		    {
		    case 'x':
		      pthread_mutex_unlock (&ncurses_lock);
		      console_exit ();
		      break;
		    case 23:	/* ^W */
		      assert_backtrace (size < 100);
		      buf[size++] = ret;
		      break;
		    case '1':
		    case '2':
		    case '3':
		    case '4':
		    case '5':
		    case '6':
		    case '7':
		    case '8':
		    case '9':
		      /* Avoid a dead lock.  */
		      pthread_mutex_unlock (&ncurses_lock);
		      console_switch (1 + (ret - '1'), 0);
		      pthread_mutex_lock (&ncurses_lock);
		      break;
		    case 'j':
		      /* Scroll pad to left.  */
		      if (padx > 0)
			{
			  padx--;
			  refresh_screen ();
			}
		      break;
		    case 'k':
		      /* Scroll pad down.  */
		      if (pady < current_height - LINES)
			{
			  pady++;
			  refresh_screen ();
			}
		      break;
		    case 'l':
		      /* Scroll pad to right.  */
		      if (padx < current_width - COLS)
			{
			  padx++;
			  refresh_screen ();
			}  
		      break;
		    case 'i':
		      /* Scroll pad up.  */
		      if (pady > 0)
			{
			  pady--;
			  refresh_screen ();
			}
		      break;
		    case 'a':
		      /* Switch autoscroll on/off.  */
		      autoscroll = !autoscroll;
		      break;
		    default:
		      break;
		    }
		  w_escaped = 0;
		}
	      else
		switch (ret)
		  {
		  case 23:	/* ^W */
		    w_escaped = 1;
		    break;
		  default:
		    found = 0;
		    for (i = 0; i < sizeof (keycodes) / sizeof (keycodes[0]);
			 i++)
		      {
			if (keycodes[i].curses == ret)
			  {	
			    if (keycodes[i].cons)
			      {
				assert_backtrace (size 
					< 101 - strlen (keycodes[i].cons));
				strcpy (&buf[size], keycodes[i].cons);
				size += strlen (keycodes[i].cons);
			      }
			    found = 1;
			    break;
			  }
		      }
		    if (!found)
		      {
			assert_backtrace (size < 100);
			buf[size++] = ret;
		      }
		    break;
		  }
	    }
	  pthread_mutex_unlock (&ncurses_lock);
	  if (size)
	    console_input (buf, size);
	}
    }
}

static inline attr_t
conchar_attr_to_attr (conchar_attr_t attr)
{
  return ((attr.intensity == CONS_ATTR_INTENSITY_BOLD
	   ? A_BOLD : (attr.intensity == CONS_ATTR_INTENSITY_DIM
		       ? A_DIM : A_NORMAL))
	  | (attr.underlined ? A_UNDERLINE : 0)
	  | (attr.reversed ? A_REVERSE : 0)
	  | (attr.blinking ? A_BLINK: 0)
	  | (attr.concealed ? A_INVIS : 0));
}

static inline short
conchar_attr_to_color_pair (conchar_attr_t attr)
{
  return COLOR_PAIR (attr.bgcol << 3 | attr.fgcol);
}

static void
mvwputsn (conchar_t *str, size_t len, off_t x, off_t y)
{
  cchar_t chr;
  wchar_t wch[2] = { L'\0', L'\0' };
  uint32_t last_attr = * (uint32_t *) &str->attr;
  attr_t attr = conchar_attr_to_attr (str->attr);
  short color_pair = conchar_attr_to_color_pair (str->attr);

  wmove (conspad, y, x);
  while (len)
    {
      int ret;
      chtype ac;

      if (last_attr != *(uint32_t *) &str->attr)
	{
	  last_attr = * (uint32_t *) &str->attr;
	  attr = conchar_attr_to_attr (str->attr);
	  color_pair = conchar_attr_to_color_pair (str->attr);
	}

      if (ucs4_to_altchar (str->chr, &ac))
	waddch (conspad, ac | attr | color_pair);
      else
	{      
	  wch[0] = str->chr;
	  ret = setcchar (&chr, wch, attr, color_pair, NULL);
#if 0
	  if (ret == ERR)
	    {
	      printf ("setcchar failed: %s\n", strerror (errno));
	      printf ("[%lc]\n", wch[0]);
	      assert_backtrace (!"Do something if setcchar fails.");
	    }
#endif
	  ret = wadd_wch (conspad, &chr);
#if 0
	  if (ret == ERR)
	    {
	      printf ("add_wch failed: %i, %s\n", ret, strerror (errno));
	      printf ("[%lc]\n", wch[0]);
	      assert_backtrace (!"Do something if add_wchr fails.");
	    }
#endif
	}
      len--;
      str++;
    }
} 


static error_t
ncursesw_update (void *handle)
{
  pthread_mutex_lock (&ncurses_lock);
  refresh_screen ();
  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}


static error_t
ncursesw_set_cursor_pos (void *handle, uint32_t col, uint32_t row)
{
  pthread_mutex_lock (&ncurses_lock);
  assert_backtrace (current_width && current_height);
  if (autoscroll)
    {
      /* Autoscroll to the right.  */
      if (col > COLS + padx)
	{
	  padx += COLS / 2;
	  if (padx > COLS + current_width)
	    padx = current_width - COLS;
	  refresh_screen ();
	}
      /* Autoscroll to the left.  */
      else if (col < padx)
	{
	  padx -= COLS / 2;
	  if (padx < 0)
	    padx = 0;
	  refresh_screen ();
	}
      /* Autoscroll down.  */
      if (row > LINES + pady)
	{
	  pady += LINES / 2;
	  if (pady > LINES + current_height)
	    pady = current_height - LINES;
	  refresh_screen ();
	}
      /* Autoscroll up.  */
      else if (row < pady)
	{
	  pady -= LINES / 2;
	  if (pady < 0)
	    pady = 0;
	  refresh_screen ();
	}
    }

  wmove (conspad, row, col);

  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}


static error_t
ncursesw_set_cursor_status (void *handle, uint32_t status)
{
  pthread_mutex_lock (&ncurses_lock);

  /* If the cursor is invisible and switching to one visible state is
     impossible, switch to the other visible state or else the cursor
     will not be shown at all.  */
  if (curs_set (status) == -1 && status)
    curs_set (status == 1 ? 2 : 1);

  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}


static error_t
ncursesw_scroll (void *handle, int delta)
{
  /* XXX We don't support scrollback for now.  */
  assert_backtrace (delta >= 0);

  pthread_mutex_lock (&ncurses_lock);
  idlok (conspad, TRUE);
  scrollok (conspad, TRUE);
  wscrl (conspad, delta);
  idlok (conspad, FALSE);
  scrollok (conspad, FALSE);
  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}


static error_t
ncursesw_write (void *handle, conchar_t *str, size_t length,
		uint32_t col, uint32_t row)
{
  int x;
  int y;

  pthread_mutex_lock (&ncurses_lock);
  getyx (conspad, y, x);
  mvwputsn (str, length, col, row);
  wmove (conspad, y, x);
  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}


static error_t
ncursesw_flash (void *handle)
{
  pthread_mutex_lock (&ncurses_lock);
  flash ();
  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}


/* Bell driver operations.  */
error_t
ncursesw_beep (void *handle)
{
  pthread_mutex_lock (&ncurses_lock);
  beep ();
  pthread_mutex_unlock (&ncurses_lock);
  return 0;
}



static error_t
ncursesw_driver_init (void **handle, int no_exit,
		      int argc, char *argv[], int *next)
{
  pthread_mutex_init (&ncurses_lock, NULL);
  return 0;
}

static error_t
ncursesw_driver_start (void *handle)
{
  pthread_t thread;
  error_t err;
  int i;

  initscr ();
  start_color ();
  for (i = 0; i < 64; i++)
    init_pair (i, i & 7, i >> 3);
  raw ();
  noecho ();
  nonl ();

  /* Create a new pad with the size minimal size.  This pad will be
     resized by ncursesw_set_dimension.  */
  conspad = newpad (1, 1);
  if (!conspad)
    return errno;

  intrflush (conspad, FALSE);
  nodelay (conspad, TRUE);
  wtimeout (conspad, 1);
  keypad (conspad, TRUE);

  err = driver_add_display (&ncursesw_display_ops, NULL);
  if (err)
    {
      endwin ();
      return err;
    }
  err = driver_add_input (&ncursesw_input_ops, NULL);
  if (err)
    {
      err = driver_remove_display (&ncursesw_display_ops, NULL);
      endwin ();
      return err;
    }
  err = driver_add_bell (&ncursesw_bell_ops, NULL);
  if (err)
    {
      err = driver_remove_input (&ncursesw_input_ops, NULL);
      err = driver_remove_display (&ncursesw_display_ops, NULL);
      endwin ();
      return err;
    }

  err = pthread_create (&thread, NULL, input_loop, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  return 0;
}

/* Destroy the display HANDLE.  */
static error_t
ncursesw_driver_fini (void *handle, int force)
{
  /* XXX Cancel the input thread.  */
  pthread_mutex_lock (&ncurses_lock);
  driver_remove_display (&ncursesw_display_ops, NULL);
  driver_remove_input (&ncursesw_input_ops, NULL);
  driver_remove_bell (&ncursesw_bell_ops, NULL);
  pthread_mutex_unlock (&ncurses_lock);

  endwin ();
  return 0;
}

static error_t
ncursesw_set_dimension (void *handle, unsigned int width, unsigned int height)
{
  pthread_mutex_lock (&ncurses_lock);
  if (width != current_width || height != current_height)
    {
      wresize (conspad, height, width);
      padx = 0;
      pady = 0;
    }
  current_width = width;
  current_height = height;
  pthread_mutex_unlock(&ncurses_lock);
  return 0;
}


struct driver_ops driver_ncursesw_ops =
  {
    ncursesw_driver_init,
    ncursesw_driver_start,
    ncursesw_driver_fini,
  };

static struct display_ops ncursesw_display_ops =
  {
    ncursesw_set_cursor_pos,
    ncursesw_set_cursor_status,
    ncursesw_scroll,
    NULL,
    ncursesw_write,
    ncursesw_update,
    ncursesw_flash,
    NULL,
    ncursesw_set_dimension
  };

static struct input_ops ncursesw_input_ops =
  {
    NULL,
    NULL
  };

static struct bell_ops ncursesw_bell_ops =
  {
    ncursesw_beep,
    NULL
  };
