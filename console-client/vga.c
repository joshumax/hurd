/* vga.c - The VGA device display driver.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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

#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <unistd.h>
#include <iconv.h>
#include <argp.h>
#include <string.h>
#include <stdint.h>

#include <sys/io.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <cthreads.h>
#include <hurd/console.h>

#include "driver.h"
#include "timer.h"

#include "vga-hw.h"
#include "vga-support.h"
#include "bdf.h"
#include "vga-dynafont.h"
#include "vga-dynacolor.h"
#include "unicode.h"


#define VGA_DISP_WIDTH 80
#define VGA_DISP_HEIGHT 25

/* The font file.  */
#define DEFAULT_VGA_FONT "/lib/hurd/fonts/vga-system.bdf"
static char *vga_display_font;

#define DEFAULT_VGA_FONT_ITALIC "/lib/hurd/fonts/vga-system-italic.bdf"
static char *vga_display_font_italic;

#define DEFAULT_VGA_FONT_BOLD "/lib/hurd/fonts/vga-system-bold.bdf"
static char *vga_display_font_bold;

#define DEFAULT_VGA_FONT_BOLD_ITALIC \
	"/lib/hurd/fonts/vga-system-bold-italic.bdf"
static char *vga_display_font_bold_italic;

/* If false use all colors, else use double font slots.  */
static int vga_display_max_glyphs;

/* The timer used for flashing the screen.  */
static struct timer_list vga_display_timer;

/* The lock that protects the color palette manipulation.  */
static struct mutex vga_display_lock;

/* Forward declaration.  */
static struct display_ops vga_display_ops;


struct refchr
{
  unsigned int used : 1;
  unsigned int chr : 9;
  unsigned int attr : 8;
};


struct vga_display
{
  /* The VGA font for this display.  */
  dynafont_t df;
  int df_size;

  /* The color palette.  */
  dynacolor_t dc;

  int width;
  int height;

  /* Current attribute.  */
  int cur_conchar_attr_init;
  conchar_attr_t cur_conchar_attr;
  char cur_attr;

  /* Remember for each cell on the display the glyph written to it and
     the colors (in the upper byte) assigned.  0 means unassigned.  */

  struct refchr refmatrix[VGA_DISP_HEIGHT][VGA_DISP_WIDTH];
};


static void
vga_display_invert_border (void)
{
  unsigned char col[3];

  mutex_lock (&vga_display_lock);
  vga_read_palette (0, col, 1);
  col[0] = 0xff - col[0];
  col[1] = 0xff - col[1];
  col[2] = 0xff - col[2];
  vga_write_palette (0, col, 1);
  mutex_unlock (&vga_display_lock);
}


static int
vga_display_flash_off (void *dummy)
{
  vga_display_invert_border ();
  return 0;
}


static error_t
vga_display_flash (void *handle)
{
  if (timer_remove (&vga_display_timer))
    vga_display_invert_border ();
  vga_display_invert_border ();
  vga_display_timer.expires = fetch_jiffies () + 10;
  timer_add (&vga_display_timer);
  return 0;
}


/* Parse arguments at startup.  */
static error_t
parse_startup_args (int no_exit, int argc, char *argv[], int *next)
{
#define PARSE_FONT_OPT(x,y)				\
    do {						\
      if (!strcmp (argv[*next], x))			\
	{						\
	  (*next)++;					\
	  if (*next == argc)				\
	    {						\
	      if (no_exit)				\
		return EINVAL;				\
	      else					\
		error (1, 0, "option " x		\
		       " requires an argument");	\
	    }						\
	  if (vga_display_##y)				\
	    free (vga_display_##y);			\
	  vga_display_##y = strdup (argv[*next]);	\
	  if (!vga_display_##y)				\
	    {						\
	      if (no_exit)				\
		return errno;				\
	      else					\
		error (1, errno, "malloc failed");	\
	    }						\
	  (*next)++;					\
          continue;					\
	}						\
      } while (0)

#define PARSE_FONT_OPT_NOARGS(x,y,z)           \
  {                                            \
    if (!strcmp (argv[*next], x))              \
      {                                        \
       (*next)++;                              \
       vga_display_##y = z;                    \
      }                                        \
  }

  while (*next < argc)
    {
      PARSE_FONT_OPT ("--font", font);
      PARSE_FONT_OPT ("--font-italic", font_italic);
      PARSE_FONT_OPT ("--font-bold", font_bold);
      PARSE_FONT_OPT ("--font-bold-italic", font_bold_italic);
      PARSE_FONT_OPT_NOARGS ("--max-colors", max_glyphs, 1);
      PARSE_FONT_OPT_NOARGS ("--max-glyphs", max_glyphs, 0);


      break;
    }
  return 0;
}

/* Initialize the subsystem.  */
static error_t
vga_display_init (void **handle, int no_exit, int argc, char *argv[], int *next)
{
  error_t err;
  struct vga_display *disp;

  /* XXX Assert that we are called only once.  */
  mutex_init (&vga_display_lock);
  timer_clear (&vga_display_timer);
  vga_display_timer.fnc = &vga_display_flash_off;

  /* Parse the arguments.  */
  err = parse_startup_args (no_exit, argc, argv, next);
  if (err)
    return err;

  /* Create and initialize the display srtucture as much as
     possible.  */
  disp = calloc (1, sizeof *disp);
  if (!disp)
    return ENOMEM;

  disp->df_size = vga_display_max_glyphs ? 512 : 256;
  disp->width = VGA_DISP_WIDTH;
  disp->height = VGA_DISP_HEIGHT;

  *handle = disp;
  return 0;
}


/* Start the driver.  */
static error_t
vga_display_start (void *handle)
{
  error_t err;
  struct vga_display *disp = handle;
  bdf_font_t font = NULL;
  bdf_font_t font_italic = NULL;
  bdf_font_t font_bold = NULL;
  bdf_font_t font_bold_italic = NULL;
  FILE *font_file;

  err = vga_init ();
  if (err)
    return err;

  dynacolor_init ();

#define LOAD_FONT(x,y)							\
  do {									\
  font_file = fopen (vga_display_##x ?: DEFAULT_VGA_##y, "r");		\
  if (font_file)							\
    {									\
      bdf_error_t bdferr = bdf_read (font_file, &x, NULL);		\
      if (bdferr)							\
	x = NULL;							\
      else								\
	bdf_sort_glyphs (x);						\
      fclose (font_file);						\
    }									\
  } while (0)

  LOAD_FONT (font, FONT);
  LOAD_FONT (font_italic, FONT_ITALIC);
  LOAD_FONT (font_bold, FONT_BOLD);
  LOAD_FONT (font_bold_italic, FONT_BOLD_ITALIC);

  err = dynafont_new (font, font_italic, font_bold, font_bold_italic,
		      disp->df_size, &disp->df);
  if (err)
    {
      free (disp);
      vga_fini ();
      return err;
    }
  dynafont_activate (disp->df);

  disp->dc = (disp->df_size == 512) ? dynacolor_init_8 : dynacolor_init_16;
  dynacolor_activate (&disp->dc);

  err = driver_add_display (&vga_display_ops, disp);
  if (err)
    {
      dynafont_free (disp->df);
      dynacolor_fini ();
      vga_fini ();
      free (disp);
    }
  return err;
}


/* Destroy the display HANDLE.  */
static error_t
vga_display_fini (void *handle, int force)
{
  struct vga_display *disp = handle;
  driver_remove_display (&vga_display_ops, disp);
  if (timer_remove (&vga_display_timer))
    vga_display_flash_off (0);

  dynafont_free (disp->df);
  free (disp);
  dynacolor_fini ();
  vga_fini ();
  if (vga_display_font)
    free (vga_display_font);
  if (vga_display_font_italic)
    free (vga_display_font_italic);
  if (vga_display_font_bold)
    free (vga_display_font_bold);
  if (vga_display_font_bold_italic)
    free (vga_display_font_bold_italic);

  return 0;
}


/* Set the cursor's position on display HANDLE to column COL and row
   ROW.  */
static error_t
vga_display_set_cursor_pos (void *handle, uint32_t col, uint32_t row)
{
  struct vga_display *disp = handle;
  unsigned int pos = row * disp->width + col;

  vga_set_cursor_pos (pos);

  return 0;
}


/* Set the cursor's state to STATE on display HANDLE.  */
static error_t
vga_display_set_cursor_status (void *handle, uint32_t state)
{
  struct vga_display *disp = handle;

  if (state != CONS_CURSOR_INVISIBLE)
    dynafont_set_cursor (disp->df, state == CONS_CURSOR_VERY_VISIBLE ? 1 : 0);
  vga_display_cursor (state == CONS_CURSOR_INVISIBLE ? 0 : 1);

  return 0;
}


/* Scroll the display by the desired amount.  The area that becomes
   free will be filled in a subsequent write call.  */
static error_t
vga_display_scroll (void *handle, int delta)
{
  struct vga_display *disp = handle;
  int count = abs(delta) * disp->width;
  int i;
  struct refchr *refpos;
      
  if (delta > 0)
    {
      memmove (vga_videomem, vga_videomem + 2 * count,
	       2 * disp->width * (disp->height - delta));
      refpos = &disp->refmatrix[0][0];
    }
  else
    {
      memmove (vga_videomem + 2 * count, vga_videomem,
	       2 * disp->width * (disp->height + delta));
      refpos = &disp->refmatrix[disp->height + delta][0];
    }
  
  for (i = 0; i < count; i++)
    {
      if (refpos->used)
	{
	  dynafont_release (disp->df, refpos->chr);
	  /* We intimately know that reference counting is only done
	     for the up to 8 colors mode.  */
	  dynacolor_release (disp->dc, refpos->attr & 7);
	  dynacolor_release (disp->dc, (refpos->attr >> 4) & 7);
	}
      refpos++;
    }

  if (delta > 0)
    {
      memmove (&disp->refmatrix[0][0], &disp->refmatrix[0][0] + count,
	       sizeof (struct refchr) * disp->width * (disp->height - delta));
      refpos = &disp->refmatrix[disp->height - delta][0];
    }
  else
    {
      memmove (&disp->refmatrix[0][0] + count, &disp->refmatrix[0][0],
	       sizeof (struct refchr) * disp->width * (disp->height + delta));
      refpos = &disp->refmatrix[0][0];
    }

  for (i = 0; i < count; i++)
    (refpos++)->used = 0;

  return 0;
}


/* Change the font on the console CONSOLE to font.  The old font will
   not be accessed by the vga console subsystem anymore after this
   call completed.  */
static void
vga_display_change_font (void *handle, bdf_font_t font)
{
  struct vga_display *disp = handle;

  dynafont_change_font (disp->df, font);
}


static inline char
vga_display_recalculate_attr (dynacolor_t *dc, conchar_attr_t attr)
{
  char vga_attr;
  signed char res_fgcol;
  signed char res_bgcol;
  signed char fgcol;
  signed char bgcol;

  /* VGA has way too few bits for this stuff.  The highest background
     color bit is also the blinking bit if blinking is enabled.  The
     highest foreground color bit is the font selector bit,
     unfortunately.  Underlining is enabled if foreground is ?001 and
     background ?000.  */
  
  /* Reversed means colors are reversed.  Note that this does not
     reverse the intensity.  */
  if (attr.reversed)
    {
      fgcol = attr.bgcol;
      bgcol = attr.fgcol;
    }
  else
    {
      fgcol = attr.fgcol;
      bgcol = attr.bgcol;
    }

  /* Set the foreground color.  */
  if (attr.concealed)
    fgcol = bgcol;
  else
    {
      /* Intensity bold and dim also affect the font selection bit.  */
      switch (attr.intensity)
	{
	case CONS_ATTR_INTENSITY_BOLD:
	  fgcol |= 1 << 3;
	  break;
	case CONS_ATTR_INTENSITY_DIM:
	  fgcol = CONS_COLOR_BLACK | 1 << 3;
	  break;
	case CONS_ATTR_INTENSITY_NORMAL:
	  break;
	}
    }

  /* Try to get the colors as desired.  This might change the palette,
     so we need to take the lock (in case a flash operation times
     out).  */
  mutex_lock (&vga_display_lock);
  res_bgcol = dynacolor_lookup (*dc, bgcol);
  res_fgcol = dynacolor_lookup (*dc, fgcol);
  mutex_unlock (&vga_display_lock);
  if (res_bgcol == -1 || res_fgcol == -1)
    dynacolor_replace_colors (dc, fgcol, bgcol, &res_fgcol, &res_bgcol);
  vga_attr = res_bgcol << 4 | res_fgcol;

  vga_attr |= attr.blinking << 7;
  
  /* XXX We can support underlined in a monochrome mode.  */
  return vga_attr;
}


/* Deallocate any scarce resources occupied by the LENGTH characters
   from column COL and row ROW.  */
static error_t
vga_display_clear (void *handle, size_t length, uint32_t col, uint32_t row)
{
  struct vga_display *disp = handle;
  struct refchr *refpos = &disp->refmatrix[row][col];

  while (length > 0)
    {
      if (refpos->used)
	{
	  dynafont_release (disp->df, refpos->chr);
	  /* We intimately know that reference counting is only done
	     for the up to 8 colors mode.  */
	  dynacolor_release (disp->dc, refpos->attr & 7);
	  dynacolor_release (disp->dc, (refpos->attr >> 4) & 7);
	  refpos->used = 0;
	}
      refpos++;
      length--;
    }
  return 0;
}

/* Write the text STR with LENGTH characters to column COL and row
   ROW.  */
static error_t
vga_display_write (void *handle, conchar_t *str, size_t length,
		   uint32_t col, uint32_t row)
{
  struct vga_display *disp = handle;
  char *pos = vga_videomem + 2 * (row * disp->width + col);
  struct refchr *refpos = &disp->refmatrix[row][col];

  /* Although all references to the current fgcol or bgcol could have
     been released here, for example due to a scroll operation, we
     know that the color slots have not been reused yet, as no routine
     but ours does color allocation.  This ensures that cur_attr is
     still valid.  XXX consider recalculating the attribute for more
     authentic (but less homogenous) colors anyway.  */

  while (length--)
    {
      int charval = dynafont_lookup (disp->df, str);
      if (!disp->cur_conchar_attr_init
	  || *(uint32_t *) &disp->cur_conchar_attr != *(uint32_t *) &str->attr)
	{
	  if (!disp->cur_conchar_attr_init)
	    disp->cur_conchar_attr_init = 1;
	  disp->cur_conchar_attr = str->attr;
	  disp->cur_attr = vga_display_recalculate_attr (&disp->dc, str->attr);
	}
      else
	{
	  /* Add two references to the colors.  See comment above for
	     why we can assume that this will succeed.  */
	  /* We intimately know that reference counting is only done
	     for the up to 8 colors mode.  */
	  dynacolor_add_ref (disp->dc, disp->cur_attr & 7);
	  dynacolor_add_ref (disp->dc, (disp->cur_attr >> 4) & 7);
	}

      *(pos++) = charval & 0xff;
      *(pos++) = disp->cur_attr
	| (disp->df_size == 512 ? (charval >> 5) & 0x8 : 0);

      /* Perform reference counting.  */
      if (refpos->used)
	{
	  dynafont_release (disp->df, refpos->chr);
	  /* We intimately know that reference counting is only done
	     for the up to 8 colors mode.  */
	  dynacolor_release (disp->dc, refpos->attr & 7);
	  dynacolor_release (disp->dc, (refpos->attr >> 4) & 7);
	}
      refpos->used = 1;
      refpos->chr = charval;
      refpos->attr = disp->cur_attr;
      refpos++;

      /* Wrap around displayed area.  */
      str++;
    }
  return 0;
}


struct driver_ops driver_vga_ops =
  {
    vga_display_init,
    vga_display_start,
    vga_display_fini,
  };

static struct display_ops vga_display_ops =
  {
    vga_display_set_cursor_pos,
    vga_display_set_cursor_status,
    vga_display_scroll,
    vga_display_clear,
    vga_display_write,
    NULL,
    vga_display_flash,
    NULL
  };
