/* vga-dynacolor.c - Dynamic color handling for VGA cards.
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

#include <assert-backtrace.h>

#include <hurd/console.h>

#include "vga-hw.h"
#include "vga-support.h"
#include "vga-dynacolor.h"


dynacolor_t dynacolor_init_8 = DYNACOLOR_INIT_8;
dynacolor_t dynacolor_init_16 = DYNACOLOR_INIT_16;

static const unsigned char std_palette[16][DYNACOLOR_COMPONENTS] =
  {
    {  0,  0,  0 },	/* Black.  */
    { 42,  0,  0 },	/* Red.  */
    {  0, 42,  0 },	/* Green.  */
    { 42, 21,  0 },	/* Brown.  */
    {  0,  0, 42 },	/* Blue.  */
    { 42,  0, 42 },	/* Magenta.  */
    {  0, 42, 42 },	/* Cyan.  */
    { 42, 42, 42 },	/* White.  */
    { 21, 21, 21 },	/* Bright Black.  */
    { 63, 21, 21 },	/* Bright Red.  */
    { 21, 63, 21 },	/* Bright Green.  */
    { 63, 63, 21 },	/* Bright Yellow.  */
    { 21, 21, 63 },	/* Bright Blue.  */
    { 63, 21, 63 },	/* Bright Magenta.  */
    { 21, 63, 63 },	/* Bright Cyan.  */
    { 63, 63, 63 }	/* Bright White.  */
  };

/* The currently active (visible) dynafont.  */
static dynacolor_t *active_dynacolor;

static unsigned char saved_palette[16][DYNACOLOR_COMPONENTS];

/* We initialize this to the desired mapping for
   vga_exchange_palette_attributes.  */
/* Palette index 0 is left as it is, for the border color.  */
static unsigned char saved_palette_attr[16] =
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };


/* Initialize the dynacolor subsystem.  */
void
dynacolor_init (void)
{
  /* Palette index 0 is left as it is, as it is also used for the
     border color by default.  */
  vga_exchange_palette_attributes (0, saved_palette_attr, 16);
  vga_read_palette (0, saved_palette[0], 16);

  vga_write_palette (0, std_palette[0], 16);
}


/* Restore the original palette.  */
void
dynacolor_fini (void)
{
  vga_write_palette (0, saved_palette[0], 16);
  vga_exchange_palette_attributes (0, saved_palette_attr, 16);
}


/* Activate the dynamic color palette DC.  */
void
dynacolor_activate (dynacolor_t *dc)
{
  if (dc == active_dynacolor)
    return;
  
  if (dc->ref[0] < 0 && (!active_dynacolor || active_dynacolor->ref[0] >= 0))
    {
      /* Switching from dynamic to static palette.  */
      vga_write_palette (0, std_palette[0], 16);
    }
  else if (dc->ref[0] >= 0
	   && (!active_dynacolor || active_dynacolor->ref[0] < 0))
    {
      /* Switching from static to dynamic palette.  */
      int i;
      for (i = 0; i < 16; i++)
	if (dc->col[i] >= 0)
	  {
	    vga_write_palette (dc->col[i], std_palette[i], 1);
	    vga_write_palette (8 + dc->col[i], std_palette[i], 1);
	  }
    }
  active_dynacolor = dc;
}


/* Try to allocate a slot for the color COL in the dynamic color
   palette DC.  Return the allocated slot number or -1 if no slot is
   available.  */
signed char
dynacolor_allocate (dynacolor_t *dc, unsigned char col)
{
  int i;
  
  for (i = 0; i < 8; i++)
    if (dc->ref[i] == 0)
      {
	/* We want to reuse slot i.  Clear the old user.  */
	int j;

	for (j = 0; j < 16; j++)
	  if (dc->col[j] == i)
	    {
	      dc->col[j] = -1;
	      break;
	    }

	dc->ref[i] = 1;
	dc->col[col] = i;
	if (active_dynacolor == dc)
	  {
	    vga_write_palette (0 + i, std_palette[col], 1);
	    vga_write_palette (8 + i, std_palette[col], 1);
	  }
	return i;
      }
  return -1;
}


/* This is a convenience function that looks up a replacement color
   pair if the original colors are not available.  The function always
   succeeds.  DC is the dynacolor to use for allocation, FGCOL and
   BGCOL are the desired colors, and R_FGCOL and R_BGCOL are the
   resulting colors.  The function assumes that the caller already
   tried to look up the desired colors before trying to replace them,
   and that the result of the lookup is contained in R_FGCOL and
   R_BGCOL.

   Example:

   res_bgcol = dynacolor_lookup (&dc, bgcol);
   res_fgcol = dynacolor_lookup (&dc, fgcol);
   if (res_bgcol == -1 || res_fgcol == -1)
     dynacolor_replace_colors (&dc, fgcol, bgcol, &res_fgcol, &res_bgcol);

   After the above code, res_fgcol and res_bgcol contain valid color
   values.  */
void
dynacolor_replace_colors (dynacolor_t *dc,
			  signed char fgcol, signed char bgcol,
			  signed char *r_fgcol, signed char *r_bgcol)
{
  /* Replacement colors.  As we have 8 colors in our palette, and
     one was already tried, we only need to try out 8 possible
     replacements.  Only the first seven can fail.  But one is
     possibly taken by the fore-/background color, so we actually
     have to try up to nine.  XXX Maybe we should have a table
     based on pairs, but that increases the number of cases a
     lot. */
  /* Note that no color must occur twice in one replacement list,
     and that the color to be replaced must not occur either.  */
  static signed char pref[16][9] =
    {
      /* Replacements for CONS_COLOR_BLACK.  */
      { CONS_COLOR_BLACK | (1 << 3), CONS_COLOR_BLUE,
	CONS_COLOR_YELLOW, CONS_COLOR_RED, CONS_COLOR_MAGENTA,
	CONS_COLOR_GREEN, CONS_COLOR_CYAN, CONS_COLOR_WHITE,
	CONS_COLOR_BLUE | (1 << 3) },
      /* Replacements for CONS_COLOR_RED.  */
      { CONS_COLOR_RED | (1 << 3), CONS_COLOR_YELLOW,
	CONS_COLOR_MAGENTA, CONS_COLOR_BLUE, CONS_COLOR_CYAN,
	CONS_COLOR_GREEN, CONS_COLOR_WHITE, CONS_COLOR_BLACK,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_GREEN.  */
      { CONS_COLOR_GREEN | (1 << 3), CONS_COLOR_CYAN,
	CONS_COLOR_YELLOW, CONS_COLOR_BLUE, CONS_COLOR_RED,
	CONS_COLOR_MAGENTA, CONS_COLOR_WHITE, CONS_COLOR_BLACK,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_YELLOW.  */
      { CONS_COLOR_YELLOW | (1 << 3), CONS_COLOR_RED,
	CONS_COLOR_GREEN, CONS_COLOR_MAGENTA, CONS_COLOR_BLUE,
	CONS_COLOR_CYAN, CONS_COLOR_WHITE, CONS_COLOR_BLACK,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_BLUE.  */
      { CONS_COLOR_BLUE | (1 << 3), CONS_COLOR_CYAN,
	CONS_COLOR_MAGENTA, CONS_COLOR_RED, CONS_COLOR_GREEN,
	CONS_COLOR_YELLOW, CONS_COLOR_WHITE, CONS_COLOR_BLACK,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_MAGENTA.  */
      { CONS_COLOR_MAGENTA | (1 << 3), CONS_COLOR_RED,
	CONS_COLOR_BLUE, CONS_COLOR_YELLOW, CONS_COLOR_CYAN,
	CONS_COLOR_BLUE, CONS_COLOR_WHITE, CONS_COLOR_BLACK,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_CYAN.  */
      { CONS_COLOR_CYAN | (1 << 3), CONS_COLOR_BLUE,
	CONS_COLOR_MAGENTA, CONS_COLOR_GREEN, CONS_COLOR_RED,
	CONS_COLOR_YELLOW, CONS_COLOR_WHITE, CONS_COLOR_BLACK,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_WHITE.  */
      { CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_CYAN,
	CONS_COLOR_GREEN, CONS_COLOR_YELLOW, CONS_COLOR_MAGENTA,
	CONS_COLOR_RED, CONS_COLOR_BLUE, CONS_COLOR_CYAN | (1 << 3),
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_BLACK | (1 << 3).  */
      { CONS_COLOR_BLACK, CONS_COLOR_BLUE | (1 << 3),
	CONS_COLOR_YELLOW | (1 << 3), CONS_COLOR_RED | (1 << 3),
	CONS_COLOR_MAGENTA | (1 << 3), CONS_COLOR_GREEN | (1 << 3),
	CONS_COLOR_CYAN | (1 << 3), CONS_COLOR_WHITE | (1 << 3),
	CONS_COLOR_WHITE },
      /* Replacements for CONS_COLOR_RED | (1 << 3).  */
      { CONS_COLOR_RED, CONS_COLOR_YELLOW | (1 << 3),
	CONS_COLOR_MAGENTA | (1 << 3), CONS_COLOR_BLUE | (1 << 3),
	CONS_COLOR_CYAN | (1 << 3), CONS_COLOR_GREEN | (1 << 3),
	CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_WHITE,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_GREEN | (1 << 3).  */
      { CONS_COLOR_GREEN, CONS_COLOR_CYAN | (1 << 3),
	CONS_COLOR_YELLOW | (1 << 3), CONS_COLOR_BLUE | (1 << 3),
	CONS_COLOR_RED | (1 << 3), CONS_COLOR_MAGENTA | (1 << 3),
	CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_WHITE,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_YELLOW | (1 << 3).  */
      { CONS_COLOR_YELLOW, CONS_COLOR_RED | (1 << 3),
	CONS_COLOR_GREEN | (1 << 3), CONS_COLOR_MAGENTA | (1 << 3),
	CONS_COLOR_BLUE | (1 << 3), CONS_COLOR_CYAN | (1 << 3),
	CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_WHITE,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_BLUE | (1 << 3).  */
      { CONS_COLOR_BLUE, CONS_COLOR_CYAN | (1 << 3),
	CONS_COLOR_MAGENTA | (1 << 3), CONS_COLOR_RED | (1 << 3),
	CONS_COLOR_GREEN | (1 << 3), CONS_COLOR_YELLOW | (1 << 3),
	CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_WHITE,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_MAGENTA | (1 << 3).  */
      { CONS_COLOR_MAGENTA, CONS_COLOR_RED | (1 << 3),
	CONS_COLOR_BLUE | (1 << 3), CONS_COLOR_YELLOW | (1 << 3),
	CONS_COLOR_CYAN | (1 << 3), CONS_COLOR_GREEN | (1 << 3),
	CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_WHITE,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_CYAN | (1 << 3).  */
      { CONS_COLOR_CYAN, CONS_COLOR_BLUE | (1 << 3),
	CONS_COLOR_MAGENTA | (1 << 3), CONS_COLOR_GREEN | (1 << 3),
	CONS_COLOR_RED | (1 << 3), CONS_COLOR_YELLOW | (1 << 3),
	CONS_COLOR_WHITE | (1 << 3), CONS_COLOR_WHITE,
	CONS_COLOR_BLACK | (1 << 3) },
      /* Replacements for CONS_COLOR_WHITE | (1 << 3).  */
      { CONS_COLOR_WHITE, CONS_COLOR_CYAN | (1 << 3),
	CONS_COLOR_GREEN | (1 << 3), CONS_COLOR_YELLOW | (1 << 3),
	CONS_COLOR_MAGENTA | (1 << 3), CONS_COLOR_RED | (1 << 3),
	CONS_COLOR_BLUE | (1 << 3), CONS_COLOR_CYAN,
	CONS_COLOR_BLACK | (1 << 3) },
    };

  signed char res_fgcol = *r_fgcol;
  signed char res_bgcol = *r_bgcol;
  signed char new_bgcol = bgcol;
  int i;

  /* First get a background color.  */
  if (res_bgcol == -1)
    {
      for (i = 0; i < 9; i++)
	{
	  /* If the foreground color is found, make sure to not
	     set the background to it.  */
	  if (res_fgcol == -1 || pref[bgcol][i] != fgcol)
	    {
	      res_bgcol = dynacolor_lookup (*dc, pref[bgcol][i]);
	      if (res_bgcol >= 0)
		break;
	    }
	}

      assert_backtrace (res_bgcol >= 0);
      new_bgcol = pref[bgcol][i];
    }

  if (fgcol == bgcol)
    {
      assert_backtrace (res_fgcol == -1);
      /* Acquire another reference.  */
      res_fgcol = dynacolor_lookup (*dc, new_bgcol);
    }
  else
    assert_backtrace (res_fgcol != res_bgcol);
      
  if (res_fgcol == -1)
    {
      /* Now find a foreground color.  */
      for (i = 0; i < 9; i++)
	{
	  if (pref[fgcol][i] != new_bgcol)
	    {
	      res_fgcol = dynacolor_lookup (*dc, pref[fgcol][i]);
	      if (res_fgcol >= 0)
		break;
	    }
	}
      assert_backtrace (res_fgcol >= 0);
    }
  *r_fgcol = res_fgcol;
  *r_bgcol = res_bgcol;
}
