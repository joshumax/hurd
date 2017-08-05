/* Implements the ps_fmt type, which describes how to output a user-readable
   version of a proc_stat.

   Copyright (C) 1995, 1996, 1997, 1998 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdio.h>
#include <stdlib.h>
#include <assert-backtrace.h>
#include <string.h>
#include <ctype.h>

#include "ps.h"
#include "common.h"

/* An internal version of ps_fmt_create that takes various extra args.  If
   POSIX is true, parse a posix-std format string.  If ERR_STRING is non-0
   and EINVAL is returned, then a malloced string will be returned in
   ERR_STRING describing why.  */
static error_t
_fmt_create (char *src, int posix, struct ps_fmt_specs *fmt_specs,
	     struct ps_fmt **fmt, char **err_string)
{
  struct ps_fmt *new_fmt;
  int needs = 0;
  int fields_alloced = 10;
  /* Initial values for CLR_FLAGS & INV_FLAGS, so the user may specify
     string-wide defaults.  */
  int global_clr_flags = 0, global_inv_flags = 0;
  struct ps_fmt_field *fields = NEWVEC (struct ps_fmt_field, fields_alloced);
  struct ps_fmt_field *field = fields; /* current last field */

  if (fields == NULL)
    return ENOMEM;

  new_fmt = NEW (struct ps_fmt);
  if (fmt == NULL)
    {
      FREE (fields);
      return ENOMEM;
    }

  /* Make a private copy of SRC so we can mutate it.  */
  new_fmt->src_len = strlen (src) + 1;
  new_fmt->src = strdup (src);
  if (new_fmt->src == NULL)
    {
      FREE (fields);
      FREE (new_fmt);
      return ENOMEM;
    }

  src = new_fmt->src;
  while (*src != '\0')
    {
      char *start = src;

      if (field - fields == fields_alloced)
	/* Time to grow FIELDS to make room for more.  */
	{
	  int offs = field - fields;

	  fields_alloced += 10;
	  fields = GROWVEC (fields, struct ps_fmt_field, fields_alloced);

	  if (fields == NULL)
	    {
	      FREE (new_fmt->src);
	      FREE (new_fmt);
	      return ENOMEM;
	    }

	  field = fields + offs;
	}

      if (posix)
	/* Posix fields are always adjacent to one another.  */
	{
	  field->pfx = " ";
	  field->pfx_len = 1;
	}
      else
	/* Find the text to be reproduced verbatim between the last field and
	   the next one; we'll add this a prefix to FIELD.  */
	{
	  field->pfx = src;
	  while (*src != '\0' && *src != '%')
	    src++;
	  field->pfx_len = src - field->pfx;
	}

      field->spec = NULL;
      field->title = NULL;
      field->width = 0;

      if (*src != '\0')
	/* Another format-spec.  */
	{
	  char *name;
	  int sign = 1;
	  int explicit_width = 0, explicit_precision = 0;
	  int quoted_name = 0;	/* True if the name is quoted with { ... }. */
	  /* Modifications to the spec's flags -- the bits in CLR_FLAGS are
	     cleared from it, and then the bits in INV_FLAGS are inverted.  */
	  int clr_flags = global_clr_flags, inv_flags = global_inv_flags;

	  if (! posix)
	    src++;		/* skip the '%' */

	  /* Set modifiers.   */
	  while (*src == '@' || *src == ':'
		 || *src == '!' || *src == '?' || *src == '^')
	    {
	      if (*src == '@')
		inv_flags ^= PS_FMT_FIELD_AT_MOD; /* Toggle */
	      else if (*src == ':')
		inv_flags ^= PS_FMT_FIELD_COLON_MOD; /* Toggle */
	      else if (*src == '^')
		inv_flags ^= PS_FMT_FIELD_UPCASE_TITLE; /* Toggle */
	      else if (*src == '!')
		{		/* Set */
		  clr_flags |= PS_FMT_FIELD_KEEP;
		  inv_flags |= PS_FMT_FIELD_KEEP;
		}
	      else if (*src == '?')
		{		/* Clear */
		  clr_flags |= PS_FMT_FIELD_KEEP;
		  inv_flags &= ~PS_FMT_FIELD_KEEP;
		}
	      src++;
	    }

	  /* Read an explicit field width.  */
	  field->width = 0;
	  if (*src == '-')
	    sign = -1, src++;
	  while (isdigit (*src))
	    {
	      field->width = field->width * 10 + (*src++ - '0');
	      explicit_width = TRUE;
	    }

	  /* Read an explicit field precision.  */
	  field->precision = 0;
	  if (*src == '.')
	    while (isdigit (*++src))
	      {
		field->precision = field->precision * 10 + (*src - '0');
		explicit_precision = 1;
	      }

	  /* Skip `{' between optional width and spec name.  */
	  if (*src == '{')
	    {
	      src++;
	      quoted_name = 1;
	    }
	  else if (!isalnum (*src) && *src != '_')
	    /* This field spec doesn't have a name, so use its flags fields
	       to set the global ones, and skip it.  */
	    {
	      /* if we didn't use any chars, don't loop indefinitely */
	      if (src == start)
		{
		  if (err_string)
		    asprintf (err_string, "%s: Unknown format spec", src);

		  FREE (new_fmt->src);
		  FREE (new_fmt);
		  FREE (fields);

		  return EINVAL;
		}

	      global_clr_flags = clr_flags;
	      global_inv_flags = inv_flags;
	      continue;
	    }

	  name = src;

	  if (posix)
	    /* Posix-style field spec: `NAME' or `NAME=TITLE'.  Only commas
	       can separate fields.  */
	    {
	      int stop = quoted_name ? '}' : ',';
	      while (*src != '\0' && *src != stop && *src != '=')
		src++;
	      if (*src == '=')
		/* An explicit title.  */
		{
		  *src++ = '\0'; /* NUL-terminate NAME.  */
		  field->title = src;
		  while (*src != '\0' && *src != stop)
		    src++;
		}

	      if (*src)
	        *src++ = '\0';	/* NUL terminate NAME. */
	    }
	  else
	    /* A gnu-style field spec: `NAME' or `NAME:TITLE'.  */
	    {
	      while (quoted_name
		     ? (*src != '\0' && *src != '}' && *src != ':')
		     : (isalnum (*src) || *src == '_'))
		src++;
	      if (quoted_name && *src == ':')
		/* An explicit title.  */
		{
		  *src++ = '\0'; /* NUL-terminate SRC.  */
		  field->title = src;
		  while (*src != '\0' && *src != '}')
		    src++;
		}

	      /* Move the name down one byte (we know there's room, at least
		 the leading `%') so that we have room to NUL-terminate the
		 name for which we're searching.  We also adjust any pointers
		 into this spec-string accordingly.  */
	      bcopy (name, name - 1, src - name);
	      name--;
	      if (field->title)
		field->title--;

	      /* Now that we've made room, do the termination of NAME.  */
	      src[-1] = '\0';
	    }

	  field->spec = ps_fmt_specs_find (fmt_specs, name);
	  if (! field->spec)
	    /* Failed to find any named spec called NAME.  */
	    {
	      if (err_string)
		asprintf (err_string, "%s: Unknown format spec", name);

	      FREE (new_fmt->src);
	      FREE (fields);
	      FREE (new_fmt);

	      return EINVAL;
	    }

	  if (! field->title)
	    {
	      /* No explicit title specified in the fmt string.  */
	      if (field->spec->title)
		field->title = field->spec->title; /* But the spec has one.  */
	      else
		field->title = field->spec->name; /* Just use field name.  */
	    }

	  /* Add FIELD's required pstat_flags to FMT's set */
	  needs |= ps_getter_needs (ps_fmt_spec_getter (field->spec));

	  if (! explicit_width)
	    field->width = field->spec->width;
	  if (! explicit_precision)
	    field->precision = field->spec->precision;

	  field->flags = (field->spec->flags & ~clr_flags) ^ inv_flags;

	  if (quoted_name && *src == '}')
	    /* Skip optional trailing `}' after the spec name.  */
	    src++;
	  if (posix)
	    /* Skip interfield noise.  */
	    {
	      if (*src == ',')
		src++;
	      while (isspace (*src))
		src++;
	    }

	  /* Remember the width's sign (we put it here after possibly using a
	     default width so that the user may include a `-' with no width
	     to flip the justification of the default width).  */
	  field->width *= sign;

	  {
	    /* Force the field to be wide enough to hold the title.  */
	    int width = field->width;
	    int tlen = strlen (field->title);
	    if (width != 0 && tlen > ABS (width))
	      field->width = (width > 0 ? tlen : -tlen);
	  }
	}

      field++;
    }

  new_fmt->fields = fields;
  new_fmt->num_fields = field - fields;
  new_fmt->needs = needs;
  new_fmt->inapp = posix ? "-" : 0;
  new_fmt->error = "?";

  *fmt = new_fmt;

  return 0;
}

/* Make a PS_FMT_T by parsing the string SRC, searching for any named
   field specs in FMT_SPECS, and returning the result in FMT.  If a memory
   allocation error occurs, ENOMEM is returned.  If SRC contains an unknown
   field name, EINVAL is returned.  Otherwise 0 is returned.  If POSIX is
   true, a posix-style format string is parsed, otherwise see ps.h for an
   explanation of how FMT is derived from SRC.  */
error_t
ps_fmt_create (char *src, int posix, struct ps_fmt_specs *fmt_specs,
	       struct ps_fmt **fmt)
{
  return _fmt_create (src, posix, fmt_specs, fmt, 0);
}

/* Given the same arguments as a previous call to ps_fmt_create that returned
   an error, this function returns a malloced string describing the error.  */
void
ps_fmt_creation_error (char *src, int posix, struct ps_fmt_specs *fmt_specs,
		       char **error)
{
  struct ps_fmt *fmt;
  error_t err = _fmt_create (src, posix, fmt_specs, &fmt, error);
  if (err != EINVAL)		/* ? */
    asprintf (error, "%s", strerror (err));
  if (! err)
    ps_fmt_free (fmt);
}

/* Free FMT, and any resources it consumes.  */
void
ps_fmt_free (struct ps_fmt *fmt)
{
  FREE (fmt->src);
  FREE (fmt->fields);
  FREE (fmt);
}

/* Return a copy of FMT in COPY, or an error.  This is useful if, for
   instance, you would like squash a format without destroying the original.  */
error_t
ps_fmt_clone (struct ps_fmt *fmt, struct ps_fmt **copy)
{
  struct ps_fmt *new = NEW (struct ps_fmt);
  struct ps_fmt_field *fields = NEWVEC (struct ps_fmt_field, fmt->num_fields);
  char *src = malloc (fmt->src_len);

  if (!new || !fields || !src)
    {
      free (new);
      free (fields);
      free (src);
      return ENOMEM;
    }

  bcopy (fmt->src, src, fmt->src_len);
  bcopy (fmt->fields, fields, fmt->num_fields * sizeof (struct ps_fmt_field));

  new->fields = fields;
  new->num_fields = fmt->num_fields;
  new->src = src;
  new->src_len = fmt->src_len;
  new->inapp = fmt->inapp;
  new->error = fmt->error;
  *copy = new;

  return 0;
}

/* Write an appropriate header line for FMT, containing the titles of all its
   fields appropriately aligned with where the values would be printed, to
   STREAM (without a trailing newline).  If count is non-NULL, the total
   number number of characters output is added to the integer it points to.
   If any fatal error occurs, the error code is returned, otherwise 0.  */
error_t
ps_fmt_write_titles (struct ps_fmt *fmt, struct ps_stream *stream)
{
  error_t err = 0;
  struct ps_fmt_field *field = ps_fmt_fields (fmt);
  int left = ps_fmt_num_fields (fmt);

  while (left-- > 0 && !err)
    {
      const char *pfx = ps_fmt_field_prefix (field);
      int pfx_len = ps_fmt_field_prefix_length (field);

      if (pfx_len > 0)
	err = ps_stream_write (stream, pfx, pfx_len);

      if (ps_fmt_field_fmt_spec (field) != NULL && !err)
	{
	  const char *title = ps_fmt_field_title (field) ?: "??";
	  int width = ps_fmt_field_width (field);

	  if (field->flags & PS_FMT_FIELD_UPCASE_TITLE)
	    {
	      int len = strlen (title), i;
	      char upcase_title[len + 1];
	      for (i = 0; i < len; i++)
		upcase_title[i] = toupper (title[i]);
	      upcase_title[len] = '\0';
	      err = ps_stream_write_field (stream, upcase_title, width);
	    }
	  else
	    err = ps_stream_write_field (stream, title, width);
	}

      field++;
    }

  return err;
}

/* Format a description as instructed by FMT, of the process described by PS
   to STREAM (without a trailing newline).  If count is non-NULL, the total
   number number of characters output is added to the integer it points to.
   If any fatal error occurs, the error code is returned, otherwise 0.  */
error_t
ps_fmt_write_proc_stat (struct ps_fmt *fmt, struct proc_stat *ps, struct ps_stream *stream)
{
  error_t err = 0;
  struct ps_fmt_field *field = ps_fmt_fields (fmt);
  int nfields = ps_fmt_num_fields (fmt);
  ps_flags_t have = ps->flags;
  ps_flags_t inapp = ps->inapp;

  while (nfields-- > 0 && !err)
    {
      const struct ps_fmt_spec *spec = ps_fmt_field_fmt_spec (field);
      const char *pfx = ps_fmt_field_prefix (field);
      int pfx_len = ps_fmt_field_prefix_length (field);

      if (pfx_len > 0)
	err = ps_stream_write (stream, pfx, pfx_len);

      if (spec != NULL && !err)
	{
	  ps_flags_t need = ps_getter_needs (ps_fmt_spec_getter (spec));

	  /* do we have the resources to print this field? */
	  if ((need & have) == need)
	    /* Yup */
	    err = (*spec->output_fn) (ps, field, stream);
	  else if (need & ~have & inapp)
	    /* This field is inappropriate for PS.  */
	    err =
	      ps_stream_write_field (stream, fmt->inapp ?: "", field->width);
	  else
	    /* This field is appropriate, but couldn't be fetched.  */
	    err =
	      ps_stream_write_field (stream, fmt->error ?: "", field->width);
	}

      field++;
    }

  return err;
}

/* Remove those fields from FMT for which the function FN, when called on the
   field, returns true.  Appropriate inter-field characters are also removed:
   those *following* deleted fields at the beginning of the fmt, and those
   *preceding* deleted fields *not* at the beginning. */
void
ps_fmt_squash (struct ps_fmt *fmt, int (*fn)(struct ps_fmt_field *field))
{
  int nfields = fmt->num_fields;
  struct ps_fmt_field *fields = fmt->fields, *field = fields;
  /* As we're removing some fields, we must recalculate the set of ps flags
     needed by all fields.  */
  ps_flags_t need = 0;

  while ((field - fields) < nfields)
    if (field->spec != NULL && (*fn)(field))
      /* Squash this field! */
      {
	/* Save the old prefix, in case we're deleting the first field,
	   and need to prepend it to the next field.  */
	const char *beg_pfx = field->pfx;
	int beg_pfx_len = field->pfx_len;

	nfields--;

	/* Shift down all following fields over this one.  */
	if (nfields > 0)
	  bcopy (field + 1, field,
		(nfields - (field - fields)) * sizeof *field);

	if (field == fields)
	  /* This is the first field, so move its prefix to the
	     following field (overwriting that field's prefix).  This
	     is to ensure that the beginning of the format string is
	     preserved in preference to the middle, as it is more
	     likely to be significant.  */
	  {
	    if (nfields == 0)
	      /* no following fields, so just make a new end field (we're
		 sure to have room, as we just vacated a space).  */
	      {
		nfields++;
		field->pfx = beg_pfx;
		field->pfx_len = beg_pfx_len;
		field->spec = NULL;
	      }
	    else if (field->spec == NULL)
	      /* One following field with only a prefix -- the suffix
		 of the format string.  Tack the prefix on before the
		 suffix so we preserve both the beginning and the end
		 of the format string.  We know there's space in our
		 copy of the source string, because we've just squashed
		 a field which took at least that much room (as it
		 previously contained the same prefix).  */
	      {
		field->pfx -= beg_pfx_len;
		field->pfx_len += beg_pfx_len;
		bcopy (beg_pfx, (char *)field->pfx, beg_pfx_len);
	      }
	    else
	      /* otherwise just replace the next field's prefix with
		 the beginning one */
	      {
		field->pfx = beg_pfx;
		field->pfx_len = beg_pfx_len;
	      }
	  }
      }
    else
      /* don't squash this field, just move to the next one */
      {
	if (field->spec)
	  need |= ps_getter_needs (field->spec->getter);
	field++;
      }

  fmt->num_fields = nfields;
  fmt->needs = need;
}

/* Remove those fields from FMT which would need the proc_stat flags FLAGS.
   Appropriate inter-field characters are also removed: those *following*
   deleted fields at the beginning of the fmt, and those *preceding* deleted
   fields *not* at the beginning.  */
void
ps_fmt_squash_flags (struct ps_fmt *fmt, ps_flags_t flags)
{
  int squashable_field (struct ps_fmt_field *field)
    {
      return field->spec->getter->needs & flags;
    }

  ps_fmt_squash (fmt, squashable_field);
}

/* Try and restrict the number of output columns in FMT to WIDTH.  */
void
ps_fmt_set_output_width (struct ps_fmt *fmt, int width)
{
  struct ps_fmt_field *field = ps_fmt_fields (fmt);
  int nfields = ps_fmt_num_fields (fmt);

  /* We're not very clever about this -- just add up the width of all the
     fields but the last, and if the last has no existing width (as is
     the case in most output formats), give it whatever is left over.  */
  while (--nfields > 0)
    {
      int fw = field->width;
      width -= field->pfx_len + (fw < 0 ? -fw : fw);
      field++;
    }
  if (nfields == 0 && field->width == 0 && width > 0)
    field->width = width - field->pfx_len - 1; /* 1 for the CR. */
}
