/* vga-dynacolor.h - Interface to dynamic color handling for VGA cards.
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

#ifndef _VGA_DYNACOLOR_H_
#define _VGA_DYNACOLOR_H_ 1

/* Every component can have up to 6 bits.  */
#define DYNACOLOR_COMPONENT_MAX	0x63

/* The components are, in that order, red, green, blue.  */
#define DYNACOLOR_COMPONENTS	3

/* The maximum number of colors.  */
#define DYNACOLOR_COLORS	8

struct dynacolor
{
  int ref[8];
  /* Reverse lookup, -1 denotes an unmapped color.  */
  signed char col[16];
};

typedef struct dynacolor dynacolor_t;

/* We start with one reference for the black color so that it is
   always available.  It is also put in front so that it can be shared
   as the border color.  The importance of this is that for
   non-standard font heights, we have some black-filled normal text
   rows below the screen matrix, for which we must allocate a real
   color slot :(  */
#define DYNACOLOR_INIT_8	{ { 1, 0, 0, 0, 0, 0, 0, 0 }, \
 { 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } }
#define DYNACOLOR_INIT_16	{ { -1 } }

extern dynacolor_t dynacolor_init_8;
extern dynacolor_t dynacolor_init_16;


/* Initialize the dynacolor subsystem.  */
void dynacolor_init (void);

/* Restore the original palette.  */
void dynacolor_fini (void);

/* Activate the dynamic color palette DC.  */
void dynacolor_activate (dynacolor_t *dc);

/* Try to allocate a slot for the color COL in the dynamic color
   palette DC.  Return the allocated slot number (with one reference)
   or -1 if no slot is available.  */
signed char dynacolor_allocate (dynacolor_t *dc, unsigned char col);

/* Get the slot number for color C in the dynamic color palette DC, or
   -1 if we ran out of color slots.  If successful, this allocates a
   reference for the color.  */
#define dynacolor_lookup(dc,c)						\
  ((dc).ref[0] < 0 ? (c) :						\
   ((dc).col[(c)] >= 0 ? (dc).ref[(dc).col[(c)]]++, (dc).col[(c)] :	\
    dynacolor_allocate (&(dc), (c))))

/* Add a reference to palette entry P in the dynamic font DC.  */
#define dynacolor_add_ref(dc,p)						\
  do {                                                                  \
      if ((dc).ref[0] >= 0)                                             \
        (dc).ref[p]++;                                                  \
  } while (0)

/* Deallocate a reference for palette entry P in the dynamic font DC.  */
#define dynacolor_release(dc,p)						\
  do {                                                                  \
      if ((dc).ref[0] >= 0)                                             \
        (dc).ref[p]--;                                                  \
  } while (0)

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
void dynacolor_replace_colors (dynacolor_t *dc,
			       signed char fgcol, signed char bgcol,
			       signed char *r_fgcol, signed char *r_bgcol);

#endif	/* _VGA_DYNACOLOR_H_ */
