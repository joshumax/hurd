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

#define MINUTE	60
#define HOUR	(60*MINUTE)
#define DAY	(24*HOUR)
#define WEEK 	(7*DAY)
#define MONTH   (31*DAY)	/* Not strictly accurate, but oh well.  */
#define YEAR    (365*DAY)	/* ditto */

/* XXX move this somewhere else.  */
int 
fmt_frac_value (int value, unsigned min_value_len,
		unsigned frac, unsigned frac_scale,
		unsigned width, char *buf, unsigned buf_len)
{
  unsigned value_len;
  unsigned frac_len;
  unsigned len = 0;

  if (value >= 100)		/* the integer part */
    value_len = 3;
  else if (value >= 10)
    value_len = 2;
  else
    value_len = 1;

  while (value_len < min_value_len-- && len < buf_len)
    *buf++ = '0', len++;

  for (frac_len = frac_scale
       ; frac_len > 0 && (width < value_len + 1 + frac_len || frac % 10 == 0)
       ; frac_len--)
    frac /= 10;

  if (frac_len > 0)
    len +=
      snprintf (buf + len, buf_len - len, "%d.%0*d", value, frac_len, frac);
  else
    len += snprintf (buf + len, buf_len - len, "%d", value);

  return len;
}

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

/* Format into BUF & BUF_LEN the time interval represented by TV, trying to
   make the result less than WIDTH characters wide.  The number of characters
   used is returned.  */
int
fmt_named_interval (struct timeval *tv, int width, char *buf, unsigned buf_len)
{
  struct tscale
    {
      struct timeval thresh;	/* Minimum time to use this scale. */
      struct timeval unit;	/* Unit this scale is based on.  */
      int fracs;		/* Allow a single fraction digit? */
      char *sfxs[5];		/* Names to use, in descending length. */
    }
  time_scales[] =
  {
    { {2*YEAR, 0},   {YEAR, 0},  0, { " years",  "years",  "yrs", "y", 0 }},
    { {3*MONTH, 0},  {MONTH, 0}, 0, { " months", "months", "mo", 0 }},
    { {2*WEEK, 0},   {WEEK, 0},  0, { " weeks",  "weeks",  "wks", "w", 0 }},
    { {2*DAY, 0},    {DAY, 0},   0, { " days",   "days",   "dys", "d", 0 }},
    { {2*HOUR, 0},   {HOUR, 0},  0, { " hours",  "hours",  "hrs", "h", 0 }},
    { {2*MINUTE, 0}, {MINUTE, 0},0, { " minutes", "min",   "mi", "m", 0 }},
    { {1, 100000},   {1, 0},     1, { " seconds", "sec", "s", 0 }},
    { {1, 0},        {1, 0},     0, { " second", "sec", "s", 0 }},
    { {0, 1100},     {0, 1000},  1, { " milliseconds", "ms", 0 }},
    { {0, 1000},     {0, 1000},  0, { " millisecond", "ms", 0 }},
    { {0, 2},        {0, 1},     0, { " microseconds", "us", 0 }},
    { {0, 1},        {0, 1},     0, { " microsecond", "us", 0 }},
    { {0, 0} }
  };
  struct tscale *ts = time_scales;

  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  for (ts = time_scales; ts->thresh.tv_sec > 0 || ts->thresh.tv_usec > 0; ts++)
    if (tv->tv_sec > ts->thresh.tv_sec
	|| (tv->tv_sec == ts->thresh.tv_sec
	    && tv->tv_usec >= ts->thresh.tv_usec))
      {
	char **sfx;
	struct timeval *u = &ts->unit;
	unsigned num = tv_div (tv, u);
	unsigned frac = 0;
	unsigned num_len = int_len (num);

	if (ts->fracs && num == 1)
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

	/* While we have a choice, find a suffxi that fits in WIDTH.  */
	for (sfx = ts->sfxs; sfx[1]; sfx++)
	  if (num_len + strlen (*sfx) <= width)
	    break;

	if (frac)
	  return snprintf (buf, buf_len, "%d.%d%s", num, frac, *sfx);
	else
	  return snprintf (buf, buf_len, "%d%s", num, *sfx);
      }

  return sprintf (buf, "0");	/* Whatever */
}

static int 
append_fraction (int frac, int digits, char *buf, unsigned max)
{
  int slen = strlen (buf);
  int left = max - slen;
  if (left > 1)
    {
      buf[slen] = '.';
      left--;
      while (digits > left)
	frac /= 10, digits--;
      sprintf (buf + slen + 1, "%0*d", digits, frac);
      return slen + 1 + digits;
    }
  else
    return slen;
}

/* Format into BUF & BUF_LEN the time interval represented by TV, using
   HH:MM:SS notation where possible, and trying to make the result less than
   WIDTH characters wide.  The number of characters used is returned.  */
int
fmt_seconds (struct timeval *tv, unsigned width, char *buf, unsigned buf_len)
{
  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  if (tv->tv_sec > DAY)
    return fmt_named_interval (tv, width, buf, buf_len);

  if (tv->tv_sec > HOUR)
    if (width >= 8)
      /* H:MM:SS.ss... */
      return snprintf (buf, buf_len, "%2d:%02d:%02d",
		       tv->tv_sec / HOUR,
		       (tv->tv_sec % HOUR) / MINUTE, (tv->tv_sec % MINUTE))
	+ append_fraction (tv->tv_usec, 6, buf, width);
    else
      return fmt_named_interval (tv, width, buf, buf_len);
  else if (width >= 5 || tv->tv_sec > MINUTE)
    /* M:SS.ss... */
    return snprintf (buf, buf_len, "%2d:%02d",
		     tv->tv_sec / MINUTE, tv->tv_sec % MINUTE)
      + append_fraction (tv->tv_usec, 6, buf, width);
  else
    return fmt_frac_value (tv->tv_sec, 1, tv->tv_usec, 6, width, buf, buf_len);
}

/* Format into BUF & BUF_LEN the time interval represented by TV, using
   HH:MM notation where possible, and trying to make the result less than
   WIDTH characters wide.  The number of characters used is returned.  */
int
fmt_minutes (struct timeval *tv, int width, char *buf, unsigned buf_len)
{
  if (width <= 0 || width >= buf_len)
    width = buf_len - 1;

  if (tv->tv_sec > DAY)
    return fmt_named_interval (tv, width, buf, buf_len);

  if (width >= 8)
    /* H:MM:SS.ss... */
    return snprintf (buf, buf_len, "%2d:%02d:%02d",
		     tv->tv_sec / HOUR,
		     (tv->tv_sec % HOUR) / MINUTE, (tv->tv_sec % MINUTE))
      + append_fraction (tv->tv_usec, 6, buf, width);
  else if (width >= 5 || (width >= 4 && tv->tv_sec < 10 * HOUR))
    /* H:MM */
    return sprintf (buf, "%2d:%02d",
		    tv->tv_sec / HOUR, (tv->tv_sec % HOUR) / MINUTE);
  else if ((tv->tv_sec >= 100 * MINUTE && width < 3) || tv->tv_sec > 3 * HOUR)
    /* H: */
    return snprintf (buf, buf_len, "%2d:", tv->tv_sec / HOUR);
  else
    /* M */
    return snprintf (buf, buf_len, "%2d", tv->tv_sec / MINUTE);
}

#if 0
int
fmt_past_time (struct timeval *tv, struct timeval *now,
	  int width, char *buf, unsigned buf_len)
{
  struct tm tm, now_tm;
  char day[10], month[10], time[20];
  long diff = now->tv_sec - tv->tv_sec;

  if (diff < 0)
    diff = -diff;		/* XXX */

  bcopy (tm, localtime (&tv.tv_sec), sizeof (tm));
  bcopy (now_tm, localtime (&now.tv_sec), sizeof (now_tm));

  strftime (time, sizeof (time), "%r", tv);
  if (tm.tm_yday != now_tm.tm_yday || tm.tm_year != now_tm.tm_year)
    strftime (day, sizeof (day), "%a", tv);
  if (tm.tm_mon != now_tm.tm_mon)
    strftime (month, sizeof (month), "%b", tv);

  
}
#endif
