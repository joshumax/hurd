/* bdf.c - Parser for the Adobe Glyph Bitmap Distribution Format (BDF).
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <ctype.h>
#include <stdarg.h>
#include <search.h>

#include "bdf.h"


/* Return a statically allocated string describing the BDF error value
   ERR.  */
const char *
bdf_strerror (bdf_error_t err)
{
  switch (err)
    {
    case BDF_NO_ERROR:
      return "Success";
    case BDF_SYSTEM_ERROR:
      return "System error";
    case BDF_SYNTAX_ERROR:
      return "Syntax error";
    case BDF_INVALID_ARGUMENT:
      return "Invalid Argument";
    case BDF_COUNT_MISMATCH:
      return "Count mismatch";
    default:
      return "Unknown error";
    }
}


/* Copy the string starting from ARG and return the pointer to it in
   STRING.  If QUOTED is true, outer double quotes are stripped, and
   two consecutive double quotes within the string are replaced by one
   douple quotes.  */
static bdf_error_t
parse_string (char *arg, char **string, int quoted)
{
  if (quoted)
    {
      char *value = ++arg;
      do
	{
	  value = strchr (value, '"');
	  if (!value)
	    return BDF_INVALID_ARGUMENT;
	  else if (*(value + 1) == '"')
	    {
	      char *copyp = value++;
	      while (*(++copyp))
		*(copyp - 1) = *copyp;
	      *(copyp - 1) = 0;
	    }
	}
      while (*value != '"' || *(value + 1) == '"');
      *value = 0;
    }

  *string = strdup (arg);
  if (!*string)
    {
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }
  return 0;
}


/* Parse the string STR for format TEMPLATE, and require an exact
   match.  Set err if a parsing error occurs.  TEMPLATE must be a
   string constant.  */
#define parse_template(str, template, rest...)				\
  do									\
    {									\
      int parse_template_count = -1;					\
      sscanf (str, template " %n", rest, &parse_template_count);	\
      if (parse_template_count == -1 || *(str + parse_template_count))	\
        err = BDF_SYNTAX_ERROR;						\
    }									\
  while (0)


#define hex2nr(c) (((c) >= '0' && (c) <= '9') ? (c) - '0'		\
		   : (((c) >= 'a' && (c) <= 'f') ? (c) - 'a' + 10	\
		      : (((c) >= 'A' && (c) <= 'F') ? (c) - 'A' + 10 : 0)))

/* Convert a two-digit hex number starting from LINE to a char and
   return the result in BYTE.  */
static bdf_error_t
parse_hexbyte (char *line, unsigned char *byte)
{
  if (!isxdigit (*line) || !isxdigit(*(line + 1)))
    return BDF_SYNTAX_ERROR;
  else
    *byte = (hex2nr (*line) << 4) + hex2nr (*(line + 1));
  return 0;
}


/* Like getline(), but keeps track of line count in COUNT, skips
   COMMENT lines, and removes whitespace at the beginning and end of a
   line.  */
static int
next_line (char **line, size_t *size, FILE *file, int *count)
{
  int len;

  do
    {
      len = getline (line, size, file);
      if (len >= 0)
	{
	  char *cline = *line;
	  if (count)
	    (*count)++;
	  if (!strncmp (cline, "COMMENT", 7))
	    len = 0;
	  else
	    while (len > 0 && (cline[len - 1] == '\n' || cline[len - 1] == '\r'
			       || cline[len - 1] == ' '
			       || cline[len - 1] == '\t'))
	      cline[--len] = 0;
	}
    }
  while (len <= 0 && !feof (file) && !ferror (file));
  return len;
}


/* Isolate the next white-space separated argument from the current
   line, and set ARGP to the beginning of the next argument.  It is an
   error if there is no further argument.  */
static bdf_error_t
find_arg (char **argp)
{
  char *arg = *argp;

  arg = strchr (arg, ' ');
  if (arg)
    {
      *(arg++) = 0;
      while (*arg == ' ' || *arg == '\t')
	arg++;
    }
  if (!arg || !*arg)
    return BDF_SYNTAX_ERROR;
  *argp = arg;
  return 0;
}


/* Read the font from stream FILE, and return it in FONT.  If
   LINECOUNT is not zero, it will contain the number of lines in the
   file at success, and the line an error occurred at failure.  */
bdf_error_t
bdf_read (FILE *filep, bdf_font_t *font, int *linecount)
{
  bdf_error_t err = 0;
  char *line = 0;
  size_t line_size = 0;
  int len;
  int done = 0;
  bdf_font_t bdf;
  struct
  {
    /* Current line.  */
    enum { START, FONT, PROPERTIES, GLYPHS, GLYPH, BITMAP } location;
    /* The number of properties parsed so far.  */
    int properties;
    /* The number of glyphs parsed so far.  */
    int glyphs;

    /* True if we have seen a SIZE keyword so far.  */
    unsigned int has_size : 1;
    /* True if we have seen a FONTBOUNDINGBOX keyword so far.  */
    unsigned int has_fbbx : 1;
    /* True if we have seen a METRICSSET keyword so far.  */
    unsigned int has_metricsset : 1;

    /* Current glyph.  */
    struct bdf_glyph *glyph;
    /* True if we have seen an ENCODING keyword for the glyph.  */
    unsigned int glyph_has_encoding : 1;
    /* True if we have seen an BBX keyword for the glyph.  */
    unsigned int glyph_has_bbx : 1;
    /* Width of the glyph in bytes.  */
    unsigned int glyph_bwidth;
    /* Height of the glyph in pixel.  */
    unsigned int glyph_bheight;
    /* How many bitmap lines have been parsed already.  */
    unsigned int glyph_blines;
  } parser = { location: START, properties: 0, glyphs: 0,
	       has_size: 0, has_fbbx: 0 };

  bdf = calloc (1, sizeof *bdf);
  if (!bdf)
    {
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }

  if (linecount)
    *linecount = 0;
  
  while (!err && (len = next_line (&line, &line_size, filep, linecount)) >= 0)
    {
      switch (parser.location)
	{
	case START:
	  {
	    /* This is the start of the file, only comments are allowed
	       until STARTFONT is encountered.  */
	    char *arg = line;
	    err = find_arg (&arg);

	    if (err)
	      continue;
	    if (!strcmp (line, "STARTFONT"))
	      {
		char *minor = strchr (arg, '.');
		if (minor)
		  *(minor++) = '\0';
		parser.location = FONT;
	        parse_template (arg, "%i", &bdf->version_maj);
		if (minor)
		  parse_template (minor, "%i", &bdf->version_min);
		else
		  bdf->version_min = 0;
	      }
	    else
	      err = BDF_SYNTAX_ERROR;
	  }
	  break;

	case FONT:
	  {
	    /* This is the global header before the CHARS.  */
	    char *arg = line;
	    err = find_arg (&arg);

	    if (err)
	      continue;
	    else if (!bdf->has_content_version
		     && !strcmp (line, "CONTENTVERSION"))
	      {
		bdf->has_content_version = 1;
		parse_template (arg, "%i", &bdf->content_version);
	      }
	    else if (!bdf->name && !strcmp (line, "FONT"))
	      err = parse_string (arg, &bdf->name, 0);
	    else if (!parser.has_size
		     && !strcmp (line, "SIZE"))
	      {
		parser.has_size = 1;
		parse_template (arg, "%i%i%i", &bdf->point_size,
				&bdf->res_x, &bdf->res_y);
	      }
	    else if (!parser.has_fbbx && !strcmp (line, "FONTBOUNDINGBOX"))
	      {
		parser.has_fbbx = 1;
		parse_template (arg, "%i%i%i%i", &bdf->bbox.width,
				&bdf->bbox.height, &bdf->bbox.offx,
				&bdf->bbox.offy);
	      }
	    else if (!parser.has_metricsset && !strcmp (line, "METRICSSET"))
	      {
		parser.has_metricsset = 1;
		parse_template (arg, "%i", &bdf->metricsset);
		if (!err && (bdf->metricsset < 0
			     || bdf->metricsset > 2))
		  err = BDF_INVALID_ARGUMENT;
	      }
	    else if (!bdf->properties && !strcmp (line, "STARTPROPERTIES"))
	      {
		parser.location = PROPERTIES;
		parse_template (arg, "%i", &bdf->properties_count);
		if (!err && (bdf->properties_count <= 0))
		  err = BDF_INVALID_ARGUMENT;
		if (err)
		  goto leave;
		bdf->__properties_allocated = bdf->properties_count;
		bdf->properties = calloc (bdf->properties_count,
					  sizeof (struct bdf_property));
		if (!bdf->properties)
		  {
		    errno = ENOMEM;
		    err = BDF_SYSTEM_ERROR;
		  }
	      }
	    else if (!strcmp (line, "CHARS"))
	      {
		/* This marks the end of the first section, so check
		   for mandatory global options.  */
		if (!bdf->name || !parser.has_size || !parser.has_fbbx)
		  err = BDF_SYNTAX_ERROR;
		else
		  {
		    parser.location = GLYPHS;
		    parse_template (arg, "%i", &bdf->glyphs_count);
		    if (!err && (bdf->glyphs_count < 0))
		      err = BDF_INVALID_ARGUMENT;
		    if (!err)
		      {
			bdf->__glyphs_allocated = bdf->glyphs_count;
			bdf->glyphs = calloc (bdf->glyphs_count,
					      sizeof (struct bdf_glyph));
			if (!bdf->glyphs)
			  {
			    errno = ENOMEM;
			    err = BDF_SYSTEM_ERROR;
			  }
		      }
		  }
	      }
	    else if (!bdf->has_swidth && !strcmp (line, "SWIDTH"))
	      {
		bdf->has_swidth = 1;
		parse_template (arg, "%i%i", &bdf->swidth.x, &bdf->swidth.y);
	      }
	    else if (!bdf->has_dwidth && !strcmp (line, "DWIDTH"))
	      {
		bdf->has_dwidth = 1;
		parse_template (arg, "%i%i", &bdf->dwidth.x, &bdf->dwidth.y);
	      }
	    else if (!bdf->has_swidth1 && !strcmp (line, "SWIDTH1"))
	      {
		bdf->has_swidth1 = 1;
		parse_template (arg, "%i%i", &bdf->swidth1.x, &bdf->swidth1.y);
	      }
	    else if (!bdf->has_dwidth1 && !strcmp (line, "DWIDTH1"))
	      {
		bdf->has_dwidth1 = 1;
		parse_template (arg, "%i%i", &bdf->dwidth1.x, &bdf->dwidth1.y);
	      }
	    else if (!bdf->has_vvector && !strcmp (line, "VVECTOR"))
	      {
		bdf->has_vvector = 1;
		parse_template (arg, "%i%i", &bdf->vvector.x, &bdf->vvector.y);
	      }
	    else
	      err = BDF_SYNTAX_ERROR;
	  }
	  break;

	case PROPERTIES:
	  /* This is the property list in the global header, between
	     STARTPROPERTIES and ENDPROPERTIES.  */
	  if (!strcmp (line, "ENDPROPERTIES"))
	    {
	      parser.location = FONT;
	      if (parser.properties != bdf->properties_count)
		err = BDF_COUNT_MISMATCH;
	    }
	  else
	    {
	      if (parser.properties == bdf->properties_count)
		err = BDF_COUNT_MISMATCH;
	      else
		{
		  struct bdf_property *prop
		    = &bdf->properties[parser.properties++];
		  char *arg = line;
		  
		  err = find_arg (&arg);
		  if (err)
		    continue;
		  
		  err = parse_string (line, &prop->name, 0);
		  if (!err)
		    {
		      if (*arg == '"')
			{
			  prop->type = BDF_PROPERTY_STRING;
			  err = parse_string (arg, &prop->value.string, 1);
			}
		      else
			{
			  prop->type = BDF_PROPERTY_NUMBER;
  			  parse_template (arg, "%i", &prop->value.number);
			}
		    }
		}
	    }
	  break;
	  
	case GLYPHS:
	  /* This is the second section of the file, containing the
	     glyphs.  */
	  if (!strcmp (line, "ENDFONT"))
	    {
	      if (parser.glyphs != bdf->glyphs_count)
		err = BDF_COUNT_MISMATCH;
	      done = 1;
	    }
	  else
	    {
	      char *arg = line;

	      err = find_arg (&arg);
	      if (err)
		continue;
	      else if (!strcmp (line, "STARTCHAR"))
		{
		  if (parser.glyphs == bdf->glyphs_count)
		    err = BDF_COUNT_MISMATCH;

		  parser.location = GLYPH;
		  parser.glyph = &bdf->glyphs[parser.glyphs++];
		  parser.glyph_has_encoding = 0;
		  parser.glyph_has_bbx = 0;
		  parser.glyph_blines = 0;
		  err = parse_string (arg, &(parser.glyph->name), 0);
		}
	      else
		err = BDF_SYNTAX_ERROR;
	    }
	  break;

	case GLYPH:
	  /* This is a glyph, but not its bitmap yet.  */
	  if (!strcmp (line, "BITMAP"))
	    {
	      if (!parser.glyph_has_encoding
		  || !parser.glyph_has_bbx

		  /* In writing mode 0, SWIDTH and DWIDTH are mandatory.  */
		  || (bdf->metricsset != 1
		      && (!(parser.glyph->has_swidth || bdf->has_swidth)
			  || !(parser.glyph->has_dwidth || bdf->has_dwidth)))

		  /* In writing mode 1, SWIDTH1, DWIDTH1 and VVECTOR
		     are mandatory.  */
		  || (bdf->metricsset != 0
		      && (!(parser.glyph->has_swidth1 || bdf->has_swidth1)
			  || !(parser.glyph->has_dwidth1 || bdf->has_dwidth1)
			  || !(parser.glyph->has_vvector
			       || bdf->has_vvector))))
		err = BDF_SYNTAX_ERROR;

	      parser.location = BITMAP;
	      parser.glyph->bitmap = malloc (parser.glyph_bwidth
					     * parser.glyph_bheight);
	      if (!parser.glyph->bitmap)
		{
		  errno = ENOMEM;
		  err = BDF_SYSTEM_ERROR;
		}
	    }
	  else
	    {
	      char *arg = line;

	      err = find_arg (&arg);
	      if (err)
		continue;
	      else if (!parser.glyph_has_encoding
		       && !strcmp (line, "ENCODING"))
		{
		  parser.glyph_has_encoding = 1;
		  parse_template (arg, "%i", &parser.glyph->encoding);
		  if (err == BDF_SYNTAX_ERROR)
		    {
		      err = 0;
		      parse_template (arg, "%i%i", &parser.glyph->encoding,
				      &parser.glyph->internal_encoding);
		      if (!err && parser.glyph->encoding != -1)
			err = BDF_SYNTAX_ERROR;
		    }
		}
	      else if (!parser.glyph_has_bbx && !strcmp (line, "BBX"))
		{
		  parser.glyph_has_bbx = 1;
		  parse_template (arg, "%i%i%i%i", &parser.glyph->bbox.width,
				  &parser.glyph->bbox.height,
				  &parser.glyph->bbox.offx,
				  &parser.glyph->bbox.offy);
		  if (!err)
		    {
		      parser.glyph_bwidth = (parser.glyph->bbox.width + 7) / 8;
		      parser.glyph_bheight = parser.glyph->bbox.height;
		    }
		}
	      else if (!parser.glyph->has_swidth && !strcmp (line, "SWIDTH"))
		{
		  parser.glyph->has_swidth = 1;
		  parse_template (arg, "%i%i", &parser.glyph->swidth.x,
				  &parser.glyph->swidth.y);
		}
	      else if (!parser.glyph->has_dwidth && !strcmp (line, "DWIDTH"))
		{
		  parser.glyph->has_dwidth = 1;
		  parse_template (arg, "%i%i", &parser.glyph->dwidth.x,
				  &parser.glyph->dwidth.y);
		}
	      else if (!parser.glyph->has_swidth1 && !strcmp (line, "SWIDTH1"))
		{
		  parser.glyph->has_swidth1 = 1;
		  parse_template (arg, "%i%i", &parser.glyph->swidth1.x,
				  &parser.glyph->swidth1.y);
		}
	      else if (!parser.glyph->has_dwidth1 && !strcmp (line, "DWIDTH1"))
		{
		  parser.glyph->has_dwidth1 = 1;
		  parse_template (arg, "%i%i", &parser.glyph->dwidth1.x,
				  &parser.glyph->dwidth1.y);
		}
	      else if (!parser.glyph->has_vvector && !strcmp (line, "VVECTOR"))
		{
		  parser.glyph->has_vvector = 1;
		  parse_template (arg, "%i%i", &parser.glyph->vvector.x,
				  &parser.glyph->vvector.y);
		}
	      else
		err = BDF_SYNTAX_ERROR;
	    }
	  break;

	case BITMAP:
	  /* This is the bitmap of a glyph.  */
	  if (!strcmp (line, "ENDCHAR"))
	    {
	      if (parser.glyph_blines != parser.glyph_bheight)
		err = BDF_COUNT_MISMATCH;
	      parser.location = GLYPHS;
	      parser.glyph = 0;
	    }
	  else
	    {
	      if (strlen (line) != 2 * parser.glyph_bwidth)
		err = BDF_SYNTAX_ERROR;
	      else if (parser.glyph_blines == parser.glyph_bheight)
		err = BDF_COUNT_MISMATCH;
	      else
		{
		  char *number = line;
		  unsigned char *bline = parser.glyph->bitmap
		    + parser.glyph_bwidth * parser.glyph_blines++;

		  do
		    {
		      err = parse_hexbyte (number, bline);
		      number += 2;
		      bline++;
		    }
		  while (!err && *number);
		}
	    }
	  break;
	}
    }
  while (!err && !done && !feof (filep) && !ferror (filep));

 leave:
  if (ferror (filep))
    err = ferror (filep);
  if (err)
    free (bdf);
  else
    *font = bdf;
  return err;
}


/* Destroy the BDF font object and release all associated
   resources.  */
void
bdf_destroy (bdf_font_t font)
{
  int i;
  for (i = 0; i < font->glyphs_count; i++)
    free (font->glyphs[i].name);
  free (font->glyphs);
  for (i = 0; i < font->properties_count; i++)
    {
      free (font->properties[i].name);
      if (font->properties[i].type == BDF_PROPERTY_STRING)
	free (font->properties[i].value.string);
    }
  free (font->properties);
  free (font->name);
}


bdf_error_t
bdf_new (bdf_font_t *font, int version_maj, int version_min,
	 const char *name, int point_size, int res_x, int res_y,
	 int bbox_width, int bbox_height, int bbox_offx, int bbox_offy,
	 int metricsset)
{
  bdf_font_t bdf;

  if (!font
      || (version_maj != 2)
      || (version_min != 1 && version_min != 2)
      || !name)
    return BDF_INVALID_ARGUMENT;

  bdf = calloc (1, sizeof *bdf);
  if (!bdf)
    {
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }

  bdf->version_maj = version_maj;
  bdf->version_min = version_min;
  bdf->name = strdup (name);
  if (!name)
    {
      free (bdf);
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }

  bdf->point_size = point_size;
  bdf->res_x = res_x;
  bdf->res_y = res_y;
  bdf->bbox.width = bbox_width;
  bdf->bbox.height = bbox_height;
  bdf->bbox.offx = bbox_offx;
  bdf->bbox.offy = bbox_offy;
  bdf->metricsset = metricsset;
  *font = bdf;
  return 0;
}


#define bdf_set_something(what)					\
bdf_error_t							\
bdf_set_##what (bdf_font_t font, int glyph, int x, int y)	\
{								\
  if (x < 0 || y < 0 || glyph > font->glyphs_count - 1)		\
    return BDF_INVALID_ARGUMENT;				\
  if (glyph < 0)						\
    {								\
      font->has_##what = 1;					\
      font->what.x = x;						\
      font->what.y = y;						\
    }								\
  else								\
    {								\
      font->glyphs[glyph].has_##what = 1;			\
      font->glyphs[glyph].what.x = x;				\
      font->glyphs[glyph].what.y = y;				\
    }								\
  return 0;							\
}

bdf_set_something (swidth)
bdf_set_something (dwidth)
bdf_set_something (swidth1)
bdf_set_something (dwidth1)
bdf_set_something (vvector)


static bdf_error_t
expand_properties (bdf_font_t font, int count)
{
  if (font->__properties_allocated == font->properties_count)
    {
      struct bdf_property *new;
      new = realloc (font->properties,
		     (font->__properties_allocated + count)
		     * sizeof (struct bdf_property));
      if (!new)
	{
	  errno = ENOMEM;
	  return BDF_SYSTEM_ERROR;
	}
      font->__properties_allocated += count;
      font->properties = new;
    }
  return 0;
}


/* Add a new string property to the font FONT.  */
bdf_error_t
bdf_add_string_property (bdf_font_t font, const char *name, const char *value)
{
  bdf_error_t err;
  struct bdf_property *prop;

  err = expand_properties (font, 16);
  if (err)
    {
      err = expand_properties (font, 1);
      if (err)
	return err;
    }

  prop = &font->properties[font->properties_count];
  prop->type = BDF_PROPERTY_STRING;
  prop->name = strdup (name);
  if (prop->name)
    prop->value.string = strdup (value);
  if (!prop->name || !prop->value.string)
    {
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }
  font->properties_count++;
  return 0;
}


/* Add a new number property to the font FONT.  */
bdf_error_t
bdf_add_number_property (bdf_font_t font, const char *name, int value)
{
  bdf_error_t err;
  struct bdf_property *prop;

  err = expand_properties (font, 16);
  if (err)
    {
      err = expand_properties (font, 1);
      if (err)
	return err;
    }

  prop = &font->properties[font->properties_count];
  prop->type = BDF_PROPERTY_NUMBER;
  prop->name = strdup (name);
  if (!prop->name)
    {    
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }
  prop->value.number = value;
  font->properties_count++;
  return 0;
}


static bdf_error_t
expand_glyphs (bdf_font_t font, int count)
{
  if (font->__glyphs_allocated == font->glyphs_count)
    {
      struct bdf_glyph *new;
      new = realloc (font->glyphs,
		     (font->__glyphs_allocated + count)
		     * sizeof (struct bdf_glyph));
      if (!new)
	{
	  errno = ENOMEM;
	  return BDF_SYSTEM_ERROR;
	}
      font->__glyphs_allocated += count;
      font->glyphs = new;
    }
  return 0;
}


/* Add a new glyph with the specified parameters to the font FONT.  If
   encoding is -1, internal_encoding specifies the internal
   encoding.  All other parameters are mandatory.  */
bdf_error_t
bdf_add_glyph (bdf_font_t font, const char *name, int encoding,
	       int internal_encoding, int bbox_width, int bbox_height,
	       int bbox_offx, int bbox_offy, const unsigned char *bitmap)
{
  bdf_error_t err;
  struct bdf_glyph *glyph;
  int bsize;

  err = expand_glyphs (font, 64);
  if (err)
    {
      err = expand_glyphs (font, 1);
      if (err)
	return err;
    }

  glyph = &font->glyphs[font->glyphs_count];
  memset (glyph, 0, sizeof (*glyph));
  
  glyph->name = strdup (name);
  if (!glyph->name)
    {    
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }
  glyph->encoding = encoding;
  if (encoding == -1)
    glyph->internal_encoding = internal_encoding;
  glyph->bbox.width = bbox_width;
  glyph->bbox.height = bbox_height;
  glyph->bbox.offx = bbox_offx;
  glyph->bbox.offy = bbox_offy;
  bsize = ((bbox_width + 7) / 8) * bbox_height;
  glyph->bitmap = malloc (bsize);
  if (!glyph->bitmap)
    {
      free (glyph->name);
      errno = ENOMEM;
      return BDF_SYSTEM_ERROR;
    }
  memcpy (glyph->bitmap, bitmap, bsize);
  font->glyphs_count++;
  return 0;
}


/* Write the font FONT in BDF format to stream FILEP.  */
bdf_error_t
bdf_write (FILE *filep, bdf_font_t font)
{
  int index;

  if (font->version_maj != 2
      || (font->version_min != 1 && font->version_min != 2))
    return BDF_INVALID_ARGUMENT;
  fprintf (filep, "STARTFONT %i.%i\n", font->version_maj, font->version_min);

  if (!font->name)
    return BDF_INVALID_ARGUMENT;
  fprintf (filep, "FONT %s\n", font->name);
  fprintf (filep, "SIZE %i %i %i\n", font->point_size,
	   font->res_x, font->res_y);
  fprintf (filep, "FONTBOUNDINGBOX %i %i %i %i\n",
	   font->bbox.width, font->bbox.height,
	   font->bbox.offx, font->bbox.offy);
  if (font->has_swidth)
    fprintf (filep, "SWIDTH %i %i\n", font->swidth.x, font->swidth.y);
  if (font->has_dwidth)
    fprintf (filep, "DWIDTH %i %i\n", font->dwidth.x, font->dwidth.y);
  if (font->has_swidth1)
    fprintf (filep, "SWIDTH1 %i %i\n", font->swidth1.x, font->swidth1.y);
  if (font->has_dwidth1)
    fprintf (filep, "DWIDTH1 %i %i\n", font->dwidth1.x, font->dwidth1.y);
  if (font->has_vvector)
    fprintf (filep, "VVECTOR %i %i\n", font->vvector.x, font->vvector.y);
  if (font->properties_count < 0)
    return BDF_INVALID_ARGUMENT;
  /* XXX We always print out a properties block for xmbdfed's sake.  */
  fprintf (filep, "STARTPROPERTIES %i\n", font->properties_count);
  if (font->properties_count > 0)
    {
      fprintf (filep, "STARTPROPERTIES %i\n", font->properties_count);
      for (index = 0; index < font->properties_count; index++)
	{
	  struct bdf_property *prop = &font->properties[index];

	  if (prop->type == BDF_PROPERTY_NUMBER)
	    fprintf (filep, "%s %i\n", prop->name, prop->value.number);
	  else
	    {
	      char *val = prop->value.string;

	      fprintf (filep, "%s \"", prop->name);
	      while (*val)
		{
		  fputc (*val, filep);
		  if (*(val++) == '"')
		    fputc ('"', filep);
		}
	      fprintf (filep, "\"\n");
	    }
	}
    }
  fprintf (filep, "ENDPROPERTIES\n");
  if (font->glyphs_count <= 0)
    return BDF_INVALID_ARGUMENT;
  fprintf (filep, "CHARS %i\n", font->glyphs_count);

  for (index = 0; index < font->glyphs_count; index++)
    {
      struct bdf_glyph *glyph = &font->glyphs[index];
      unsigned char *bitmap;
      int row, col;

      fprintf (filep, "STARTCHAR %s\n", glyph->name);
      if (glyph->encoding != -1)
	fprintf (filep, "ENCODING %i\n", glyph->encoding);
      else
	fprintf (filep, "ENCODING %i %i\n", glyph->encoding,
		 glyph->internal_encoding);
      if (glyph->has_swidth)
	fprintf (filep, "SWIDTH %i %i\n", glyph->swidth.x, glyph->swidth.y);
      if (glyph->has_dwidth)
	fprintf (filep, "DWIDTH %i %i\n", glyph->dwidth.x, glyph->dwidth.y);
      if (glyph->has_swidth1)
	fprintf (filep, "SWIDTH1 %i %i\n", glyph->swidth1.x, glyph->swidth1.y);
      if (glyph->has_dwidth1)
	fprintf (filep, "DWIDTH1 %i %i\n", glyph->dwidth1.x, glyph->dwidth1.y);
      if (glyph->has_vvector)
	fprintf (filep, "VVECTOR %i %i\n", glyph->vvector.x, glyph->vvector.y);
      fprintf (filep, "BBX %i %i %i %i\n", glyph->bbox.width,
	       glyph->bbox.height, glyph->bbox.offx, glyph->bbox.offy);
      fprintf (filep, "BITMAP\n");
      bitmap = glyph->bitmap;
      for (row = 0; row < glyph->bbox.height; row++)
	{
	  for (col = 0; col < (glyph->bbox.width + 7) / 8; col++)
	    fprintf (filep, "%02X", *(bitmap++));
	  fputc ('\n', filep);
	}
      fprintf (filep, "ENDCHAR\n");
    }
  fprintf (filep, "ENDFONT\n");
  if (ferror (filep))
    {
      errno = ferror (filep);
      return BDF_SYSTEM_ERROR;
    }
  return 0;
}


/* The function returns -1 if the encoding of glyph A is lower than
   the encoding of glyph B, 1 if it is the other way round, and 0 if
   the encoding of both glyphs is the same.  */
int
bdf_compare_glyphs (const void *a, const void *b)
{
  struct bdf_glyph *first = (struct bdf_glyph *) a;
  struct bdf_glyph *second = (struct bdf_glyph *) b;

  if (first->encoding < second->encoding)
    return -1;
  else if (first->encoding > second->encoding)
    return 1;
  else
    {
      if (first->encoding == -1)
	{
	  if (first->internal_encoding < second->internal_encoding)
	    return -1;
	  else if (first->internal_encoding > second->internal_encoding)
	    return 1;
	  else
	    return 0;
	}
      else
	return 0;
    }
}


/* Sort the glyphs in the font FONT.  This must be called before using
   bdf_find_glyphs, after the font has been created or after new
   glyphs have been added to the font.  */
void
bdf_sort_glyphs (bdf_font_t font)
{
  qsort (font->glyphs, font->glyphs_count, sizeof (struct bdf_glyph),
	 bdf_compare_glyphs);
}


/* Find the glyph with the encoding ENC (and INTERNAL_ENC, if ENC is
   -1) in the font FONT.  Requires that the glyphs in the font are
   sorted.  */
struct bdf_glyph *
bdf_find_glyph (bdf_font_t font, int enc, int internal_enc)
{
  struct bdf_glyph key = { encoding: enc, internal_encoding: internal_enc };
  return bsearch (&key, font->glyphs, font->glyphs_count,
		  sizeof (struct bdf_glyph), bdf_compare_glyphs);
}
