/* bdf.h - Parser for the Adobe Glyph Bitmap Distribution Format (BDF).
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann <marcus@gnu.org>.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#ifndef _BDF_H_
#define _BDF_H_

#include <stdio.h>

/* Version 2.2 of the Glyph Bitmap Distribution Format Specification
   was released by Adobe 22 March 1993 and is a superset of version 2.1.

   The format of a BDF file is a human-readable ASCII text file.
   Every line consists of a keyword, which may be followed by one or
   more arguments.  The specification is vague about the exact data
   types of the arguments, so we treat a string as an 8-bit string
   which must not contain a binary null, and a number like an integer
   as an int.  Leading and trailing white space are removed, multiple
   spaces that separate arguments are replaced by a single white
   space, and empty lines are ignored.  */


/* Possible error values returned by the BDF functions.  */
typedef enum
{
  /* No error occurred.  This is guaranteed to be zero.  */
  BDF_NO_ERROR = 0,

  /* A system error occurred.  The caller should consult errno.  */
  BDF_SYSTEM_ERROR,

  /* All following errors indicate that the file is not a valid BDF
     file.  */
  BDF_SYNTAX_ERROR,

  /* An argument is out of range or of an invalid type.  */
  BDF_INVALID_ARGUMENT,

  /* The number of properties, glyphs or bitmap lines is
     incorrect.  */
  BDF_COUNT_MISMATCH
} bdf_error_t;

/* Return a statically allocated string describing the error value ERR.  */
const char *bdf_strerror (bdf_error_t err);


/* A property in the BDF font file is an unspecified KEYWORD VALUE
   pair in a line after STARTPROPERTIES and before ENDPROPERTIES.
   VALUE can be an integer or a string in double quotes (two
   consecutives double quotes can be used to include a double quote in
   the string.  */
struct bdf_property
{
  /* The property name.  */
  char *name;

  /* The type indicates which member of the union is valid.  */
  enum { BDF_PROPERTY_NUMBER, BDF_PROPERTY_STRING } type;
  union
  {
    int number;
    char *string;
  } value;
};
typedef struct bdf_property *bdf_property_t;

/* The bounding box of a font or a glyph within.  */
struct bdf_bbox
{
  int width;
  int height;
  int offx;
  int offy;
};

/* A vector, used for displacement and resolution.  */
struct bdf_vector
{
  int x;
  int y;
};

/* A single glyph in the font.  */
struct bdf_glyph
{
  /* The name of the glyph.  */
  char *name;

  /* The Adode Standard Encoding of the glyph, or -1 if not
     available.  */
  int encoding;

  /* If encoding is -1, internal_encoding contains the internal
     encoding of the glyph.  The internal encoding is private to the
     font and the application.  */
  int internal_encoding;

  /* The bounding box of the glyph.  The width of the bounding box,
     divided by 8 (rounding up), specifies how many bytes are used in
     the bitmap for each row of the glyph.  The height specifies the
     number of rows.  */
  struct bdf_bbox bbox;

  /* The bitmap of the glyph, row-by-row from top to bottom, and
     byte-by-byte within a row from left to right.  Within a byte, the
     most significant bit is left in the glyph.  */
  unsigned char *bitmap;

  /* If the writing direction is horizontal, SWIDTH and DWIDTH are
     relevant.  If the writing direction is vertical, SWIDTH1, DWIDTH1
     and VVECTOR are relevant.  Relevant values have to be specified
     here, or font-wide in the global section.  A global value can be
     overridden for individual glyphs.  */
  int has_swidth : 1;
  int has_dwidth : 1;
  int has_swidth1 : 1;
  int has_dwidth1 : 1;
  int has_vvector : 1;
  struct bdf_vector swidth;
  struct bdf_vector dwidth;
  struct bdf_vector swidth1;
  struct bdf_vector dwidth1;
  struct bdf_vector vvector;
};

/* A font is a set of glyphs with some font-wide attributes.  */
struct bdf_font
{
  /* The version of the font format.  Should be 2.1 or 2.2.  It is
     split up into major and minor component to make precise
     comparison possible.  */
  int version_maj;
  int version_min;

  /* The font name.  */
  char *name;

  /* The content version, if available.  The content version indicates
     the revision of the appearance of the glyphs within the font.  */
  int has_content_version : 1;
  int content_version;

  /* The point size of the font in device pixels.  */
  int point_size;

  /* The resolution of the display the font is intended for (dpi).
     This is needed if you want to scale the glyphs for a different
     device than they were intended for.  */
  int res_x;
  int res_y;

  /* The global bounding box parameters.  */
  struct bdf_bbox bbox;

  int __properties_allocated;
  int properties_count;
  struct bdf_property *properties;
  int __glyphs_allocated;
  int glyphs_count;
  struct bdf_glyph *glyphs;

  /* The metricsset.  If 0, the font has a horizontal writing
     direction.  If 1, the font has a vertical writing direction.  If
     2, the font has a mixed writing direction.  */
  int metricsset;

  /* The following have the same meaning as the corresponding members
     in the glyph structure and specify a font wide default which must
     be used if the glyph does not provide its own values.  */
  int has_swidth : 1;
  int has_dwidth : 1;
  int has_swidth1 : 1;
  int has_dwidth1 : 1;
  int has_vvector : 1;
  struct bdf_vector swidth;
  struct bdf_vector dwidth;
  struct bdf_vector swidth1;
  struct bdf_vector dwidth1;
  struct bdf_vector vvector;
};
typedef struct bdf_font *bdf_font_t;


/* Read the font from stream FILE, and return it in FONT.  If
   LINECOUNT is not zero, it will contain the number of lines in the
   file at success, and the current line an error occurred at
   failure.  */
bdf_error_t bdf_read (FILE *file, bdf_font_t *font, int *linecount);

/* Destroy the BDF font object and release all associated
   resources.  */
void bdf_destroy (bdf_font_t font);

/* Create a new font object with the specified mandatory
   parameters.  */
bdf_error_t bdf_new (bdf_font_t *font, int version_maj, int version_min,
		     const char *name, int point_size, int res_x, int res_y,
		     int bbox_width, int bbox_height, int bbox_offx,
		     int bbox_offy, int metricsset);

/* Set the SWIDTH of the glyph GLYPH in font FONT to X and Y.  If
   glyph is negativ, the default for the font will be set.  */
bdf_error_t bdf_set_swidth (bdf_font_t font, int glyph, int x, int y);

/* Set the DWIDTH of the glyph GLYPH in font FONT to X and Y.  If
   glyph is negativ, the default for the font will be set.  */
bdf_error_t bdf_set_dwidth (bdf_font_t font, int glyph, int x, int y);

/* Set the SWIDTH1 of the glyph GLYPH in font FONT to X and Y.  If
   glyph is negativ, the default for the font will be set.  */
bdf_error_t bdf_set_swidth1 (bdf_font_t font, int glyph, int x, int y);

/* Set the DWIDTH1 of the glyph GLYPH in font FONT to X and Y.  If
   glyph is negativ, the default for the font will be set.  */
bdf_error_t bdf_set_dwidth1 (bdf_font_t font, int glyph, int x, int y);

/* Set the VVECTOR of the glyph GLYPH in font FONT to X and Y.  If
   glyph is negativ, the default for the font will be set.  */
bdf_error_t bdf_set_vvector (bdf_font_t font, int glyph, int x, int y);

/* Add a new string property to the font FONT.  */
bdf_error_t bdf_add_string_property (bdf_font_t font, const char *name,
				     const char *value);

/* Add a new number property to the font FONT.  */
bdf_error_t bdf_add_number_property (bdf_font_t font, const char *name,
				     int value);

/* Add a new glyph with the specified parameters to the font FONT.  If
   encoding is -1, internal_encoding specifies the internal
   encoding.  All other parameters are mandatory.  */
bdf_error_t bdf_add_glyph (bdf_font_t font, const char *name, int encoding,
			   int internal_encoding, int bbox_width,
			   int bbox_height, int bbox_offx, int bbox_offy,
			   const unsigned char *bitmap);

/* Write the font FONT in BDF format to stream FILEP.  */
bdf_error_t bdf_write (FILE *filep, bdf_font_t font);

/* The function returns -1 if the encoding of glyph A is lower than
   the encoding of glyph B, 1 if it is the other way round, and 0 if
   the encoding of both glyphs is the same.  */
int bdf_compare_glyphs (const void *a, const void *b);

/* Sort the glyphs in the font FONT.  This must be called before using
   bdf_find_glyphs, after the font has been created or after new
   glyphs have been added to the font.  */
void bdf_sort_glyphs (bdf_font_t font);

/* Find the glyph with the encoding ENC (and INTERNAL_ENC, if ENC is
   -1) in the font FONT.  Requires that the glyphs in the font are
   sorted.  */
struct bdf_glyph *bdf_find_glyph (bdf_font_t font, int enc, int internal_enc);

#endif	/* _BDF_H_ */
