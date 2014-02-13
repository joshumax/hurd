/* vga-hw.h - Definitions for the VGA hardware.
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

#ifndef _VGA_HW_H_
#define _VGA_HW_H_ 1

#define VGA_VIDEO_MEM_BASE_ADDR	0x0b8000
#define VGA_VIDEO_MEM_LENGTH	0x004000

#define VGA_FONT_BUFFER		8
#define VGA_FONT_SIZE		256
#define VGA_FONT_HEIGHT		32
#define VGA_FONT_LGC_BEGIN	0xc0
#define VGA_FONT_LGC_COUNT	32

#define VGA_MIN_REG		0x3c0
#define VGA_MAX_REG		0x3df

/* The sequencer address register selects the sub-register of the
   sequencer that is accessed through the sequencer data register.  */
#define VGA_SEQ_ADDR_REG	0x3c4
#define VGA_SEQ_DATA_REG	0x3c5

/* The reset subregister can be used to asynchronously or
   synchronously halt or clear the sequencer.  */
#define VGA_SEQ_RESET_ADDR	0x00
#define VGA_SEQ_RESET_ASYNC	0x10	/* Can cause loss of video data.  */
#define VGA_SEQ_RESET_SYNC	0x01
#define VGA_SEQ_RESET_CLEAR	0x11	/* Sequencer can operate.  */

/* The clocking mode subregister.  */
#define VGA_SEQ_CLOCK_MODE_ADDR	0x01
#define VGA_SEQ_CLOCK_MODE_8	0x01	/* 8-pixel width for fonts.  */

/* The map subregister specifies which planes are written to.  */
#define VGA_SEQ_MAP_ADDR	0x02
#define VGA_SEQ_MAP_PLANE0	0x01
#define VGA_SEQ_MAP_PLANE1	0x02
#define VGA_SEQ_MAP_PLANE2	0x04
#define VGA_SEQ_MAP_PLANE3	0x08

/* The font subregister.  */
#define VGA_SEQ_FONT_ADDR	0x03

/* The memory mode subregister specifies the way that memory is
   accessed.  */
#define VGA_SEQ_MODE_ADDR	0x04
#define VGA_SEQ_MODE_EXT	0x02	/* Access 265kB rather than 64kB.  */
#define VGA_SEQ_MODE_SEQUENTIAL	0x04	/* Sequential, not odd/even addr.  */
#define VGA_SEQ_MODE_CHAIN4	0x08	/* Chain 4 addressing.  */


/* The graphics address register selects the sub-register that is
   accessed through the graphics data register.  */
#define VGA_GFX_ADDR_REG	0x3ce
#define VGA_GFX_DATA_REG	0x3cf

/* The map subregister selects the plane to read from.  */
#define VGA_GFX_MAP_ADDR	0x04
#define VGA_GFX_MAP_PLANE0	0x00
#define VGA_GFX_MAP_PLANE1	0x01
#define VGA_GFX_MAP_PLANE2	0x02
#define VGA_GFX_MAP_PLANE3	0x03

/* The mode subregister selects the memory access mode.  */
#define VGA_GFX_MODE_ADDR	0x05
#define VGA_GFX_MODE_SHIFT256	0x40
#define VGA_GFX_MODE_SHIFT	0x20
#define VGA_GFX_MODE_HOSTOE	0x10
#define VGA_GFX_MODE_READ0	0x00
#define VGA_GFX_MODE_READ1	0x08
#define VGA_GFX_MODE_WRITE0	0x00
#define VGA_GFX_MODE_WRITE1	0x01
#define VGA_GFX_MODE_WRITE2	0x02
#define VGA_GFX_MODE_WRITE3	0x03

/* The miscellaneous subregister.  */
#define VGA_GFX_MISC_ADDR	0x06
#define VGA_GFX_MISC_GFX	0x01	/* Switch on graphics mode.  */
#define VGA_GFX_MISC_CHAINOE	0x02
#define VGA_GFX_MISC_A0TOBF	0x00
#define VGA_GFX_MISC_A0TOAF	0x04
#define VGA_GFX_MISC_B0TOB7	0x08
#define VGA_GFX_MISC_B8TOBF	0x0c


/* The CRTC Registers.  XXX Depends on the I/O Address Select field.
   However, the only need to use the other values is for compatibility
   with monochrome adapters.  */
#define VGA_CRT_ADDR_REG	0x3d4
#define VGA_CRT_DATA_REG	0x3d5

/* The maximum scan line subregister.  */
#define VGA_CRT_MAX_SCAN_LINE	0x09

/* The cursor start subregister.  */
#define VGA_CRT_CURSOR_START	0x0a
#define VGA_CRT_CURSOR_DISABLE	0x20

/* The cursor end subregister.  */
#define VGA_CRT_CURSOR_END	0x0b

/* The cursor position subregisters.  */
#define VGA_CRT_CURSOR_HIGH	0x0e
#define VGA_CRT_CURSOR_LOW	0x0f


/* The DAC Registers.  */
#define VGA_DAC_WRITE_ADDR_REG	0x3c8
#define VGA_DAC_READ_ADDR_REG	0x3c7
#define VGA_DAC_DATA_REG	0x3c9


/* The Attribute Registers.  */
#define VGA_ATTR_ADDR_DATA_REG	0x3c0
#define VGA_ATTR_DATA_READ_REG	0x3c1

/* The Attribute Mode Control subregister.  */
#define VGA_ATTR_MODE_ADDR	0x10
#define VGA_ATTR_MODE_LGE	0x04

#define VGA_ATTR_ENABLE_ADDR	0x20

/* Other junk.  */
#define VGA_INPUT_STATUS_1_REG	0x3da

#endif	/* _VGA_HW_H_ */
