/* vga-support.h - Interface for VGA hardware access.
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

#ifndef _VGA_SUPPORT_H_
#define _VGA_SUPPORT_H_ 1

#include <errno.h>
#include <sys/types.h>


/* The VGA interface does not do locking on its own, for maximum
   efficiency it relies on locking by the caller (because usually the
   caller has some other data structures to lock along with the
   VGA hardware.  */

/* The mapped video memory.  */
extern char *vga_videomem;

/* Initialize the VGA hardware and set up the permissions and memory
   mappings.  */
error_t vga_init (void);

/* Release the resources and privileges associated with the VGA
   hardware access.  */
void vga_fini (void);

/* Write DATALEN bytes from DATA to the font buffer BUFFER, starting
   from glyph index.  */
void vga_write_font_buffer (int buffer, int index,
			    unsigned char *data, size_t datalen);

/* Read DATALEN bytes into DATA from the font buffer BUFFER, starting
   from glyph INDEX.  */
void vga_read_font_buffer (int buffer, int index,
			   unsigned char *data, size_t datalen);

/* Set FONT_BUFFER_SUPP to FONT_BUFFER if the font is small.  */
void vga_select_font_buffer (int font_buffer, int font_buffer_supp);

/* Set the font height in pixel.  */
void vga_set_font_height (int height);

/* Get the font height in pixel.  Can be 8 or 9.  */
int vga_get_font_width (void);

/* Set the font height in pixel.  WIDTH can be 8 or 9.  */
void vga_set_font_width (int width);

/* Enable (if ON is true) or disable (otherwise) the cursor.  Expects
   the VGA hardware to be locked.  */
void vga_display_cursor (int on);

/* Set cursor size from START to END (set to -1 to not set one of the
   values).  */
void vga_set_cursor_size (int start, int end);

/* Set the cursor position to POS, which is (x_pos + y_pos *
   width).  */
void vga_set_cursor_pos (unsigned int pos);

/* Read NR entries from the color palette, starting from INDEX.  */
void vga_read_palette (unsigned char index, unsigned char *data, int nr);

/* Write NR entries to the color palette, starting from INDEX.  */
void vga_write_palette (unsigned char index,
			const unsigned char *data, int nr);

void vga_exchange_palette_attributes (unsigned char index,
				      unsigned char *saved_palette_attr,
				      int nr);

#endif	/* _VGA_SUPPORT_H_ */
