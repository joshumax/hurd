/* Routines for formatting time

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "timefmt.h"

#define SECOND 	1
#define MINUTE	60
#define HOUR	(60*MINUTE)
#define DAY	(24*HOUR)
#define WEEK 	(7*DAY)
#define MONTH   (31*DAY)	/* Not strictly accurate, but oh well.  */
#define YEAR    (365*DAY)	/* ditto */

/* Returns the number of digits in the integer N.  */
static unsigned
int_len (unsigned n)
{
  unsigned len = 1;
  while (n >= 10)
    {
      n /= 10;
      len++;
    }
  return len;
}

/* Returns TV1 divided by TV2.  */
static unsigned
tv_div (struct timeval *tv1, struct timeval *tv2)
{
  return 
    tv2->tv_sec
      ? tv1->tv_sec / tv2->tv_sec
	: (tv1->tv_usec / tv2->tv_usec
	   + (tv1->tv_sec ? tv1->tv_sec * 1000000 / tv2->tv_usec : 0));
}

/* Returns true if TV is zero.  */
static inline int
tv_is_zero (struct timeval *tv)
{
  return tv->tv_sec == 0 && tv->tv_usec == 0;
}

/* Returns true if TV1 >= TV2.  */
static inline int
tv_is_ge (struct timeval *tv1, struct timeval *tv2)
{
  return
    tv1->tv_sec > tv2->tv_sec
      || (tv1->tv_sec == tv2->tv_sec && tv1->tv_usec >= tv2->tv_usec);
}

/* Format into BUF & BUF_LEN the time interval represented by TV, trying to
   make the result less than WIDTH characters wide.  The number of characters
   used is returned.  */
size_t
fmt_named_interval (struct timeval *tv, size_t width,
		    char *buf, size_t buf_len)
{
  struct tscale
    {
      struct timeval thresh;	/* Minimum time to use this scale. */
      struct timeval unit;	/* Unit this scale is based on.  */
      struct timeval frac_thresh; /* If a emitting a single digit of precision
				     will cause at least this much error, also
				     emit a single fraction digit.  */
      char *sfxs[5];		/* Names to use, in descending length. */
    }
  time_scales[] =
  {
    {{2*YEAR, 0},  {YEAR, 0},  {MONTH, 0},{" years", "years", "yrs", "y", 0 }},
    {{3*MONTH, 0}, {MONTH, 0}, {WEEK, 0}, {" months","months","mo",       0 }},
    {{2*WEEK, 0},  {WEEK, 0},  {DAY, 0},  {" weeks", "weeks", "wks", "w", 0 }},
    {{2*DAY, 0},   {DAY, 0},   {HOUR, 0}, {" days",  "days",  "dys", "d", 0 }},
    {{2*HOUR, 0},  {HOUR, 0},  {MINUTE, 0},{" hours","hours", "hrs", "h", 0 }},
    {{2*MINUTE, 0},{MINUTE, 0},{1, 0},    {" minutes","min",  "mi",  "m", 0 }},
    {{1, 100000},  {1, 0},     {0, 100000},{" seconds", "sec", "s", 0 }},
    {{1, 0},       {1, 0},     {0, 0},    {" second", "sec", "s", 0 }},
    {{0, 1100},    {0, 1000},  {0, 100},  {" milliseconds", "ms", 0 }},
    {{0, 1000},    {0, 1000},  {0, 0},    {" millisecond", "ms", 0 }},
    {{0, 2},       {0, 1},     {0, 0},    {" microseconds", "us", 0 }},
    {{0, 1},       {0, 1},     {0, 0},    {" microsecond", "us", 0 }},
    {{0, 0} }
  };
  struct tscale *ts;

  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  for (ts = time_scales; !tv_is_zero (&ts->thresh); ts++)
    if (tv_is_ge (tv, &ts->thresh))
      {
	char **sfx;
	struct timeval *u = &ts->unit;
	unsigned num = tv_div (tv, u);
	unsigned frac = 0;
	unsigned num_len = int_len (num);

	if (num < 10
	    && !tv_is_zero (&ts->frac_thresh)
	    && tv_is_ge (tv, &ts->frac_thresh))
	  /* Calculate another place of prec, but only for low numbers.  */
	  {
	    /* TV times 10.  */
	    struct timeval tv10 = 
	      { tv->tv_sec * 10 + tv->tv_usec / 100000,
		  (tv->tv_usec % 100000) * 10 };
	    frac = tv_div (&tv10, u) - num * 10;
	    if (frac)
	      num_len += 2;	/* Account for the extra `.' + DIGIT.  */
	  }

	/* While we have a choice, find a suffix that fits in WIDTH.  */
	for (sfx = ts->sfxs; sfx[1]; sfx++)
	  if (num_len + strlen (*sfx) <= width)
	    break;

	if (!sfx[1] && frac)
	  /* We couldn't find a suffix that fits, and we're printing a
	     fraction digit.  Sacrifice the fraction to make it fit.  */
	  {
	    num_len -= 2;
	    frac = 0;
	    for (sfx = ts->sfxs; sfx[1]; sfx++)
	      if (num_len + strlen (*sfx) <= width)
		break;
	  }

	if (!sfx[1])
	  /* Still couldn't find a suffix that fits.  Oh well, use the best. */
	  sfx--;

	if (frac)
	  return snprintf (buf, buf_len, "%d.%d%s", num, frac, *sfx);
	else
	  return snprintf (buf, buf_len, "%d%s", num, *sfx);
      }

  return sprintf (buf, "0");	/* Whatever */
}

/* Prints the number of units of size UNIT in *SECS, subtracting them from
   *SECS, to BUF (the result *must* fit!), followed by SUFFIX; if the number
   of units is zero, however, and *LEADING_ZEROS is false, print nothing (and
   if something *is* printed, set *LEADING_ZEROS to true).  MIN_WIDTH is the
   minimum *total width* (including other fields) needed to print these
   units.  WIDTH is the amount of (total) space available.  The number of
   characters printed is returned.  */
static size_t
add_field (int *secs, int unit, int *leading_zeros,
	   size_t min_width, char *suffix,
	   size_t width, char *buf)
{
  int units = *secs / unit;
  if (units || (width >= min_width && *leading_zeros))
    {
      *secs -= units * unit;
      *leading_zeros = 1;
      return
	sprintf (buf,
		  (width == min_width ? "%d%s"
		   : width == min_width + 1 ? "%2d%s"
		   : "%02d%s"),
		  units, suffix);
    }
  else
    return 0;
}

/* Format into BUF & BUF_LEN the time interval represented by TV, using
   HH:MM:SS notation where possible, with FRAC_PLACES digits after the
   decimal point, and trying to make the result less than WIDTH characters
   wide.  If LEADING_ZEROS is true, then any fields that are zero-valued, but
   would fit in the given width are printed.  If FRAC_PLACES is negative,
   then any space remaining after printing the time, up to WIDTH, is used for
   the fraction.  The number of characters used is returned.  */
size_t
fmt_seconds (struct timeval *tv, int leading_zeros, int frac_places,
	     size_t width, char *buf, size_t buf_len)
{
  char *p = buf;
  int secs = tv->tv_sec;

  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  if (tv->tv_sec > DAY)
    return fmt_named_interval (tv, width, buf, buf_len);

  if (frac_places > 0)
    width -= frac_places + 1;

  /* See if this time won't fit at all in fixed format.  */
  if ((secs > 10*HOUR && width < 8)
      || (secs > HOUR && width < 7)
      || (secs > 10*MINUTE && width < 5)
      || (secs > MINUTE && width < 4)
      || (secs > 10 && width < 2)
      || width < 1)
    return fmt_named_interval (tv, width, buf, buf_len);

  p += add_field (&secs, HOUR, &leading_zeros, 7, ":", width, p);
  p += add_field (&secs, MINUTE, &leading_zeros, 4, ":", width, p);
  p += add_field (&secs, SECOND, &leading_zeros, 1, "", width, p);

  if (frac_places < 0 && (p - buf) < (int) width - 2)
    /* If FRAC_PLACES is < 0, then use any space remaining before WIDTH.  */
    frac_places = width - (p - buf) - 1;

  if (frac_places > 0)
    /* Print fractions of a second.  */
    {
      int frac = tv->tv_usec, i;
      for (i = 6; i > frac_places; i--)
	frac /= 10;
      return (p - buf) + sprintf (p, ".%0*d", frac_places, frac);
    }
  else
    return (p - buf);
}

/* Format into BUF & BUF_LEN the time interval represented by TV, using HH:MM
   notation where possible, and trying to make the result less than WIDTH
   characters wide.  If LEADING_ZEROS is true, then any fields that are
   zero-valued, but would fit in the given width are printed.  The number of
   characters used is returned.  */
size_t
fmt_minutes (struct timeval *tv, int leading_zeros,
	     size_t width, char *buf, size_t buf_len)
{
  char *p = buf;
  int secs = tv->tv_sec;

  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  if (secs > DAY)
    return fmt_named_interval (tv, width, buf, buf_len);

  /* See if this time won't fit at all in fixed format.  */
  if ((secs > 10*HOUR && width < 5)
      || (secs > HOUR && width < 4)
      || (secs > 10*MINUTE && width < 2)
      || width < 1)
    return fmt_named_interval (tv, width, buf, buf_len);

  p += add_field (&secs, HOUR, &leading_zeros, 4, ":", width, p);
  p += add_field (&secs, MINUTE, &leading_zeros, 1, "", width, p);

  return p - buf;
}

/* Format into BUF & BUF_LEN the absolute time represented by TV.  An attempt
   is made to fit the result in less than WIDTH characters, by omitting
   fields implied by the current time, NOW (if NOW is 0, then no assumption
   is made, so the resulting times will be somewhat long).  The number of
   characters used is returned.  */
size_t
fmt_past_time (struct timeval *tv, struct timeval *now,
	       size_t width, char *buf, size_t buf_len)
{
  static char *time_fmts[] = { "%-r", "%-l:%M%p", "%-l%p", 0 };
  static char *week_fmts[] = { "%A", "%a", 0 };
  static char *month_fmts[] = { "%A %-d", "%a %-d", "%a%-d", 0 };
  static char *date_fmts[] =
    { "%A, %-d %B", "%a, %-d %b", "%-d %B", "%-d %b", "%-d%b", 0 };
  static char *year_fmts[] =
    { "%A, %-d %B %Y", "%a, %-d %b %Y", "%a, %-d %b %y", "%-d %b %y", "%-d%b%y", 0 };
  struct tm tm;
  int used = 0;			/* Number of characters generated.  */
  long diff = now ? (now->tv_sec - tv->tv_sec) : tv->tv_sec;

  if (diff < 0)
    diff = -diff;		/* XXX */

  memcpy (&tm, localtime ((time_t *) &tv->tv_sec), sizeof tm);

  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  if (diff < DAY)
    {
      char **fmt;
      for (fmt = time_fmts; *fmt && !used; fmt++)
	used = strftime (buf, width + 1, *fmt, &tm);
      if (! used)
	/* Nothing worked, perhaps WIDTH is too small, but BUF_LEN will work.
	   We know FMT is one past the end the array, so FMT[-1] should be
	   the shortest possible option.  */
	used = strftime (buf, buf_len, fmt[-1], &tm);
    }
  else
    {
      static char *seps[] = { ", ", " ", "", 0 };
      char **fmt, **dfmt, **dfmts, **sep;

      if (diff < WEEK)
	dfmts = week_fmts;
      else if (diff < MONTH)
	dfmts = month_fmts;
      else if (diff < YEAR)
	dfmts = date_fmts;
      else
	dfmts = year_fmts;

      /* This ordering (date varying most quickly, then the separator, then
	 the time) preserves time detail as long as possible, and seems to
	 produce a graceful degradation of the result with decreasing widths. */
      for (fmt = time_fmts; *fmt && !used; fmt++)
	for (sep = seps; *sep && !used; sep++)
	  for (dfmt = dfmts; *dfmt && !used; dfmt++)
	    {
	      char whole_fmt[strlen (*dfmt) + strlen (*sep) + strlen (*fmt) + 1];
	      char *end = whole_fmt;

	      end = stpcpy (end, *dfmt);
	      end = stpcpy (end, *sep);
	      stpcpy (end, *fmt);

	      used = strftime (buf, width + 1, whole_fmt, &tm);
	    }

      if (! used)
	/* No concatenated formats worked, try just date formats.  */
	for (dfmt = dfmts; *dfmt && !used; dfmt++)
	  used = strftime (buf, width + 1, *dfmt, &tm);

      if (! used)
	/* Absolutely nothing has worked, perhaps WIDTH is too small, but
	   BUF_LEN will work.  We know DFMT is one past the end the array, so
	   DFMT[-1] should be the shortest possible option.  */
	used = strftime (buf, buf_len, dfmt[-1], &tm);
    }

  return used;
}
