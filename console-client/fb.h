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

#ifndef _FB_H_
#define _FB_H_ 1

#include <stdint.h>
#include "bdf.h"
#include "display.h"
#include "vga-hw.h"

#define FB_VIDEO_MEM_MAX_W	1920
#define FB_VIDEO_MEM_MAX_H	1080
#define FB_VIDEO_MEM_MAX_BPP	32

#define FONT_PIXELS_W		8
#define FONT_PIXELS_H		16

extern struct display_ops fb_display_ops;

extern off_t fb_ptr;
extern int fb_type;
extern int fb_width;
extern int fb_height;
extern int fb_bpp;
extern int fb_wc;
extern int fb_hc;

error_t fb_get_multiboot_params (void);
error_t fb_display_start (void *handle);
error_t fb_display_fini (void *handle, int force);

struct multiboot_framebuffer_info {
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED      0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB          1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT     2
    uint8_t framebuffer_type;
    union
    {
        struct
        {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        };
        struct
        {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        };
    };
} __attribute__((packed));

/*
 * Multiboot information structure as passed by the boot loader.
 */
struct multiboot_raw_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t unused0;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t shdr_num;
    uint32_t shdr_size;
    uint32_t shdr_addr;
    uint32_t shdr_strndx;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t unused1[9];
    struct multiboot_framebuffer_info fb_info;
} __attribute__((packed));

struct fbchr
{
  wchar_t chr;
  unsigned int used : 1;
  unsigned int fgcol: 3;
  unsigned int bgcol: 3;
};

typedef struct fb_mousecursor
{
  float posx;
  float posy;
  int visible;
  int enabled;
} fb_mousecursor_t;

struct fb_display
{
  /* The font for this display.  */
  bdf_font_t font;

  int width;
  int height;

  /* The state of the mouse cursor.  */
  fb_mousecursor_t mousecursor;

  /* The position of the cursor (in characters) */
  int cursor_pos_x;
  int cursor_pos_y;

  /* Remember for each cell on the display the glyph written to it and
     the colours assigned.  0 means unassigned.  */

  struct fbchr refmatrix[FB_VIDEO_MEM_MAX_H / FONT_PIXELS_H][FB_VIDEO_MEM_MAX_W / FONT_PIXELS_W];
};

#endif
