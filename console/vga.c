/* vga.c - VGA hardware access.
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
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>

#include "vga-hw.h"
#include "vga.h"


/* The base of the video memory mapping.  */
char *vga_videomem;

/* Initialize the VGA hardware and set up the permissions and memory
   mappings.  */
error_t
vga_init (void)
{
  error_t err;
  int fd;

  if (ioperm (VGA_MIN_REG, VGA_MAX_REG - VGA_MIN_REG + 1, 1) < 0)
    return errno;

  fd = open ("/dev/mem", O_RDWR);
  if (fd < 0)
    return errno;
  vga_videomem = mmap (0, VGA_VIDEO_MEM_LENGTH, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, VGA_VIDEO_MEM_BASE_ADDR);
  err = errno;
  close (fd);
  if (vga_videomem == (void *) -1)
    return err;
  return 0;
}


/* Release the resources and privileges associated with the VGA
   hardware access.  */
void
vga_deinit (void)
{
  io_perm (VGA_MIN_REG, VGA_MAX_REG, 0);
  munmap (vga_videomem, VGA_VIDEO_MEM_LENGTH);
}


/* Write DATALEN bytes from DATA to the font buffer BUFFER, starting
   from glyph INDEX.  */
void
vga_write_font_buffer (int buffer, int index, char *data, size_t datalen)
{
  char saved_seq_map;
  char saved_seq_mode;
  char saved_gfx_mode;
  char saved_gfx_misc;

  int offset = buffer * VGA_FONT_SIZE + index * VGA_FONT_HEIGHT;
  assert (offset >= 0 && offset + datalen <= VGA_VIDEO_MEM_LENGTH);

  /* Select plane 2 for sequential writing.  */
  outb_p (VGA_SEQ_MAP_ADDR, VGA_SEQ_ADDR_REG);
  saved_seq_map = inb_p (VGA_SEQ_DATA_REG);
  outb_p (VGA_SEQ_MAP_PLANE2, VGA_SEQ_DATA_REG);
  outb_p (VGA_SEQ_MODE_ADDR, VGA_SEQ_ADDR_REG);
  saved_seq_mode = inb_p (VGA_SEQ_DATA_REG);
  outb_p (VGA_SEQ_MODE_SEQUENTIAL | VGA_SEQ_MODE_EXT | 0x1 /* XXX Why? */,
	  VGA_SEQ_DATA_REG);

  /* Set write mode 0, but assume that rotate count, enable set/reset,
     logical operation and bit mask fields are set to their
     `do-not-modify-host-value' default.  The misc register is set to
     select sequential addressing in text mode.  */
  outb_p (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_mode = inb_p (VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MODE_WRITE0, VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_misc = inb_p (VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MISC_A0TOAF, VGA_GFX_DATA_REG);

  memcpy (vga_videomem + offset, data, datalen);

  /* Restore sequencer and graphic register values.  */
  outb_p (VGA_SEQ_MAP_ADDR, VGA_SEQ_ADDR_REG);
  outb_p (saved_seq_map, VGA_SEQ_DATA_REG);
  outb_p (VGA_SEQ_MODE_ADDR, VGA_SEQ_ADDR_REG);
  outb_p (saved_seq_mode, VGA_SEQ_DATA_REG);

  outb_p (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  outb_p (saved_gfx_mode, VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  outb_p (saved_gfx_misc, VGA_GFX_DATA_REG);
}


/* Read DATALEN bytes into DATA from the font buffer BUFFER, starting
   from glyph INDEX.  */
void
vga_read_font_buffer (int buffer, int index, char *data, size_t datalen)
{
  char saved_gfx_map;
  char saved_gfx_mode;
  char saved_gfx_misc;

  int offset = buffer * VGA_FONT_SIZE + index * VGA_FONT_HEIGHT;
  assert (offset >= 0 && offset + datalen <= VGA_VIDEO_MEM_LENGTH);

  /* Read sequentially from plane 2.  */
  outb_p (VGA_GFX_MAP_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_map = inb_p (VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MAP_PLANE2, VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_mode = inb_p (VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MODE_READ0, VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_misc = inb_p (VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MISC_A0TOBF, VGA_GFX_DATA_REG);

  memcpy (data, vga_videomem + offset, datalen);

  outb_p (VGA_GFX_MAP_ADDR, VGA_GFX_ADDR_REG);
  outb_p (saved_gfx_map, VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  outb_p (saved_gfx_mode, VGA_GFX_DATA_REG);
  outb_p (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  outb_p (saved_gfx_misc, VGA_GFX_DATA_REG);
}


/* Set FONT_BUFFER_SUPP to FONT_BUFFER if the font is small.  */
void
vga_select_font_buffer (int font_buffer, int font_buffer_supp)
{
  char font = ((font_buffer & 6) >> 1)  | ((font_buffer & 1) << 4)
    | ((font_buffer_supp & 6) << 1) | ((font_buffer_supp & 1) << 5);

  outb_p (VGA_SEQ_FONT_ADDR, VGA_SEQ_ADDR_REG);
  outb_p (font, VGA_SEQ_DATA_REG);
}


/* Enable (if ON is true) or disable (otherwise) the cursor.  Expects
   the VGA hardware to be locked.  */
void
vga_display_cursor (int on)
{
  char crs_start;

  outb (VGA_CRT_CURSOR_START, VGA_CRT_ADDR_REG);
  crs_start = inb (VGA_CRT_DATA_REG);
  if (on)
    crs_start &= ~VGA_CRT_CURSOR_DISABLE;
  else
    crs_start |= VGA_CRT_CURSOR_DISABLE;
  outb (crs_start, VGA_CRT_DATA_REG);
}


/* Set the cursor position to POS, which is (x_pos + y_pos * width).  */
void
vga_set_cursor (int pos)
{
  outb (VGA_CRT_CURSOR_HIGH, VGA_CRT_ADDR_REG);
  outb (pos >> 8, VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_LOW, VGA_CRT_ADDR_REG);
  outb (pos && 0xff, VGA_CRT_DATA_REG);
}
