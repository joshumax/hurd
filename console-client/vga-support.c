/* vga-support.c - VGA hardware access.
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
#include "vga-support.h"


/* The base of the video memory mapping.  */
char *vga_videomem;

/* The saved state of the VGA card.  */
struct vga_state
{
  unsigned char seq_clock_mode;
  unsigned char seq_map;
  unsigned char seq_font;
  unsigned char seq_mode;

  unsigned char gfx_map;
  unsigned char gfx_mode;
  unsigned char gfx_misc;

  unsigned char crt_max_scan_line;
  unsigned char crt_cursor_start;
  unsigned char crt_cursor_end;
  unsigned char crt_cursor_high;
  unsigned char crt_cursor_low;

  char videomem[2 * 80 * 25];
  unsigned char fontmem[2 * VGA_FONT_SIZE * VGA_FONT_HEIGHT];
};

static struct vga_state *vga_state;


#if OSKIT_MACH
#else

#include <device/device.h>
#include <hurd.h>

/* Constants from Mach.  */
#define VIDMMAP_BEGIN 0xA0000
#define VIDMMAP_SIZE (0xC0000 - 0xA0000)
#define VIDMMAP_KDOFS 0xA0000 /* == kd_bitmap_start in mach/i386/i386at/kd.c */

#endif

error_t
vga_init (void)
{
  error_t err;
#if OSKIT_MACH
  int fd;
#else
  device_t device_master = MACH_PORT_NULL;
  memory_object_t kd_mem = MACH_PORT_NULL;
  static device_t kd_device = MACH_PORT_NULL;
  vm_address_t mapped;
#endif

#if OSKIT_MACH
  if (ioperm (VGA_MIN_REG, VGA_MAX_REG - VGA_MIN_REG + 1, 1) < 0)
    {
      free (vga_state);
      return errno;
    }

  fd = open ("/dev/mem", O_RDWR);
  if (fd < 0)
    return errno;
  vga_videomem = mmap (0, VGA_VIDEO_MEM_LENGTH, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, VGA_VIDEO_MEM_BASE_ADDR);
  err = errno;
  close (fd);
  if (vga_videomem == (void *) -1)
    return err;
#else
  err = get_privileged_ports (0, &device_master);
  if (err)
    return err;

  err = device_open (device_master, D_WRITE, "kd", &kd_device);
  if (err)
    return err;

  err = device_map (kd_device, VM_PROT_READ | VM_PROT_WRITE,
                    VIDMMAP_BEGIN - VIDMMAP_KDOFS, VIDMMAP_SIZE,
                    &kd_mem, 0);
  if (err)
    return err;

  err = vm_map (mach_task_self (), &mapped, VIDMMAP_SIZE,
                0, 1, kd_mem, VIDMMAP_BEGIN - VIDMMAP_KDOFS, 0,
                VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
                VM_INHERIT_NONE);
  if (err)
    return err;

  vga_videomem = (char *) mapped;
  assert (vga_videomem != NULL);

  mach_port_deallocate (mach_task_self (), device_master);
  mach_port_deallocate (mach_task_self (), kd_mem);
#endif

  /* Save the current state.  */
  vga_state = malloc (sizeof (*vga_state));
  if (!vga_state)
    return errno;

  outb (VGA_SEQ_CLOCK_MODE_ADDR, VGA_SEQ_ADDR_REG);
  vga_state->seq_clock_mode = inb (VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MAP_ADDR, VGA_SEQ_ADDR_REG);
  vga_state->seq_map = inb (VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_FONT_ADDR, VGA_SEQ_ADDR_REG);
  vga_state->seq_font = inb (VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MODE_ADDR, VGA_SEQ_ADDR_REG);
  vga_state->seq_mode = inb (VGA_SEQ_DATA_REG);

  outb (VGA_GFX_MAP_ADDR, VGA_GFX_ADDR_REG);
  vga_state->gfx_map = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  vga_state->gfx_mode = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  vga_state->gfx_misc = inb (VGA_GFX_DATA_REG);

  outb (VGA_CRT_MAX_SCAN_LINE, VGA_CRT_ADDR_REG);
  vga_state->crt_max_scan_line = inb (VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_START, VGA_CRT_ADDR_REG);
  vga_state->crt_cursor_start = inb (VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_END, VGA_CRT_ADDR_REG);
  vga_state->crt_cursor_end = inb (VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_HIGH, VGA_CRT_ADDR_REG);
  vga_state->crt_cursor_high = inb (VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_LOW, VGA_CRT_ADDR_REG);
  vga_state->crt_cursor_low = inb (VGA_CRT_DATA_REG);

  /* Read/write in interleaved mode.  */
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  outb (VGA_GFX_MISC_CHAINOE | VGA_GFX_MISC_A0TOAF, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  outb (VGA_GFX_MODE_HOSTOE, VGA_GFX_DATA_REG);

  memcpy (vga_state->videomem, vga_videomem, 2 * 80 * 25);
  vga_read_font_buffer (0, 0, vga_state->fontmem,
			2 * VGA_FONT_SIZE * VGA_FONT_HEIGHT);

  /* 80 cols, 25 rows, two bytes per cell and twice because with lower
     max scan line we get more lines on the screen.  */
  memset (vga_videomem, 0, 80 * 25 * 2 * 2);

  return 0;
}


/* Release the resources and privileges associated with the VGA
   hardware access.  */
void
vga_fini (void)
{
  /* Recover the saved state.  */
  vga_write_font_buffer (0, 0, vga_state->fontmem,
			 2 * VGA_FONT_SIZE * VGA_FONT_HEIGHT);
  memcpy (vga_videomem, vga_state->videomem, 2 * 80 * 25);

  /* Restore the registers.  */
  outb (VGA_SEQ_CLOCK_MODE_ADDR, VGA_SEQ_ADDR_REG);
  outb (vga_state->seq_clock_mode, VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MAP_ADDR, VGA_SEQ_ADDR_REG);
  outb (vga_state->seq_map, VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_FONT_ADDR, VGA_SEQ_ADDR_REG);
  outb (vga_state->seq_font, VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MODE_ADDR, VGA_SEQ_ADDR_REG);
  outb (vga_state->seq_mode, VGA_SEQ_DATA_REG);

  outb (VGA_GFX_MAP_ADDR, VGA_GFX_ADDR_REG);
  outb (vga_state->gfx_map, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  outb (vga_state->gfx_mode, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  outb (vga_state->gfx_misc, VGA_GFX_DATA_REG);

  outb (VGA_CRT_MAX_SCAN_LINE, VGA_CRT_ADDR_REG);
  outb (vga_state->crt_max_scan_line, VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_START, VGA_CRT_ADDR_REG);
  outb (vga_state->crt_cursor_start, VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_END, VGA_CRT_ADDR_REG);
  outb (vga_state->crt_cursor_end, VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_HIGH, VGA_CRT_ADDR_REG);
  outb (vga_state->crt_cursor_high, VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_LOW, VGA_CRT_ADDR_REG);
  outb (vga_state->crt_cursor_low, VGA_CRT_DATA_REG);

  ioperm (VGA_MIN_REG, VGA_MAX_REG, 0);
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
  outb (VGA_SEQ_MAP_ADDR, VGA_SEQ_ADDR_REG);
  saved_seq_map = inb (VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MAP_PLANE2, VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MODE_ADDR, VGA_SEQ_ADDR_REG);
  saved_seq_mode = inb (VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MODE_SEQUENTIAL | VGA_SEQ_MODE_EXT | 0x1 /* XXX Why? */,
	VGA_SEQ_DATA_REG);

  /* Set write mode 0, but assume that rotate count, enable set/reset,
     logical operation and bit mask fields are set to their
     `do-not-modify-host-value' default.  The misc register is set to
     select sequential addressing in text mode.  */
  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_mode = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_WRITE0, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_misc = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_A0TOAF, VGA_GFX_DATA_REG);

  memcpy (vga_videomem + offset, data, datalen);

  /* Restore sequencer and graphic register values.  */
  outb (VGA_SEQ_MAP_ADDR, VGA_SEQ_ADDR_REG);
  outb (saved_seq_map, VGA_SEQ_DATA_REG);
  outb (VGA_SEQ_MODE_ADDR, VGA_SEQ_ADDR_REG);
  outb (saved_seq_mode, VGA_SEQ_DATA_REG);

  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  outb (saved_gfx_mode, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  outb (saved_gfx_misc, VGA_GFX_DATA_REG);
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
  outb (VGA_GFX_MAP_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_map = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MAP_PLANE2, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_mode = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_READ0, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  saved_gfx_misc = inb (VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_A0TOBF, VGA_GFX_DATA_REG);

  memcpy (data, vga_videomem + offset, datalen);

  outb (VGA_GFX_MAP_ADDR, VGA_GFX_ADDR_REG);
  outb (saved_gfx_map, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MODE_ADDR, VGA_GFX_ADDR_REG);
  outb (saved_gfx_mode, VGA_GFX_DATA_REG);
  outb (VGA_GFX_MISC_ADDR, VGA_GFX_ADDR_REG);
  outb (saved_gfx_misc, VGA_GFX_DATA_REG);
}


/* Set FONT_BUFFER_SUPP to FONT_BUFFER if the font is small.  */
void
vga_select_font_buffer (int font_buffer, int font_buffer_supp)
{
  char font = ((font_buffer & 6) >> 1)  | ((font_buffer & 1) << 4)
    | ((font_buffer_supp & 6) << 1) | ((font_buffer_supp & 1) << 5);

  outb (VGA_SEQ_FONT_ADDR, VGA_SEQ_ADDR_REG);
  outb (font, VGA_SEQ_DATA_REG);
}

/* Set the font height in pixel.  */
void
vga_set_font_height (int height)
{
  char saved;

  outb (VGA_CRT_MAX_SCAN_LINE, VGA_CRT_ADDR_REG);
  saved = inb (VGA_CRT_DATA_REG);
  saved &= ~31;
  saved |= (height - 1) & 31;
  outb (saved, VGA_CRT_DATA_REG);
}


/* Set the font height in pixel.  WIDTH can be 8 or 9.  */
void
vga_set_font_width (int width)
{
  char saved;

  if (width != 8 && width != 9)
    return;

  outb (VGA_SEQ_CLOCK_MODE_ADDR, VGA_SEQ_ADDR_REG);
  saved = inb (VGA_SEQ_DATA_REG);
  if (width == 8)
    saved |= VGA_SEQ_CLOCK_MODE_8;
  else
    saved &= ~VGA_SEQ_CLOCK_MODE_8;
  outb (saved, VGA_SEQ_DATA_REG);
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


/* Set cursor size from START to END (set to -1 to not set one of the
   values).  */
void
vga_set_cursor_size (int start, int end)
{
  char saved;

  if (start >= 0)
    {
      outb (VGA_CRT_CURSOR_START, VGA_CRT_ADDR_REG);
      saved = inb (VGA_CRT_DATA_REG);
      saved &= ~31;
      saved |= start & 31;
      outb (saved, VGA_CRT_DATA_REG);
    }
  if (end >= 0)
    {
      outb (VGA_CRT_CURSOR_END, VGA_CRT_ADDR_REG);
      saved = inb (VGA_CRT_DATA_REG);
      saved &= ~31;
      saved |= end & 31;
      outb (saved, VGA_CRT_DATA_REG);
    }
}


/* Set the cursor position to POS, which is (x_pos + y_pos * width).  */
void
vga_set_cursor_pos (unsigned int pos)
{
  outb (VGA_CRT_CURSOR_HIGH, VGA_CRT_ADDR_REG);
  outb (pos >> 8, VGA_CRT_DATA_REG);
  outb (VGA_CRT_CURSOR_LOW, VGA_CRT_ADDR_REG);
  outb (pos & 0xff, VGA_CRT_DATA_REG);
}


/* Read NR entries from the color palette, starting from INDEX.  DATA
   must be able to hold at least 3 * NR bytes and will contain the
   desired colors in RGB form.  Only the lower six bits of each
   component are significant.  */
void
vga_read_palette (unsigned char index, unsigned char *data, int nr)
{
  /* Every color has three components.  */
  nr *= 3;

  outb (index, VGA_DAC_READ_ADDR_REG);
  while (nr--)
    *(data++) = inb (VGA_DAC_DATA_REG);
}


/* Write NR entries to the color palette, starting from INDEX.  DATA
   must be at least 3 * NR of bytes long and contains the desired
   colors in RGB form.  Only the lower six bits for each component are
   significant.  */
void
vga_write_palette (unsigned char index, const unsigned char *data, int nr)
{
  /* Every color has three components.  */
  nr *= 3;

  outb (index, VGA_DAC_WRITE_ADDR_REG);
  while (nr--)
    outb (*(data++), VGA_DAC_DATA_REG);
}


/* Exchange NR entries in the internal palette with the values in
   PALETTE_ATTR, starting from the internal palette entry INDEX (which
   can be betweern 0 and 15).  Only the lower six bits of each entry
   in PALETTE_ATTR is significant.

   The internal palette entry is used to look up a color in the
   palette for the color attribute in text mode with the corresponding
   value.

   Example: The attribute byte specifies 3 as the foreground color.
   This means that the character with this attribute gets the color of
   the palette entry specified by the internal palette entry 3 (the
   fourth one).  */
void
vga_exchange_palette_attributes (unsigned char index,
				 unsigned char *palette_attr,
				 int nr)
{
  if (!nr)
    return;

  /* We want to read and change the palette attribute register.  */
  while (nr--)
    {
      unsigned char attr;

      /* Side effect of reading the input status #1 register is to
	 reset the attribute mixed address/data register so that the
	 next write it expects is the address, not the data.  */
      inb (VGA_INPUT_STATUS_1_REG);

      /* Set the address.  */
      outb (index++, VGA_ATTR_ADDR_DATA_REG);
      attr = inb (VGA_ATTR_DATA_READ_REG);
      outb ((attr & ~077) | (*palette_attr & 077), VGA_ATTR_ADDR_DATA_REG);
      *(palette_attr++) = attr & 077;
    }

  /* Re-enable the screen, which was blanked during the palette
     operation.  */
  inb (VGA_INPUT_STATUS_1_REG);
  outb (0x20, VGA_ATTR_ADDR_DATA_REG);
}
