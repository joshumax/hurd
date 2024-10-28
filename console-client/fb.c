/*
   Copyright (C) 2024 Free Software Foundation, Inc.

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

#include <assert-backtrace.h>
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
#include <pthread.h>
#include <hurd/console.h>
#include <device/device.h>
#include <hurd.h>
#include <mach.h>

#include "driver.h"

#include "fb.h"
#include "vga-hw.h"
#include "vga-support.h"
#include "bdf.h"
#include "unicode.h"

/* The font file.  */
#define DEFAULT_VGA_FONT DEFAULT_VGA_FONT_DIR "vga-system.bdf"
static char *fb_display_font;

off_t fb_ptr;
int fb_type;
int fb_width;
int fb_height;
int fb_bpp;
int fb_wc;
int fb_hc;

static unsigned char question_mark[32] = {
	0x7E,	/*  ******  */
	0xC3,	/* **    ** */
	0x99,	/* *  **  * */
	0x99,	/* *  **  * */
	0xF9,	/* *****  * */
	0xF3,	/* ****  ** */
	0xF3,	/* ***  *** */
	0xE7,	/* ***  *** */
	0xFF,	/* ******** */
	0xE7,	/* ***  *** */
	0xE7,	/* ***  *** */
	0x7E,	/*  ******  */
	0
};

static struct bdf_glyph qmark = {
  .name = "missing",
  .encoding = 0,
  .internal_encoding = 0,
  .bbox = { 8, 12, 0, 0 },
  .bitmap = &question_mark[0],
};

/* Is set to 1 if the cursor state should be hidden.  */
static int cursor_hidden;
static int cursor_state;
static int cursor_pos_x = 0;
static int cursor_pos_y = 0;

static int current_width;
static int current_height;

/* FIXME: inherit previous char colours */
static int current_fg = 7;
static int current_bg = 0;

#define fb_pos(_col, _row) (vga_videomem + fb_bpp/8 * ( (_row) * fb_hc * disp->width + (_col) * fb_wc ))
#define CURSOR_GLYPH	0x2581
#define CURSOR_COLOUR	7



error_t
fb_get_multiboot_params (void)
{
  error_t ret = 0;
  mach_port_t master_device, mbinfo_dev;
  struct multiboot_raw_info mbi;
  char buf[sizeof(struct multiboot_raw_info)];
  char *bufptr = &buf[0];
  uint32_t bytes = sizeof(struct multiboot_raw_info);
  uint32_t bytes_read = 0;

  ret = get_privileged_ports (NULL, &master_device);
  if (ret)
    goto fail;

  ret = device_open (master_device, D_READ, "mbinfo", &mbinfo_dev);
  mach_port_deallocate (mach_task_self (), master_device);
  if (ret)
    goto fail;

  ret = device_read (mbinfo_dev, D_READ, 0, bytes, &bufptr, &bytes_read);
  mach_port_deallocate (mach_task_self (), mbinfo_dev);
  if (ret)
    goto fail;

  if (bytes_read != bytes)
    goto fail;

  memcpy((void *)&mbi, (void *)bufptr, sizeof(struct multiboot_raw_info));

  fb_ptr = mbi.fb_info.framebuffer_addr;
  fb_type = mbi.fb_info.framebuffer_type;
  fb_width = mbi.fb_info.framebuffer_width;
  fb_height = mbi.fb_info.framebuffer_height;
  fb_bpp = mbi.fb_info.framebuffer_bpp;
  fb_wc = FONT_PIXELS_W;
  fb_hc = FONT_PIXELS_H;
  return ret;

fail:
  /* Fall back to EGA text mode */
  fb_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;
  return ret;
}

static struct bdf_glyph *
always_find_glyph(bdf_font_t f, int ch)
{
  struct bdf_glyph *g;

  g = bdf_find_glyph (f, ch, 0);
  if (!g)
    g = bdf_find_glyph (f, -1, ch);
  if (!g)
    g = &qmark;

  return g;
}

static void
blit_glyph(bdf_font_t f, char *mem, int ch, uint32_t fg, uint32_t bg, int width, int bpp)
{
  int w, h;

  struct bdf_glyph *gl = always_find_glyph(f, ch);

  if (bg == -1)
    bg = current_bg;
  if (fg == -1)
    fg = current_fg;

  for (h = 0; h < fb_hc; h++)
    {
      for (w = 0; w < fb_wc; w++)
        {
          char *pixel = mem + bpp/8 * (w + width * h);
          uint32_t colour = (gl->bitmap[h] & (1 << (7-w))) ? fg : bg;
          pixel[0] = (colour >> 16) & 0xff;
          pixel[1] = (colour >> 8) & 0xff;
          pixel[2] = colour & 0xff;
        }
    }
}

static void
blit_glyph_xor(bdf_font_t f, char *mem, int ch, uint32_t fg, uint32_t bg, int width, int bpp)
{
  int w, h;

  struct bdf_glyph *gl = always_find_glyph(f, ch);

  if (bg == -1)
    bg = current_bg;
  if (fg == -1)
    fg = current_fg;

  for (h = 0; h < fb_hc; h++)
    {
      for (w = 0; w < fb_wc; w++)
        {
          char *pixel = mem + bpp/8 * (w + width * h);
          uint32_t colour = (gl->bitmap[h] & (1 << (7-w))) ? fg : bg;
          pixel[0] ^= (colour >> 16) & 0xff;
          pixel[1] ^= (colour >> 8) & 0xff;
          pixel[2] ^= colour & 0xff;
        }
    }
}

static error_t
fb_init(void)
{
  error_t err;
  int fd;

  fd = open ("/dev/mem", O_RDWR);
  if (fd < 0)
    return errno;

  vga_videomem = mmap (0, fb_width * fb_height * fb_bpp/8, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, fb_ptr);
  err = errno;
  close (fd);
  if (vga_videomem == MAP_FAILED)
    return err;

  /* Clear screen */
  memset (vga_videomem, 0, fb_width * fb_height * fb_bpp/8);
  return 0;
}

static void
fb_fini(void)
{
  munmap (vga_videomem, fb_width * fb_height * fb_bpp/8);
}

/* Start the driver.  */
error_t
fb_display_start (void *handle)
{
  error_t err;
  struct fb_display *disp = handle;
  FILE *font_file;

  err = fb_init ();
  if (err)
    return err;

#define LOAD_FONT(x,y,z)						\
  do {									\
  font_file = fopen (fb_display_##x ?: DEFAULT_VGA_##y, "r");		\
  if (font_file)							\
    {									\
      bdf_error_t bdferr = bdf_read (font_file, &z, NULL);		\
      if (bdferr)							\
        {								\
          z = NULL;							\
	  err = ENOSYS;							\
	}								\
      else								\
	bdf_sort_glyphs (z);						\
      fclose (font_file);						\
    }									\
  else									\
    err = ENOSYS;							\
  } while (0)

  LOAD_FONT (font, FONT, disp->font);
  if (err)
    {
      fb_fini ();
      free (disp);
      return err;
    }

  err = driver_add_display (&fb_display_ops, disp);
  if (err)
    {
      fb_fini ();
      free (disp);
    }
  return err;
}

/* Destroy the display HANDLE.  */
error_t
fb_display_fini (void *handle, int force)
{
  struct fb_display *disp = handle;

  driver_remove_display (&fb_display_ops, disp);
  bdf_destroy (disp->font);
  free (disp);
  fb_fini ();
  free (fb_display_font);

  return 0;
}

uint32_t ansi_colour[8] = {
  0x000000, /* black */
  0xaa0000, /* red */
  0x00aa00, /* green */
  0xaa5500, /* yellow */
  0x0000aa, /* blue */
  0xaa00aa, /* magenta */
  0x00aaaa, /* cyan */
  0xaaaaaa  /* white */
};

uint32_t ansi_colour_bold[8] = {
  0x555555, /* bright black */
  0xff5555, /* bright red */
  0x55ff55, /* bright green */
  0xffff55, /* bright yellow */
  0x5555ff, /* bright blue */
  0xff55ff, /* bright magenta */
  0x55ffff, /* bright cyan */
  0xffffff, /* bright white */
};

static void
hide_mousecursor (struct fb_display *disp)
{
  char *oldpos = fb_pos((int)disp->mousecursor.posx, (int)disp->mousecursor.posy);

  if (!disp->mousecursor.visible)
    return;

  /* First remove the old cursor.  */
  blit_glyph_xor (disp->font, oldpos, 'X', 0x00ff00, -1, disp->width, fb_bpp);
  disp->mousecursor.visible = 0;
}


static void
draw_mousecursor (struct fb_display *disp)
{
  char *newpos = fb_pos((int)disp->mousecursor.posx, (int)disp->mousecursor.posy);

  if (disp->mousecursor.visible)
    return;

  /* Draw the new cursor.  */
  blit_glyph_xor (disp->font, newpos, 'X', 0x00ff00, -1, disp->width, fb_bpp);

  disp->mousecursor.visible = 1;
}


static void
hide_cursor(struct fb_display *disp)
{
  char *curpos;

  if (cursor_hidden)
    return;

  /* Remove old cursor */
  curpos = fb_pos(cursor_pos_x, cursor_pos_y);
  blit_glyph_xor (disp->font, curpos, CURSOR_GLYPH, ansi_colour[CURSOR_COLOUR], -1, disp->width, fb_bpp);
  cursor_hidden = 1;
}

static void
draw_cursor(struct fb_display *disp)
{
  char *curpos;

  if (!cursor_hidden)
    return;

  /* Add new cursor */
  curpos = fb_pos(cursor_pos_x, cursor_pos_y);
  blit_glyph_xor (disp->font, curpos, CURSOR_GLYPH, ansi_colour[CURSOR_COLOUR], -1, disp->width, fb_bpp);
  cursor_hidden = 0;
}

/* Set the cursor's state to STATE on display HANDLE.  */
static error_t
fb_display_set_cursor_status (void *handle, uint32_t state)
{
  struct fb_display *disp = handle;

  cursor_state = state;

  if (!state)
    hide_cursor (disp);
  else
    draw_cursor (disp);

  return 0;
}


/* Set the cursor's position on display HANDLE to column COL and row
   ROW.  */
static error_t
fb_display_set_cursor_pos (void *handle, uint32_t col, uint32_t row)
{
  struct fb_display *disp = handle;

  /* If the cursor did not move from the character position, don't
     bother about updating the cursor position.  */
  if (cursor_state && (col == cursor_pos_x) && (row == (cursor_pos_y)))
    return 0;

  if (cursor_state)
    hide_cursor (disp);

  cursor_pos_x = col;
  cursor_pos_y = row;

  if (cursor_state)
    draw_cursor (disp);

  return 0;
}

/* Deallocate any scarce resources occupied by the LENGTH characters
   from column COL and row ROW.  */
static error_t
fb_display_clear (void *handle, size_t length, uint32_t col, uint32_t row)
{
  return 0;
}

/* Scroll the display by the desired number of lines.  The area that becomes
   free will be filled in a subsequent write call.  */
static error_t
fb_display_scroll (void *handle, int delta)
{
  struct fb_display *disp = handle;
  int pixels, chars;
  uint32_t r;

  if (abs(delta) > disp->height/fb_hc)
    return ENOTSUP;

  pixels = abs(delta)*fb_hc * disp->width;
  chars = abs(delta) * disp->width/fb_wc;

  hide_mousecursor (disp);
  hide_cursor (disp);

  /* XXX: If the virtual console is bigger than the physical console it is
     impossible to scroll because the data to scroll is not in memory.  */
  if (current_height > disp->height/fb_hc)
    return ENOTSUP;

  if (delta > 0)
    {
      memmove (vga_videomem, vga_videomem + fb_bpp/8 * pixels,
	       fb_bpp/8 * disp->width * (disp->height - delta*fb_hc));
    }
  else
    {
      memmove (vga_videomem + fb_bpp/8 * pixels, vga_videomem,
	       fb_bpp/8 * disp->width * (disp->height + delta*fb_hc));
    }

  if (delta > 0)
    {
      r = disp->height/fb_hc - delta;
      memmove (&disp->refmatrix[0][0], &disp->refmatrix[0][0] + chars,
	       sizeof (struct fbchr) * disp->width/fb_wc * r);
    }
  else
    {
      r = 0;
      memmove (&disp->refmatrix[0][0] + chars, &disp->refmatrix[0][0],
	       sizeof (struct fbchr) * disp->width/fb_wc * (disp->height/fb_hc + delta));
    }

  return 0;
}



/* Write the text STR with LENGTH characters to column COL and row
   ROW.  */
static error_t
fb_display_write (void *handle, conchar_t *str, size_t length,
		   uint32_t col, uint32_t row)
{
  struct fb_display *disp = handle;
  char *pos;
  struct fbchr *refpos = &disp->refmatrix[row][col];
  char *mouse_cursor_pos;

  hide_mousecursor (disp);
  hide_cursor (disp);

  /* The starting column is outside the physical screen.  */
  if (disp->width/fb_wc < current_width && col >= disp->width/fb_wc)
    {
      size_t skip = current_width - disp->width/fb_wc;
      str += skip;
      length -= skip;
      col = 0;
      row += 1;
    }

  mouse_cursor_pos = fb_pos((int)disp->mousecursor.posx, (int)disp->mousecursor.posy);

  while (length--)
    {
      int charval = str->chr;
      int fg, bg;

      /* The virtual console is smaller than the physical screen.  */
      if (col >= current_width)
        {
          size_t skip = disp->width/fb_wc - current_width;
          refpos += skip;
          col = 0;
          row += 1;
        }
      /* The virtual console is bigger than the physical console.  */
      else if (disp->width/fb_wc < current_width && col == disp->width/fb_wc)
        {
          size_t skip = current_width - disp->width/fb_wc;
          str += skip;
          length -= skip;
          col = 0;
          row += 1;
        }

      /* The screen is filled until the bottom of the screen.  */
      if (row >= disp->height/fb_hc)
        return 0;

      pos = fb_pos(col, row);

      /* blit glyph to screen */
      fg = (str->attr.intensity == CONS_ATTR_INTENSITY_BOLD)
         ? ansi_colour_bold[str->attr.fgcol]
         : ansi_colour[str->attr.fgcol];
      bg = ansi_colour[str->attr.bgcol];
      blit_glyph(disp->font, pos, charval, fg, bg, disp->width, fb_bpp);

      if (pos == mouse_cursor_pos)
        disp->mousecursor.visible = 0;

      refpos->used = 1;
      refpos->chr = charval;
      refpos->fgcol = fg;
      refpos->bgcol = bg;
      refpos++;
      col++;

      /* Go to next character.  */
      str++;
    }
  return 0;
}

static error_t
fb_set_dimension (void *handle, unsigned int width, unsigned int height)
{
  current_width = width;
  current_height = height;

  return 0;
}


static error_t
fb_display_update (void *handle)
{
  struct fb_display *disp = handle;

  if (disp->mousecursor.enabled)
    draw_mousecursor (disp);

  if (cursor_state)
    draw_cursor (disp);

  return 0;
}


static error_t
fb_set_mousecursor_pos (void *handle, float x, float y)
{
  struct fb_display *disp = handle;

  /* If the mouse did not move from the character position, don't
     bother about updating the cursor position.  */
  if (disp->mousecursor.visible && x == (int) disp->mousecursor.posx
      && y == (int) disp->mousecursor.posy)
    return 0;

  if (disp->mousecursor.enabled)
    hide_mousecursor (disp);

  disp->mousecursor.posx = x;
  disp->mousecursor.posy = y;

  if (disp->mousecursor.enabled)
    draw_mousecursor (disp);

  return 0;
}


static error_t
fb_set_mousecursor_status (void *handle, int status)
{
  struct fb_display *disp = handle;

  disp->mousecursor.enabled = status;
  if (!status)
    hide_mousecursor (disp);
  else
    draw_mousecursor (disp);

  return 0;
}



struct display_ops fb_display_ops =
  {
    fb_display_set_cursor_pos,
    fb_display_set_cursor_status,
    fb_display_scroll,
    fb_display_clear,
    fb_display_write,
    fb_display_update,
    NULL,
    NULL,
    fb_set_dimension,
    fb_set_mousecursor_pos,
    fb_set_mousecursor_status
  };
