/* dynafont.c - Dynamic font handling.
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

#include <assert.h>
#include <malloc.h>
#include <wchar.h>
#include <hurd/ihash.h>
#include <string.h>

#include "vga-hw.h"
#include "vga.h"
#include "bdf.h"
#include "dynafont.h"


/* The currently active (visible) dynafont.  */
static dynafont_t active_dynafont;

/* One glyph in a VGA font is 8 pixels wide and 32 pixels high.  Only
   the first N lines are visible, and N depends on the VGA register
   settings.  */
typedef char vga_font_glyph[VGA_FONT_HEIGHT];

/* For each glyph in the VGA font, one instance of this structure is
   held in the dynafont.  */
struct mapped_character
{
  /* Reference count of the mapped character.  If this drops zero, we
     can remove the mapping at any time.  */
  int refs;

  /* Remember the character this glyph belongs to, so the glyph can be
     reloaded when the font is changed.  */
  wchar_t character;

  /* Used by libihash for fast removal of elements.  */
  void **locp;
};


struct dynafont
{
  /* The sorted font to take the glyphs from.  */
  bdf_font_t font;

  /* The size of the VGA font (256 or 512).  Must be a power of 2!  */
  int size;

  /* A hash containing a pointer to a struct mapped_character for each
     UCS-4 character we map to a VGA font index.  */
  ihash_t charmap;

  /* The struct mapped_characters are preallocated for all vga font
     index values.  This points to an array of SIZE such elements.  */
  struct mapped_character *charmap_data;

  /* The last vga font index that had been free (or could be reused).  */
  int vga_font_last_free_index;

  /* The number of free slots in the VGA font.  */
  int vga_font_free_indices;

  /* The font memory as stored on the card.  */
  vga_font_glyph *vga_font;
};


/* Create a new dynafont object, which uses glyphs from the font FONT
   (which must be 8 pixels wide and up to 32 pixels heigh).  SIZE is
   either 256 or 512, and specifies the number of available glyphs in
   the font cache.  The object is returned in DYNAFONT.  The caller
   must ensure the integrity of FONT until the object is destroyed or
   the font is changed with dynafont_change_font.  */
error_t
dynafont_new (bdf_font_t font, int size, dynafont_t *dynafont)
{
  error_t err = 0;
  dynafont_t df;
  struct bdf_glyph *glyph = NULL;

  df = malloc (sizeof *df);
  if (!df)
    return ENOMEM;

  df->font = font;
  df->size = size;
  err = ihash_create (&df->charmap);
  if (err)
    {
      free (df);
      return err;
    }

  df->charmap_data = calloc (size, sizeof (struct mapped_character));
  if (!df->charmap_data)
    {
      ihash_free (df->charmap);
      free (df);
      return ENOMEM;
    }

  df->vga_font = malloc (sizeof (vga_font_glyph) * size);
  if (!df->vga_font)
    {
      ihash_free (df->charmap);
      free (df->charmap_data);
      free (df);
      return ENOMEM;
    }
  df->vga_font_free_indices = df->size;
  df->vga_font_last_free_index = 0;

  /* The encoding of the unknown glyph.  Some fonts provide an empty
     box for this encoding.  Undefine this if you always want to use
     the built-in font.  XXX Make this a command line option.  */
#define ENCODING_UNKNOWN 0
  /* The encoding of the space.  All fonts should provide it.  */
#define ENCODING_SPACE 32

  {
    struct mapped_character *chr = &df->charmap_data[FONT_INDEX_UNKNOWN];
    df->vga_font_free_indices--;
    chr->refs = 1;

#if defined(ENCODING_UNKNOWN)
    glyph = bdf_find_glyph (df->font, ENCODING_UNKNOWN, 0);
    if (!glyph)
      glyph = bdf_find_glyph (df->font, -1, ENCODING_UNKNOWN);
#endif
    if (glyph)
      {
	/* XXX Take glyph size into account.  */
	memcpy (df->vga_font[FONT_INDEX_UNKNOWN], glyph->bitmap, 32);
	/* Update the hash table.  */
	ihash_add (df->charmap, ENCODING_UNKNOWN, chr, &chr->locp);
      }
    else
      {
	int i;
	char *gl = df->vga_font[FONT_INDEX_UNKNOWN];
	/* XXX Take font height into account.  */
	gl[0] = 0xFF;	/* ******** */
	gl[1] = 0xC3;	/* **    ** */
	gl[2] = 0x99;	/* *  **  * */
	gl[3] = 0x99;	/* *  **  * */
	gl[4] = 0xF9;	/* *****  * */
	gl[5] = 0xF3;	/* ****  ** */
	gl[6] = 0xF3;	/* ****  ** */
	gl[7] = 0xE7;	/* ***  *** */
	gl[8] = 0xE7;	/* ***  *** */
	gl[9] = 0xFF;	/* ******** */
	gl[10] = 0xE7;	/* ***  *** */
	gl[11] = 0xFF;	/* ******** */
	for (i = 12; i < 32; i++)
	  gl[i] = 0;
      }

#if defined(ENCODING_SPACE)
    glyph = bdf_find_glyph (df->font, ENCODING_SPACE, 0);
    if (!glyph)
      glyph = bdf_find_glyph (df->font, -1, ENCODING_SPACE);
#endif
    if (glyph)
      {
	/* XXX Take glyph size into account.  */
	memcpy (df->vga_font[FONT_INDEX_SPACE], glyph->bitmap, 32);
	/* Update the hash table.  */
	ihash_add (df->charmap, ENCODING_SPACE, chr, &chr->locp);
      }
    else
      {
	int i;
	char *gl = df->vga_font[FONT_INDEX_SPACE];
	for (i = 0; i < 32; i++)
	  gl[i] = 0;
      }
  }
  *dynafont = df;
  return err;
}


/* Release a dynafont object and its associated resources.  */
void
dynafont_free (dynafont_t df)
{
  ihash_free (df->charmap);
  free (df->charmap_data);
  free (df->vga_font);
  free (df);
}


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
int
dynafont_lookup (dynafont_t df, wchar_t wide_chr)
{
  struct mapped_character *chr = ihash_find (df->charmap, (int) wide_chr);

  if (chr)
    {
      if (!chr->refs++)
	df->vga_font_free_indices--;
      return (chr - df->charmap_data) / sizeof (struct mapped_character);
    }

  /* The character is not currently mapped.  Look for an empty
     slot and add it.  */
  if (df->vga_font_free_indices)
    {
      int pos = (df->vga_font_last_free_index + 1) % df->size;
      struct bdf_glyph *glyph;

      glyph = bdf_find_glyph (df->font, (int) wide_chr, 0);
      if (!glyph)
	glyph = bdf_find_glyph (df->font, -1, (int) wide_chr);
      if (glyph)
	{
	  while (pos != df->vga_font_last_free_index)
	    {
	      if (df->charmap_data[pos].refs == 0)
		{
		  /* Ok, we found a new entry, use it.  */
		  df->vga_font_last_free_index = pos;
		  chr = &df->charmap_data[pos];
		  chr->refs = 1;
		  memcpy (df->vga_font[pos], glyph->bitmap, VGA_FONT_HEIGHT);
		  /* XXX This will have to be updated when fonts are
		     cached.  */
		  if (active_dynafont == df)
		    vga_write_font_buffer (0, pos, (char *) glyph->bitmap,
					   VGA_FONT_HEIGHT);
		  /* Update the hash table.  */
		  if (chr->locp)
		    ihash_locp_remove (df->charmap, chr->locp);
		  ihash_add (df->charmap, (int) wide_chr, chr, &chr->locp);
		  return pos;
		}
	      pos = (pos + 1) % df->size;
	    }
	  /* Should never be reached.  */
	  assert (!"No free vga font index.");
	}
    }

  df->charmap_data[FONT_INDEX_UNKNOWN].refs++;
  return FONT_INDEX_UNKNOWN;
}


/* Release a reference to the glyph VGA_FONT_INDEX in dynafont DF.  */
void
dynafont_release (dynafont_t df, int vga_font_index)
{
  if (! --df->charmap_data[vga_font_index].refs)
    df->vga_font_free_indices++;
}


/* Load the VGA font to the card and make it active.  */
void
dynafont_activate (dynafont_t df)
{
  /* XXX Replace this with some caching method.  We have eight font
     slots available.  */
  vga_write_font_buffer (0, 0, (char *) df->vga_font,
			 df->size * VGA_FONT_HEIGHT);
  vga_select_font_buffer (0, (df->size == 512) ? 1 : 0);
  active_dynafont = df;
}


/* Change the font used by dynafont DF to FONT.  This transformation
   is initially loss-less, if the new font can't display some
   characters on the screen, you can always go back to a font that
   does and get the glyphs for those characters back.  (However, the
   comments in dynafont_lookup hold for glyphs looked up after the font
   change.)  */
void
dynafont_change_font (dynafont_t df, bdf_font_t font)
{
  int i;

  df->font = font;
  for (i = 0; i < df->size; i++)
    {
      /* If we don't derive the unknown or space glyph from the font,
	 we don't update it.  */
#ifndef ENCODING_UNKNOWN
      if (i == FONT_INDEX_UNKNOWN)
	continue;
#endif
#ifndef ENCODING_SPACE
      if (i == FONT_INDEX_SPACE)
	continue;
#endif
      if (! df->charmap_data[i].refs)
	{
	  /* The glyph is not used.  If it is mapped, we need to
	     remove the mapping to invalidate the glyph.  */
	  if (df->charmap_data[i].locp)
	    ihash_locp_remove (df->charmap, df->charmap_data[i].locp);
	}

      else
	{
	  /* The glyph is mapped and in use.  We will not destroy the
	     mapping, but just override the glyph in the VGA font.
	     This way the user can recover from loading a bad font by
	     going back to a better one.  */
	  struct bdf_glyph *glyph;
	  glyph = bdf_find_glyph (df->font,
				  (int) df->charmap_data[i].character, 0);
	  if (!glyph)
	    glyph = bdf_find_glyph (df->font, -1,
				    (int) df->charmap_data[i].character);
	  if (!glyph)
	    {
#ifdef ENCODING_UNKNOWN
	      /* The new font doesn't have a representation for the
		 unknown glyph at the desired place, so keep the old
		 one.  XXX Should load the built-in one here.  */
	      if (i == FONT_INDEX_UNKNOWN)
		continue;
#endif
#ifdef ENCODING_SPACE
	      /* The new font doesn't have a representation for the
		 blank glyph at the desired place, so keep the old
		 one.  XXX Should load the built-in one here.  */
	      if (i == FONT_INDEX_SPACE)
		continue;
#endif
	      memcpy (df->vga_font[i], df->vga_font[FONT_INDEX_UNKNOWN],
		    32);
	    }
	  else
	    /* XXX Take font size and glyph size into account.  */
	    memcpy (df->vga_font[i], glyph->bitmap, 32);
	}
    }

  /* XXX This will have to be changed when we use font caching.  */
  if (active_dynafont == df)
    vga_write_font_buffer (0, 0, (char *) df->vga_font,
			   df->size * VGA_FONT_HEIGHT);
}
