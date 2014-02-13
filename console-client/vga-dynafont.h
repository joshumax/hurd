/* vga-dynafont.h - Interface to the dynamic font handling for VGA cards.
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

#ifndef _VGA_DYNAFONT_H_
#define _VGA_DYNAFONT_H_ 1

#include <wchar.h>

#include "bdf.h"


/* The dynafont interface does not do locking on its own, for maximum
   efficiency it relies on locking by the caller (because usually the
   caller has some other data structures to lock along with the
   dynafont.  However, it is safe to call two functions on different
   dynafonts asynchronously.  */
typedef struct dynafont *dynafont_t;

/* The representation for the unknown glyph is always at the same
   location.  */
#define FONT_INDEX_UNKNOWN 0

/* Create a new dynafont object, which uses glyphs from the font FONT
   (which must be 8 pixels wide and up to 32 pixels heigh).  SIZE is
   either 256 or 512, and specifies the number of available glyphs in
   the font cache.  The object is returned in DYNAFONT.  The caller
   must ensure the integrity of FONT until the object is destroyed or
   the font is changed with dynafont_change_font.  */
error_t dynafont_new (bdf_font_t font, bdf_font_t font_italic,
		      bdf_font_t font_bold, bdf_font_t font_bold_italic,
		      int size, int width, dynafont_t *dynafont);

/* Release a dynafont object and its associated resources.  */ 
void dynafont_free (dynafont_t df);

/* Look up the vga font index for an UCS-4 character.  If not already
   mapped, try to find space for a new entry and add the mapping.
   Acquires an additional reference to the character.  Might return
   the glyph for the unrepresentable character if the glyph is ot
   available for this character or no free slot is available right
   now.  In the former case, some information gets lost (to do
   otherwise, one would have to either assign one of the scarce font
   indices to each undisplayable character value on screen, or to
   store the whole scrollback buffer as wide chars as well to recover
   the lost info from that copy of the original text.  */
int dynafont_lookup (dynafont_t df, conchar_t *chr);

/* Release a reference to the glyph VGA_FONT_INDEX in dynafont DF.  */
void dynafont_release (dynafont_t df, int vga_font_index);

/* Load the VGA font to the card and make it active.  */
void dynafont_activate (dynafont_t df);

/* Set the cursor to normal if STANDOUT is zero, or to a block cursor
   otherwise.  */
void dynafont_set_cursor (dynafont_t df, int standout);

/* Change the font used by dynafont DF to FONT.  This transformation
   is initially loss-less, if the new font can't display some
   characters on the screen, you can always go back to a font that
   does and get the glyphs for those characters back.  (However, the
   comments in dynafont_lookup hold for glyphs looked up after the font
   change.)  */
void dynafont_change_font (dynafont_t df, bdf_font_t font);

#endif	/* _VGA_DYNAFONT_H_ */
