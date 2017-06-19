/* vga-dynafont.c - Dynamic font handling for VGA cards.
   Copyright (C) 2002, 2010 Free Software Foundation, Inc.
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

#include <stddef.h>
#include <assert-backtrace.h>
#include <malloc.h>
#include <wchar.h>
#include <stdlib.h>
#include <hurd/ihash.h>
#include <string.h>

#include <hurd/console.h>
#include "vga-hw.h"
#include "vga-support.h"
#include "bdf.h"
#include "vga-dynafont.h"
#include "unicode.h"


/* The currently active (visible) dynafont.  The original idea was to
   use some sort of caching for multiple dynafonts using the 8 font
   buffers on the graphic card, but the new idea is to share a common
   state among all virtual consoles and do a full refresh on
   switching.  However, bits and pieces of the old idea are still
   present, in case they become useful.  */
static dynafont_t active_dynafont;


/* One glyph in a VGA font is 8 pixels wide and 32 pixels high.  Only
   the first N lines are visible, and N depends on the VGA register
   settings.  */
typedef unsigned char vga_font_glyph[VGA_FONT_HEIGHT];


/* For each glyph in the VGA font, one instance of this structure is
   held in the dynafont.  */
struct mapped_character
{
  /* Reference count of the mapped character.  If this drops zero, we
     can remove the mapping at any time.  */
  int refs;

  /* Remember the character this glyph belongs to, so the glyph can be
     reloaded when the font is changed.  This is actually a wchar_t
     with some text attributes mixed into the high bits.  */
#define WCHAR_BOLD	((wchar_t) 0x20000000)
#define WCHAR_ITALIC	((wchar_t) 0x10000000)
#define WCHAR_MASK	CONS_WCHAR_MASK
  wchar_t character;

  /* Used by libihash for fast removal of elements.  */
  hurd_ihash_locp_t locp;
};


struct dynafont
{
  /* The sorted font to take the glyphs from.  */
  bdf_font_t font;

  /* The sorted font to take the italic glyphs from.  */
  bdf_font_t font_italic;

  /* The sorted font to take the bold glyphs from.  */
  bdf_font_t font_bold;

  /* The sorted font to take the bold and italic glyphs from.  */
  bdf_font_t font_bold_italic;

  /* The size of the VGA font (256 or 512).  Must be a power of 2!  */
  int size;

  /* The width of the VGA font (8 or 9).  */
  int width;

  /* A hash containing a pointer to a struct mapped_character for each
     UCS-4 character we map to a VGA font index.  */
  struct hurd_ihash charmap;

  /* The struct mapped_characters are preallocated for all vga font
     index values.  This points to an array of SIZE such elements.  */
  struct mapped_character *charmap_data;

  /* The last vga font index that had been free (or could be
     reused).  */
  int vga_font_last_free_index;

  /* The number of free slots in the VGA font.  */
  int vga_font_free_indices;

  int use_lgc;

  /* The last vga font index that had been free (or could be reused)
     for horizontal line graphic characters.  */
  int vga_font_last_free_index_lgc;

  /* The number of free slots in the VGA font for horizontal line
     graphic charactes.  */
  int vga_font_free_indices_lgc;

  /* The font memory as stored on the card.  */
  vga_font_glyph *vga_font;

  /* The cursor size, standout or normal.  */
  int cursor_standout;
};


/* The VGA standard font follows IBM code page 437.  The following
   table maps this to unicode.  For simplicity, a 1:1 mapping is
   assumed.  Ambiguities are special cased in create_system_font.  */
static
wchar_t ibm437_to_unicode[VGA_FONT_SIZE] = {
    0,							/* 0x00 */
    UNICODE_WHITE_SMILING_FACE,				/* 0x01 */
    UNICODE_BLACK_SMILING_FACE,				/* 0x02 */
    UNICODE_BLACK_HEART_SUIT,				/* 0x03 */
    UNICODE_BLACK_DIAMOND_SUIT,				/* 0x04 */
    UNICODE_BLACK_CLUB_SUIT,				/* 0x05 */
    UNICODE_BLACK_SPADE_SUIT,				/* 0x06 */
    UNICODE_BULLET,					/* 0x07 */
    UNICODE_INVERSE_BULLET,				/* 0x08 */
    UNICODE_WHITE_CIRCLE,				/* 0x09 */
    UNICODE_INVERSE_WHITE_CIRCLE,			/* 0x0a */
    UNICODE_MALE_SIGN,					/* 0x0b */
    UNICODE_FEMALE_SIGN,				/* 0x0c */
    UNICODE_EIGHTH_NOTE,				/* 0x0d */
    UNICODE_BEAMED_EIGHTH_NOTES,			/* 0x0e */
    UNICODE_WHITE_SUN_WITH_RAYS,			/* 0x0f */
    UNICODE_BLACK_RIGHT_POINTING_TRIANGLE,		/* 0x10 */
    UNICODE_BLACK_LEFT_POINTING_TRIANGLE,		/* 0x11 */
    UNICODE_UP_DOWN_ARROW,				/* 0x12 */
    UNICODE_DOUBLE_EXCLAMATION_MARK,			/* 0x13 */
    UNICODE_PILCROW_SIGN,				/* 0x14 */
    UNICODE_SECTION_SIGN,				/* 0x15 */
    UNICODE_BLACK_RECTANGLE,				/* 0x16 */
    UNICODE_UP_DOWN_ARROW_WITH_BASE,			/* 0x17 */
    UNICODE_UPWARDS_ARROW,				/* 0x18 */
    UNICODE_DOWNWARDS_ARROW,				/* 0x19 */
    UNICODE_RIGHTWARDS_ARROW,				/* 0x1a */
    UNICODE_LEFTWARDS_ARROW,				/* 0x1b */
    UNICODE_RIGHT_ANGLE,				/* 0x1c */
    UNICODE_LEFT_RIGHT_ARROW,				/* 0x1d */
    UNICODE_BLACK_UP_POINTING_TRIANGLE,			/* 0x1e */
    UNICODE_BLACK_DOWN_POINTING_TRIANGLE,		/* 0x1f */
    ' ', '!', '"', '#', '$', '%', '&', '\'',		/* 0x20 - 0x27 */
    '(', ')', '*', '+', ',', '-', '.', '/',		/* 0x28 - 0x2f */
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',	/* 0x30 - 0x37 */
    ':', ';', '<', '=', '>', '?',			/* 0x38 - 0x3f */
    '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',		/* 0x40 - 0x47 */
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',		/* 0x48 - 0x4f */
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',		/* 0x50 - 0x57 */
    'X', 'Y', 'Z', '[', '\\', ']', '^', '_',		/* 0x58 - 0x5f */
    '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g',		/* 0x60 - 0x67 */
    'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',		/* 0x68 - 0x6f */
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w',		/* 0x70 - 0x77 */
    'x', 'y', 'z', '{', '|', '}', '~', UNICODE_HOUSE,	/* 0x78 - 0x7f */
    UNICODE_LATIN_CAPITAL_LETTER_C_WITH_CEDILLA,	/* 0x80 */
    UNICODE_LATIN_SMALL_LETTER_U_WITH_DIARESIS,		/* 0x81 */
    UNICODE_LATIN_SMALL_LETTER_E_WITH_ACUTE,		/* 0x82 */
    UNICODE_LATIN_SMALL_LETTER_A_WITH_CIRCUMFLEX,	/* 0x83 */
    UNICODE_LATIN_SMALL_LETTER_A_WITH_DIARESIS,		/* 0x84 */
    UNICODE_LATIN_SMALL_LETTER_A_WITH_GRAVE,		/* 0x85 */
    UNICODE_LATIN_SMALL_LETTER_A_WITH_RING_ABOVE,	/* 0x86 */
    UNICODE_LATIN_SMALL_LETTER_C_WITH_CEDILLA,		/* 0x87 */
    UNICODE_LATIN_SMALL_LETTER_E_WITH_CIRCUMFLEX,	/* 0x88 */
    UNICODE_LATIN_SMALL_LETTER_E_WITH_DIARESIS,		/* 0x89 */
    UNICODE_LATIN_SMALL_LETTER_E_WITH_GRAVE,		/* 0x8a */
    UNICODE_LATIN_SMALL_LETTER_I_WITH_DIARESIS,		/* 0x8b */
    UNICODE_LATIN_SMALL_LETTER_I_WITH_CIRCUMFLEX,	/* 0x8c */
    UNICODE_LATIN_SMALL_LETTER_I_WITH_GRAVE,		/* 0x8d */
    UNICODE_LATIN_CAPITAL_LETTER_A_WITH_DIARESIS,	/* 0x8e */
    UNICODE_LATIN_CAPITAL_LETTER_A_WITH_RING_ABOVE,	/* 0x8f */
    UNICODE_LATIN_CAPITAL_LETTER_E_WITH_ACUTE,		/* 0x90 */
    UNICODE_LATIN_SMALL_LETTER_AE,			/* 0x91 */
    UNICODE_LATIN_CAPITAL_LETTER_AE,			/* 0x92 */
    UNICODE_LATIN_SMALL_LETTER_O_WITH_CIRCUMFLEX,	/* 0x93 */
    UNICODE_LATIN_SMALL_LETTER_O_WITH_DIARESIS,		/* 0x94 */
    UNICODE_LATIN_SMALL_LETTER_O_WITH_GRAVE,		/* 0x95 */
    UNICODE_LATIN_SMALL_LETTER_U_WITH_CIRCUMFLEX,	/* 0x96 */
    UNICODE_LATIN_SMALL_LETTER_U_WITH_GRAVE,		/* 0x97 */
    UNICODE_LATIN_SMALL_LETTER_Y_WITH_DIARESIS,		/* 0x98 */
    UNICODE_LATIN_CAPITAL_LETTER_O_WITH_DIARESIS,	/* 0x99 */
    UNICODE_LATIN_CAPITAL_LETTER_U_WITH_DIARESIS,	/* 0x9a */
    UNICODE_CENT_SIGN,					/* 0x9b */
    UNICODE_POUND_SIGN,					/* 0x9c */
    UNICODE_YEN_SIGN,					/* 0x9d */
    UNICODE_PESETA_SIGN,				/* 0x9e */
    UNICODE_LATIN_SMALL_LETTER_F_WITH_HOOK,		/* 0x9f */
    UNICODE_LATIN_SMALL_LETTER_A_WITH_ACUTE,		/* 0xa0 */
    UNICODE_LATIN_SMALL_LETTER_I_WITH_ACUTE,		/* 0xa1 */
    UNICODE_LATIN_SMALL_LETTER_O_WITH_ACUTE,		/* 0xa2 */
    UNICODE_LATIN_SMALL_LETTER_U_WITH_ACUTE,		/* 0xa3 */
    UNICODE_LATIN_SMALL_LETTER_N_WITH_TILDE,		/* 0xa4 */
    UNICODE_LATIN_CAPITAL_LETTER_N_WITH_TILDE,		/* 0xa5 */
    UNICODE_FEMININE_ORDINAL_INDICATOR,			/* 0xa6 */
    UNICODE_MASCULINE_ORDINAL_INDICATOR,		/* 0xa7 */
    UNICODE_INVERTED_QUESTION_MARK,			/* 0xa8 */
    UNICODE_REVERSED_NOT_SIGN,				/* 0xa9 */
    UNICODE_NOT_SIGN,					/* 0xaa */
    UNICODE_VULGAR_FRACTION_ONE_HALF,			/* 0xab */
    UNICODE_VULGAR_FRACTION_ONE_QUARTER,		/* 0xac */
    UNICODE_INVERTED_EXCLAMATION_MARK,			/* 0xad */
    UNICODE_LEFT_POINTING_DOUBLE_ANGLE_QUOTATION_MARK,	/* 0xae */
    UNICODE_RIGHT_POINTING_DOUBLE_ANGLE_QUOTATION_MARK,	/* 0xaf */
    UNICODE_LIGHT_SHADE,				/* 0xb0 */
    UNICODE_MEDIUM_SHADE,				/* 0xb1 */
    UNICODE_DARK_SHADE,					/* 0xb2 */
    UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL,		/* 0xb3 */
    UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_LEFT,	/* 0xb4 */
    UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_LEFT_DOUBLE, /* 0xb5 */
    UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_LEFT_SINGLE, /* 0xb6 */
    UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_LEFT_SINGLE,	/* 0xb7 */
    UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_LEFT_DOUBLE,	/* 0xb8 */
    UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_LEFT,	/* 0xb9 */
    UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL,		/* 0xba */
    UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_LEFT,		/* 0xbb */
    UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_LEFT,		/* 0xbc */
    UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_LEFT_SINGLE,	/* 0xbd */
    UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_LEFT_DOUBLE,	/* 0xbe */
    UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_LEFT,		/* 0xbf */
    UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_RIGHT,		/* 0xc0 */
    UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_HORIZONTAL,	/* 0xc1 */
    UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_HORIZONTAL,	/* 0xc2 */
    UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_RIGHT,	/* 0xc3 */
    UNICODE_BOX_DRAWINGS_LIGHT_HORIZONTAL,		/* 0xc4 */
    UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_HORIZONTAL, /* 0xc5 */
    UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_RIGHT_DOUBLE, /* 0xc6 */
    UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_RIGHT_SINGLE, /* 0xc7 */
    UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_RIGHT,		/* 0xc8 */
    UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_RIGHT,		/* 0xc9 */
    UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_HORIZONTAL,	/* 0xca */
    UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_HORIZONTAL,	/* 0xcb */
    UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_RIGHT,	/* 0xcc */
    UNICODE_BOX_DRAWINGS_DOUBLE_HORIZONTAL,		/* 0xcd */
    UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_HORIZONTAL,/* 0xce */
    UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_HORIZONTAL_DOUBLE, /* 0xcf */
    UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_HORIZONTAL_SINGLE, /* 0xd0 */
    UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_HORIZONTAL_DOUBLE, /* 0xd1 */
    UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_HORIZONTAL_SINGLE, /* 0xd2 */
    UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_RIGHT_SINGLE,	/* 0xd3 */
    UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_RIGHT_DOUBLE,	/* 0xd4 */
    UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_RIGHT_DOUBLE,	/* 0xd5 */
    UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_RIGHT_SINGLE,	/* 0xd6 */
    UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_HORIZONTAL_SINGLE, /* 0xd7 */
    UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_HORIZONTAL_DOUBLE, /* 0xd8 */
    UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_LEFT,		/* 0xd9 */
    UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_RIGHT,		/* 0xda */
    UNICODE_FULL_BLOCK,					/* 0xdb */
    UNICODE_LOWER_HALF_BLOCK,				/* 0xdc */
    UNICODE_LEFT_HALF_BLOCK,				/* 0xdd */
    UNICODE_RIGHT_HALF_BLOCK,				/* 0xde */
    UNICODE_UPPER_HALF_BLOCK,				/* 0xdf */
    UNICODE_GREEK_SMALL_LETTER_ALPHA,			/* 0xe0 */
    /* The next one: Also sz-ligature.  */
    UNICODE_GREEK_SMALL_LETTER_BETA,			/* 0xe1 */
    UNICODE_GREEK_CAPITAL_LETTER_GAMMA,			/* 0xe2 */
    UNICODE_GREEK_SMALL_LETTER_PI,			/* 0xe3 */
    UNICODE_GREEK_CAPITAL_LETTER_SIGMA,			/* 0xe4 */
    UNICODE_GREEK_SMALL_LETTER_SIGMA,			/* 0xe5 */
    /* The next one: Also micro.  */
    UNICODE_GREEK_SMALL_LETTER_MU,			/* 0xe6 */
    UNICODE_GREEK_SMALL_LETTER_TAU,			/* 0xe7 */
    UNICODE_GREEK_CAPITAL_LETTER_PHI,			/* 0xe8 */
    UNICODE_GREEK_CAPITAL_LETTER_OMICRON,		/* 0xe9 */
    UNICODE_GREEK_CAPITAL_LETTER_OMEGA,			/* 0xea */
    UNICODE_GREEK_SMALL_LETTER_DELTA,			/* 0xeb */
    UNICODE_INFINITY,					/* 0xec */
    UNICODE_GREEK_SMALL_LETTER_PHI,			/* 0xed */
    UNICODE_GREEK_SMALL_LETTER_EPSILON,			/* 0xee */
    UNICODE_INTERSECTION,				/* 0xef */
    UNICODE_IDENTICAL_TO,				/* 0xf0 */
    UNICODE_PLUS_MINUS_SIGN,				/* 0xf1 */
    UNICODE_GREATER_THAN_OR_EQUAL_TO,	       		/* 0xf2 */
    UNICODE_LESS_THAN_OR_EQUAL_TO,			/* 0xf3 */
    UNICODE_TOP_HALF_INTEGRAL,				/* 0xf4 */
    UNICODE_BOTTOM_HALF_INTEGRAL,			/* 0xf5 */
    UNICODE_DIVISION_SIGN,				/* 0xf6 */
    UNICODE_ALMOST_EQUAL_TO,				/* 0xf7 */
    UNICODE_DEGREE_SIGN,				/* 0xf8 */
    UNICODE_BULLET_OPERATOR,				/* 0xf9 */
    UNICODE_MIDDLE_DOT,					/* 0xfa */
    UNICODE_SQUARE_ROOT,				/* 0xfb */
    UNICODE_SUPERSCRIPT_LATIN_SMALL_LETTER,		/* 0xfc */
    UNICODE_SUPERSCRIPT_TWO,				/* 0xfd */
    UNICODE_BLACK_SQUARE,				/* 0xfe */
    UNICODE_NO_BREAK_SPACE				/* 0xff */
  };

/* Read the VGA card's memory as IBM 437 font and create a Unicode BDF
   font from it.  If an error occurs, errno is set and NULL is
   returned.  */ 
static bdf_font_t
create_system_font (void)
{
  bdf_error_t bdferr;
  bdf_font_t font;
  unsigned char bitmap[VGA_FONT_SIZE][VGA_FONT_HEIGHT];	/* 8kB on stack.  */
  int width = vga_get_font_width ();
  int i;

  /* Add the glyph at position POS to the font for character
     ENCODING.  */
  void vga_add_glyph (int pos, int encoding)
    {
      char name[16];
      snprintf (name, sizeof (name), "VGA %i", pos);
 
      if (width == 8)
	bdferr = bdf_add_glyph (font, name, encoding,
				0, 8, 16, 0, 0, bitmap[pos]);
      else
	{
	  int i;
	  unsigned char glyph_bitmap[32];
	  
	  for (i = 0; i < 16; i++)
	    {
	      glyph_bitmap[i * 2] = bitmap[pos][i];
	      if (pos >= VGA_FONT_LGC_BEGIN
		  && pos < VGA_FONT_LGC_BEGIN + VGA_FONT_LGC_COUNT)
		glyph_bitmap[i * 2 + 1]
		  = (bitmap[pos][i] & 1) ? 0x80 : 0;
	      else
		glyph_bitmap[i * 2 + 1] = 0;
	    }
	  bdferr = bdf_add_glyph (font, name, encoding,
				  0, 9, 16, 0, 0, glyph_bitmap);
	}
    }

  /* The point size and resolution is arbitrary.  */
  bdferr = bdf_new (&font, 2, 2, "vga-system", 10, 100, 100,
		    width, 16, 0, 0, 0);
  if (bdferr)
    {
      if (bdferr != BDF_SYSTEM_ERROR)
	errno = EGRATUITOUS;
      return NULL;
    }

  vga_read_font_buffer (0, 0, (unsigned char *) bitmap,
			VGA_FONT_SIZE * VGA_FONT_HEIGHT);

  for (i = 0; i < VGA_FONT_SIZE; i++)
    if (ibm437_to_unicode[i])
      {
	vga_add_glyph (i, ibm437_to_unicode[i]);
	if (bdferr)
	  break;

	/* Some glyphs are ambivalent.  */
	if (ibm437_to_unicode[i] == UNICODE_GREEK_SMALL_LETTER_BETA)
	  vga_add_glyph (i, UNICODE_LATIN_SMALL_LETTER_SHARP_S);
	else if (ibm437_to_unicode[i] == UNICODE_GREEK_SMALL_LETTER_MU)
	  vga_add_glyph (i, UNICODE_MICRO_SIGN);
	if (bdferr)
	  break;
      }

  if (bdferr)
    {
      bdf_destroy (font);
      if (bdferr != BDF_SYSTEM_ERROR)
	errno = EGRATUITOUS;
      return NULL;
    }

  return font;
}


#if QUAERENDO_INVENIETIS
#define GNU_HEAD_BEGIN (UNICODE_PRIVATE_USE_AREA + 0x0f00)

static void
add_gnu_head (bdf_font_t font)
{
#define GNU_HEAD_WIDTH 6
  static unsigned char gnu_head[][GNU_HEAD_WIDTH] =
    {
      { 255, 255, 255, 255, 255, 255 }, { 255,   0, 127, 255, 252,  31 },
      { 252,   0,  31, 255, 224,   7 }, { 248,   0,   7, 255,   0,   3 },
      { 240,   0,  15, 255, 128,   3 }, { 240,  31, 255, 255, 252,   1 },
      { 224,  63, 241, 255, 255,   1 }, { 192, 127, 128,  96, 255, 129 },
      { 192, 255,   0,   0,  63, 193 }, { 192, 254,   0,   0,  31, 193 },
      { 192, 252,   0,   0,  15, 193 }, { 192, 248,   0,   0,  15, 193 },
      { 192, 248,   0,   0,   7, 129 }, { 192,  96,  63, 131, 192,   1 },
      { 192,   1, 227,  98, 112,   1 }, { 192,   3, 195, 244, 176,   3 },
      { 224,   7, 221, 125, 248,   3 }, { 240,  15, 184, 124, 120,   7 },
      { 240,  15, 248, 124,  60,  15 }, { 248,  15, 220, 254, 124, 127 },
      { 252,  31, 223, 255, 254, 127 }, { 255, 159, 159, 255,  31, 191 },
      { 255, 223, 159, 255,  35,  63 }, { 255, 191, 127, 195, 152, 127 },
      { 255, 188, 255, 156, 199, 255 }, { 255,  96, 253, 134, 115, 255 },
      { 254, 134, 251, 227, 251, 255 }, { 254,  46, 254, 249, 251, 255 },
      { 255, 238, 126, 127, 231, 255 }, { 255, 239, 127, 127, 207, 255 },
      { 255, 239,  63,  63, 231, 255 }, { 255, 247, 159, 158,  15, 255 },
      { 255, 247, 207, 193, 159, 255 }, { 255, 247, 223, 255, 223, 255 },
      { 255, 243, 199, 252,  31, 255 }, { 255, 251, 227, 224,  63, 255 },
      { 255, 253, 241, 240, 255, 255 }, { 255, 252, 244, 126, 255, 255 },
      { 255, 254, 121, 122, 255, 255 }, { 255, 255, 252,  48, 255, 255 },
      { 255, 255, 252,  35, 255, 255 }, { 255, 255, 249,   1, 127, 255 },
      { 255, 255, 251,   0, 127, 255 }, { 255, 255, 255, 128, 255, 255 },
      { 255, 255, 255, 255, 255, 255 }
    };
  /* Height of a single glyph, truncated to fit VGA glyph slot.  */
  int height = (font->bbox.height > 32) ? 32 : font->bbox.height;
  /* Width of a single glyph in byte.  */
  int width = (font->bbox.width + 7) / 8;
  /* Number of rows in bitmap.  */
  int rows = sizeof (gnu_head) / sizeof (gnu_head[0]);
  /* Number of rows of glyphs necessary.  */
  int nr = (rows + height - 1) / height;
  int row, col;

  /* Check that all those glyphs are free in the private area.  */
  if (nr * GNU_HEAD_WIDTH > GNU_HEAD_BEGIN - UNICODE_PRIVATE_USE_AREA + 1)
    return;
  for (int i = 0; i < nr * GNU_HEAD_WIDTH; i++)
    if (bdf_find_glyph (font, (int) GNU_HEAD_BEGIN + i, 0)
	|| bdf_find_glyph (font, -1, (int) GNU_HEAD_BEGIN + i))
      return;
      
  for (row = 0; row < nr; row++)
    for (col = 0; col < GNU_HEAD_WIDTH; col++)
      {
	char bitmap[font->bbox.height][width];
	char name[] = "GNU Head ..........";

	/* strlen ("GNU Head ") == 9.  */
	sprintf (&name[9], "%i", row * GNU_HEAD_WIDTH + col);
	
	memset (bitmap, 0, sizeof (bitmap));
	for (int j = 0; j < height && row * height + j < rows; j++)
	  bitmap[j][0] = gnu_head[row * height + j][col];

	if (bdf_add_glyph (font, name,
			   GNU_HEAD_BEGIN + row * GNU_HEAD_WIDTH + col,
			   0, font->bbox.width, font->bbox.height,
			   0, 0, (unsigned char *) bitmap))
	  return;
      }
}
#endif 

/* Create a new dynafont object, which uses glyphs from the font FONT
   (which should be 8 or 9 pixels wide and 13 to 16 pixels high).
   SIZE is either 256 or 512, and specifies the number of available
   glyphs in the font cache.  The object is returned in DYNAFONT.

   The font is consumed.  If FONT is the null pointer, the VGA card's
   memory is converted to a font according to the VGA default map (IBM
   Code Page 437).  */
error_t
dynafont_new (bdf_font_t font, bdf_font_t font_italic, bdf_font_t font_bold,
	      bdf_font_t font_bold_italic, int size, int width,
	      dynafont_t *dynafont)
{
  dynafont_t df;
  struct bdf_glyph *glyph = NULL;

  if (!font)
    font = create_system_font ();
  if (!font || !font->bbox.height)
    return errno;
  if (!width)
    width = font->bbox.width;
  if ((width % 8) == 0)
    width = 8;
  if (width != 8 && width != 9)
    return EINVAL;

  df = malloc (sizeof *df);
  if (!df)
    return ENOMEM;

#if QUAERENDO_INVENIETIS
  add_gnu_head (font);
#endif
  bdf_sort_glyphs (font);
  df->font = font;
  df->font_italic = font_italic;
  df->font_bold = font_bold;
  df->font_bold_italic = font_bold_italic;
  df->size = size;
  df->width = width;
  df->cursor_standout = 0;

  df->charmap_data = calloc (size, sizeof (struct mapped_character));
  if (!df->charmap_data)
    {
      free (df);
      return ENOMEM;
    }

  df->vga_font = malloc (sizeof (vga_font_glyph) * size);
  if (!df->vga_font)
    {
      free (df->charmap_data);
      free (df);
      return ENOMEM;
    }

  hurd_ihash_init (&df->charmap, offsetof (struct mapped_character, locp));

  if (width == 9)
    {
      /* 32 from 256 font slots are for horizontal line graphic
	 characters.  */
      df->use_lgc = 1;
      df->vga_font_free_indices = df->size
	- (df->size / 256) * VGA_FONT_LGC_COUNT;
      df->vga_font_last_free_index = 0;
      df->vga_font_free_indices_lgc = (df->size / 256) * VGA_FONT_LGC_COUNT;
      df->vga_font_last_free_index_lgc = VGA_FONT_LGC_BEGIN;
    }
  else
    {
      df->use_lgc = 0;
      df->vga_font_free_indices = df->size;
      df->vga_font_last_free_index = 0;
      df->vga_font_free_indices_lgc = 0;
      df->vga_font_last_free_index_lgc = 0;
    }

  /* Ensure that ASCII is always available 1-to-1, for kernel messages.  */
  for (int c = ' '; c <= '~'; c++)
    {
      glyph = bdf_find_glyph (df->font, c, 0);
      if (!glyph)
	glyph = bdf_find_glyph (df->font, -1, c);
      if (glyph)
	{
	  struct mapped_character *chr = &df->charmap_data[c];
	  df->vga_font_free_indices--;
	  chr->refs = 1;

	  for (int i = 0; i < ((glyph->bbox.height > 32)
			       ? 32 : glyph->bbox.height); i++)
	    df->vga_font[c][i]
	      = glyph->bitmap[i * ((glyph->bbox.width + 7) / 8)];
	  if (glyph->bbox.height < 32)
	    memset (((char *) df->vga_font[c])
		    + glyph->bbox.height, 0, 32 - glyph->bbox.height);

	  /* Update the hash table.  */
	  hurd_ihash_add (&df->charmap, c, chr);
	}
    }

  /* Ensure that we always have the replacement character
     available.  */
  {
    struct mapped_character *chr = &df->charmap_data[FONT_INDEX_UNKNOWN];
    df->vga_font_free_indices--;
    chr->refs = 1;

    glyph = bdf_find_glyph (df->font, UNICODE_REPLACEMENT_CHARACTER, 0);
    if (!glyph)
      glyph = bdf_find_glyph (df->font, -1, UNICODE_REPLACEMENT_CHARACTER);
    if (glyph)
      {
	for (int i = 0; i < ((glyph->bbox.height > 32)
			     ? 32 : glyph->bbox.height); i++)
	  df->vga_font[FONT_INDEX_UNKNOWN][i]
	    = glyph->bitmap[i * ((glyph->bbox.width + 7) / 8)];
	if (glyph->bbox.height < 32)
	  memset (((char *) df->vga_font[FONT_INDEX_UNKNOWN])
		  + glyph->bbox.height, 0, 32 - glyph->bbox.height);

	/* Update the hash table.  */
	hurd_ihash_add (&df->charmap, UNICODE_REPLACEMENT_CHARACTER, chr);
      }
    else
      {
	int i;
	unsigned char *gl = df->vga_font[FONT_INDEX_UNKNOWN];
	/* XXX Take font height into account.  */
	gl[0] = 0x7E;	/*  ******  */
	gl[1] = 0xC3;	/* **    ** */
	gl[2] = 0x99;	/* *  **  * */
	gl[3] = 0x99;	/* *  **  * */
	gl[4] = 0xF9;	/* *****  * */
	gl[5] = 0xF3;	/* ****  ** */
	gl[6] = 0xF3;	/* ***  *** */
	gl[7] = 0xE7;	/* ***  *** */
	gl[8] = 0xFF;	/* ******** */
	gl[9] = 0xE7;	/* ***  *** */
	gl[10] = 0xE7;	/* ***  *** */
	gl[11] = 0x7E;	/*  ******  */
	for (i = 12; i < 32; i++)
	  gl[i] = 0;
      }
  }

  *dynafont = df;
  return 0;
}


/* Release a dynafont object and its associated resources.  */
void
dynafont_free (dynafont_t df)
{
  if (active_dynafont == df)
    active_dynafont = NULL;

  bdf_destroy (df->font);
  if (df->font_italic)
    bdf_destroy (df->font_italic);
  if (df->font_bold)
    bdf_destroy (df->font_bold);
  if (df->font_bold_italic)
    bdf_destroy (df->font_bold_italic);
  hurd_ihash_destroy (&df->charmap);
  free (df->charmap_data);
  free (df->vga_font);
  free (df);
}


/* Determine if CHR is a character that needs to be continuous in the
   horizontal direction by repeating the last (eighth) column in
   9-pixel-width mode.  */
static inline int
is_lgc (wchar_t chr)
{
  /* This list must be sorted for bsearch!  */
  static wchar_t horiz_glyphs[] =
    {
      UNICODE_BOX_DRAWINGS_LIGHT_HORIZONTAL,			/* 0x2500 */
      UNICODE_BOX_DRAWINGS_HEAVY_HORIZONTAL,			/* 0x2501 */
      UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_RIGHT,		/* 0x250c */
      UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_RIGHT_HEAVY,		/* 0x250d */
      UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_RIGHT_LIGHT,		/* 0x250e */
      UNICODE_BOX_DRAWINGS_HEAVY_DOWN_AND_RIGHT,		/* 0x250f */
      UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_RIGHT,			/* 0x2514 */
      UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_RIGHT_HEAVY,		/* 0x2515 */
      UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_RIGHT_LIGHT,		/* 0x2516 */
      UNICODE_BOX_DRAWINGS_HEAVY_UP_AND_RIGHT,			/* 0x2517 */
      UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_RIGHT,		/* 0x251c */
      UNICODE_BOX_DRAWINGS_VERTICAL_LIGHT_AND_RIGHT_HEAVY,	/* 0x251d */
      UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_RIGHT_UP_LIGHT,		/* 0x251e */
      UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_RIGHT_UP_LIGHT,	/* 0x251f */
      UNICODE_BOX_DRAWINGS_VERTICAL_HEAVY_AND_RIGHT_LIGHT,	/* 0x2520 */
      UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_RIGHT_UP_HEAVY,	/* 0x2521 */
      UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_RIGHT_DOWN_HEAVY,	/* 0x2522 */
      UNICODE_BOX_DRAWINGS_HEAVY_VERTICAL_AND_RIGHT,		/* 0x2523 */
      UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_HORIZONTAL,		/* 0x252c */
      UNICODE_BOX_DRAWINGS_LEFT_HEAVY_AND_RIGHT_DOWN_LIGHT,	/* 0x252d */
      UNICODE_BOX_DRAWINGS_RIGHT_HEAVY_AND_LEFT_DOWN_LIGHT,	/* 0x252e */
      UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_HORIZONTAL_HEAVY,	/* 0x252f */
      UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_HORIZONTAL_LIGHT,	/* 0x2530 */
      UNICODE_BOX_DRAWINGS_RIGHT_LIGHT_AND_LEFT_DOWN_HEAVY,	/* 0x2531 */
      UNICODE_BOX_DRAWINGS_LEFT_LIGHT_AND_RIGHT_DOWN_HEAVY,	/* 0x2532 */
      UNICODE_BOX_DRAWINGS_HEAVY_DOWN_AND_HORIZONTAL,		/* 0x2533 */
      UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_HORIZONTAL,		/* 0x2534 */
      UNICODE_BOX_DRAWINGS_LEFT_HEAVY_AND_RIGHT_UP_LIGHT,	/* 0x2535 */
      UNICODE_BOX_DRAWINGS_RIGHT_HEAVY_AND_LEFT_UP_LIGHT,	/* 0x2536 */
      UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_HORIZONTAL_HEAVY,	/* 0x2537 */
      UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_HORIZONTAL_LIGHT,	/* 0x2538 */
      UNICODE_BOX_DRAWINGS_RIGHT_LIGHT_AND_LEFT_UP_HEAVY,	/* 0x2539 */
      UNICODE_BOX_DRAWINGS_LEFT_LIGHT_AND_RIGHT_UP_HEAVY,	/* 0x253a */
      UNICODE_BOX_DRAWINGS_HEAVY_UP_AND_HORIZONTAL,		/* 0x253b */
      UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_HORIZONTAL,	/* 0x253c */
      UNICODE_BOX_DRAWINGS_LEFT_HEAVY_AND_RIGHT_VERTICAL_LIGHT,	/* 0x253d */
      UNICODE_BOX_DRAWINGS_RIGHT_HEAVY_AND_LEFT_VERTICAL_LIGHT,	/* 0x253e */
      UNICODE_BOX_DRAWINGS_VERTICAL_LIGHT_AND_HORIZONTAL_HEAVY,	/* 0x253f */
      UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_DOWN_HORIZONTAL_LIGHT,	/* 0x2540 */
      UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_UP_HORIZONTAL_LIGHT,	/* 0x2541 */
      UNICODE_BOX_DRAWINGS_VERTICAL_HEAVY_AND_HORIZONTAL_LIGHT,	/* 0x2542 */
      UNICODE_BOX_DRAWINGS_LEFT_UP_HEAVY_AND_RIGHT_DOWN_LIGHT,	/* 0x2543 */
      UNICODE_BOX_DRAWINGS_RIGHT_UP_HEAVY_AND_LEFT_DOWN_LIGHT,	/* 0x2544 */
      UNICODE_BOX_DRAWINGS_LEFT_DOWN_HEAVY_AND_RIGHT_UP_LIGHT,	/* 0x2545 */
      UNICODE_BOX_DRAWINGS_RIGHT_DOWN_HEAVY_AND_LEFT_UP_LIGHT,	/* 0x2546 */
      UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_UP_HORIZONTAL_HEAVY,	/* 0x2547 */
      UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_DOWN_HORIZONTAL_HEAVY,	/* 0x2548 */
      UNICODE_BOX_DRAWINGS_RIGHT_LIGHT_AND_LEFT_VERTICAL_HEAVY,	/* 0x2549 */
      UNICODE_BOX_DRAWINGS_LEFT_LIGHT_AND_RIGHT_VERTICAL_HEAVY,	/* 0x254a */
      UNICODE_BOX_DRAWINGS_HEAVY_VERTICAL_AND_HORIZONTAL,	/* 0x254b */
      UNICODE_BOX_DRAWINGS_DOUBLE_HORIZONTAL,			/* 0x2550 */
      UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_RIGHT_DOUBLE,	/* 0x2552 */
      UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_RIGHT_SINGLE,	/* 0x2553 */
      UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_RIGHT,		/* 0x2554 */
      UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_RIGHT_DOUBLE,		/* 0x2558 */
      UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_RIGHT_SINGLE,		/* 0x2559 */
      UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_RIGHT,			/* 0x255a */
      UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_RIGHT_DOUBLE,	/* 0x255e */
      UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_RIGHT_SINGLE,	/* 0x255f */
      UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_RIGHT,		/* 0x2560 */
      UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_HORIZONTAL_DOUBLE,	/* 0x2564 */
      UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_HORIZONTAL_SINGLE,	/* 0x2565 */
      UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_HORIZONTAL,		/* 0x2566 */
      UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_HORIZONTAL_DOUBLE,	/* 0x2567 */
      UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_HORIZONTAL_SINGLE,	/* 0x2568 */
      UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_HORIZONTAL,		/* 0x2569 */
      UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_HORIZONTAL_DOUBLE, /* 0x256a */
      UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_HORIZONTAL_SINGLE, /* 0x256b */
      UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_HORIZONTAL,	/* 0x256c */
      UNICODE_BOX_DRAWINGS_LIGHT_ARC_DOWN_AND_RIGHT,		/* 0x256d */
      UNICODE_BOX_DRAWINGS_LIGHT_ARC_UP_AND_RIGHT,		/* 0x2570 */
#if 0
      /* The diagonal lines don't look much better with or without the
	 ninth column.  */
      /* 0x2571 */
      UNICODE_BOX_DRAWINGS_LIGHT_DIAGONAL_UPPER_RIGHT_TO_LOWER_LEFT,
      /* 0x2572 */
      UNICODE_BOX_DRAWINGS_LIGHT_DIAGONAL_UPPER_LEFT_TO_LOWER_RIGHT,
      UNICODE_BOX_DRAWINGS_LIGHT_DIAGONAL_CROSS,		/* 0x2573 */
#endif
      UNICODE_BOX_DRAWINGS_LIGHT_RIGHT,				/* 0x2576 */
      UNICODE_BOX_DRAWINGS_HEAVY_RIGHT,				/* 0x257a */
      UNICODE_BOX_DRAWINGS_LIGHT_LEFT_AND_HEAVY_RIGHT,		/* 0x257c */
      UNICODE_BOX_DRAWINGS_HEAVY_LEFT_AND_LIGHT_RIGHT,		/* 0x257e */
      UNICODE_UPPER_HALF_BLOCK,					/* 0x2580 */
      UNICODE_LOWER_ONE_EIGHTH_BLOCK,				/* 0x2581 */
      UNICODE_LOWER_ONE_QUARTER_BLOCK,				/* 0x2582 */
      UNICODE_LOWER_THREE_EIGHTHS_BLOCK,			/* 0x2583 */
      UNICODE_LOWER_HALF_BLOCK,					/* 0x2584 */
      UNICODE_LOWER_FIVE_EIGHTHS_BLOCK,				/* 0x2585 */
      UNICODE_LOWER_THREE_QUARTERS_BLOCK,			/* 0x2586 */
      UNICODE_LOWER_SEVEN_EIGHTHS_BLOCK,			/* 0x2587 */
      UNICODE_FULL_BLOCK,					/* 0x2588 */
      UNICODE_RIGHT_HALF_BLOCK,					/* 0x2590 */
#if 0
      /* The shades don't look much better with or without the ninth
	 column.  */
      UNICODE_LIGHT_SHADE,					/* 0x2591 */
      UNICODE_MEDIUM_SHADE,					/* 0x2592 */
      UNICODE_DARK_SHADE,					/* 0x2593 */
#endif
      UNICODE_BLACK_SQUARE,					/* 0x25a0 */
      UNICODE_UPPER_ONE_EIGHTH_BLOCK,				/* 0x2594 */
      UNICODE_RIGHT_ONE_EIGHTH_BLOCK,				/* 0x2595 */
      UNICODE_QUADRANT_LOWER_RIGHT,				/* 0x2597 */
      UNICODE_QUADRANT_UPPER_LEFT_AND_LOWER_LEFT_AND_LOWER_RIGHT, /* 0x2599 */
      UNICODE_QUADRANT_UPPER_LEFT_AND_LOWER_RIGHT,		/* 0x259a */
      UNICODE_QUADRANT_UPPER_LEFT_AND_UPPER_RIGHT_AND_LOWER_LEFT, /* 0x259b */
      UNICODE_QUADRANT_UPPER_LEFT_AND_UPPER_RIGHT_AND_LOWER_RIGHT, /* 0x259c */
      UNICODE_QUADRANT_UPPER_RIGHT,				/* 0x259d */
      UNICODE_QUADRANT_UPPER_RIGHT_AND_LOWER_LEFT,		/* 0x259e */
      UNICODE_QUADRANT_UPPER_RIGHT_AND_LOWER_LEFT_AND_LOWER_RIGHT, /* 0x259f */
    };

  int cmp_wchar (const void *a, const void *b)
    {
      const wchar_t *wa = (const wchar_t *) a;
      const wchar_t *wb = (const wchar_t *) b;
      return (*wa > *wb) - (*wa < *wb);
    }

#if QUAERENDO_INVENIETIS
  /* XXX The (maximum) size is hard coded.  */
  if (chr >= GNU_HEAD_BEGIN && chr <= GNU_HEAD_BEGIN + 50)
    return 1;
#endif

  return bsearch (&chr, horiz_glyphs,
		  sizeof (horiz_glyphs) / sizeof (horiz_glyphs[0]),
		  sizeof (horiz_glyphs[0]), cmp_wchar) ? 1 : 0;
}


/* Try to look up glyph CHR in font FONT of dynafont DF with attribute
   ATTR.  This is a helper function for dynafont_lookup.  Returns 1 on
   success, 0 otherwise.  */
static int
dynafont_lookup_internal (dynafont_t df, bdf_font_t font,
			  wchar_t wide_chr, wchar_t attr, int *rpos)
{
  /* When hashing the character, we mix in the font attribute.  */
  struct mapped_character *chr = hurd_ihash_find (&df->charmap,
						  (int) (wide_chr | attr));
  int lgc;
  struct bdf_glyph *glyph;
  int pos;
  int found = 0;

  lgc = df->use_lgc ? is_lgc (wide_chr) : 0;

  if (chr)
    {
      if (!chr->refs++)
	{
	  if (lgc)
	    df->vga_font_free_indices_lgc--;
	  else
	    df->vga_font_free_indices--;
	}
      /* Return the index of CHR.  */
      *rpos = chr - df->charmap_data;
      return 1;
    }

  /* The character is not currently mapped.  Look for an empty slot
     and add it.  */
  if ((lgc && !df->vga_font_free_indices_lgc)
      || (!lgc && !df->vga_font_free_indices))
    return 0;

  glyph = bdf_find_glyph (font, (int) (wide_chr & ~CONS_WCHAR_CONTINUED), 0);
  if (!glyph)
    glyph = bdf_find_glyph (font, -1, (int) (wide_chr & ~CONS_WCHAR_CONTINUED));
  if (!glyph)
    return 0;

  if (lgc)
    {
      int start_pos = df->vga_font_last_free_index_lgc + 1;
      if ((start_pos % VGA_FONT_SIZE)
	  == VGA_FONT_LGC_BEGIN + VGA_FONT_LGC_COUNT)
	{
	  start_pos += VGA_FONT_SIZE - VGA_FONT_LGC_COUNT;
	  start_pos %= df->size;
	}
      pos = start_pos;

      do
	{
	  if (df->charmap_data[pos].refs == 0)
	    {
	      found = 1;
	      break;
	    }
	  pos++;
	  if ((pos % VGA_FONT_SIZE) == VGA_FONT_LGC_BEGIN + VGA_FONT_LGC_COUNT)
	    {
	      pos += VGA_FONT_SIZE - VGA_FONT_LGC_COUNT;
	      pos %= df->size;
	    }
	}
      while (pos != start_pos);

      assert_backtrace (found);
      df->vga_font_free_indices_lgc--;
      df->vga_font_last_free_index_lgc = pos;
    }
  else
    {
      int start_pos = (df->vga_font_last_free_index + 1) % df->size;
      if (df->use_lgc && (start_pos % VGA_FONT_SIZE) == VGA_FONT_LGC_BEGIN)
	start_pos += VGA_FONT_LGC_COUNT;
      pos = start_pos;

      do
	{
	  if (df->charmap_data[pos].refs == 0)
	    {
	      found = 1;
	      break;
	    }
	  pos = (pos + 1) % df->size;
	  if (df->use_lgc && (pos % VGA_FONT_SIZE) == VGA_FONT_LGC_BEGIN)
	    pos += VGA_FONT_LGC_COUNT;
	}
      while (pos != start_pos);

      assert_backtrace (found);
      df->vga_font_free_indices--;
      df->vga_font_last_free_index = pos;
    }

  /* Ok, we found a new entry, use it.  */
  chr = &df->charmap_data[pos];
  chr->refs = 1;
  chr->character = (wide_chr | attr);

  /* Copy the glyph bitmap, taking into account double-width characters.  */
  {
    int height = (glyph->bbox.height > 32) ? 32 : glyph->bbox.height;
    int bwidth = (glyph->bbox.width + 7) / 8;
    int ofs = (bwidth >= 2) && (wide_chr & CONS_WCHAR_CONTINUED);

    for (int i = 0; i < height; i++)
      df->vga_font[pos][i] = glyph->bitmap[i * bwidth + ofs];
    if (height < 32)
      memset (&df->vga_font[pos][height], 0, 32 - height);
  }
  
  if (active_dynafont == df)
    vga_write_font_buffer (0, pos, df->vga_font[pos],
			   VGA_FONT_HEIGHT);
  /* Update the hash table.  */
  if (chr->locp)
    hurd_ihash_locp_remove (&df->charmap, chr->locp);
  hurd_ihash_add (&df->charmap, (int) (wide_chr | attr), chr);
  *rpos = pos;
  return 1;
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
dynafont_lookup (dynafont_t df, conchar_t *conchr)
{
  wchar_t attr = (conchr->attr.italic ? WCHAR_ITALIC : 0)
    | (conchr->attr.bold ? WCHAR_BOLD : 0);
  int found = 0;
  int pos = FONT_INDEX_UNKNOWN;

  if (attr == (WCHAR_BOLD | WCHAR_ITALIC) && df->font_bold_italic)
    found = dynafont_lookup_internal (df, df->font_bold_italic,
				      conchr->chr, WCHAR_BOLD | WCHAR_ITALIC,
				      &pos);
  /* This order will prefer bold over italic if both are requested but
     are not available at the same time.  The other order is just as
     good.  XXX.  */
  if (!found && (attr & WCHAR_BOLD) && df->font_bold)
    found = dynafont_lookup_internal (df, df->font_bold,
				      conchr->chr, WCHAR_BOLD, &pos);
  if (!found && (attr & WCHAR_ITALIC) && df->font_italic)
    found = dynafont_lookup_internal (df, df->font_italic,
				    conchr->chr, WCHAR_ITALIC, &pos);
  if (!found)
    found = dynafont_lookup_internal (df, df->font, conchr->chr, 0, &pos);
  if (!found)
    {
      df->charmap_data[FONT_INDEX_UNKNOWN].refs++;
      pos = FONT_INDEX_UNKNOWN;
    }
  return pos;
}


/* Release a reference to the glyph VGA_FONT_INDEX in dynafont DF.  */
void
dynafont_release (dynafont_t df, int vga_font_index)
{
  if (! --df->charmap_data[vga_font_index].refs)
    {
      if (df->use_lgc
	  && is_lgc (df->charmap_data[vga_font_index].character & WCHAR_MASK))
	df->vga_font_free_indices_lgc++;
      else
	df->vga_font_free_indices++;
    }
}



/* Set the cursor to normal if STANDOUT is zero, or to a block cursor
   otherwise.  */
void
dynafont_set_cursor (dynafont_t df, int standout)
{
  int height = (df->font->bbox.height > 32) ? 32 : df->font->bbox.height;

  df->cursor_standout = standout;

  if (df == active_dynafont)
    {
      if (standout)
	vga_set_cursor_size (1, height - 1);
      else
	vga_set_cursor_size ((height >= 2) ? height - 2 : 0, height - 1);
    }
}


/* Load the VGA font to the card and make it active.  */
void
dynafont_activate (dynafont_t df)
{
  int height = (df->font->bbox.height > 32) ? 32 : df->font->bbox.height;

  vga_write_font_buffer (0, 0, (unsigned char *) df->vga_font,
			 df->size * VGA_FONT_HEIGHT);
  vga_select_font_buffer (0, (df->size == 512) ? 1 : 0);

  /* XXX Changing the font height below 13 or above 16 will cause
     display problems for the user if we don't also program the video
     mode timings.  The standard font height for 80x25 is 16.  */
  vga_set_font_height (height);
  vga_set_font_width (df->width);

  active_dynafont = df;
  dynafont_set_cursor (df, df->cursor_standout);
}


#if 0
/* XXX This code is deactivated because it needs to be changed to
   allow the italic and bold code, too.  */

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
      if (! df->charmap_data[i].refs)
	{
	  /* The glyph is not used.  If it is mapped, we need to
	     remove the mapping to invalidate the glyph.  */
	  if (df->charmap_data[i].locp)
	    {
	      
	      hurd_ihash_locp_remove (&df->charmap, df->charmap_data[i].locp);
	      df->charmap_data[i].locp = NULL;
	    }
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
	      memcpy (df->vga_font[i], df->vga_font[FONT_INDEX_UNKNOWN], 32);
	    }
	  else
	    {
	      /* XXX Take font size and glyph size into account.  */
	      for (int j = 0; j < ((glyph->bbox.height > 32)
				   ? 32 : glyph->bbox.height); j++)
		df->vga_font[i][j]
		  = glyph->bitmap[j * ((glyph->bbox.width + 7) / 8)];
	      if (glyph->bbox.height < 32)
		memset (((char *) df->vga_font[i]) + glyph->bbox.height,
			0, 32 - glyph->bbox.height);
	    }
	}
    }

  if (active_dynafont == df)
    /* Refresh the card info.  */
    dynafont_activate (df);
}
#endif
