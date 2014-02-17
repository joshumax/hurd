/* Hacked and slashed by roland@gnu.ai.mit.edu for use in Hurd exec server.  */

/* util.c -- utility functions for gzip support
 * Copyright (C) 1992-1993 Jean-loup Gailly
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License, see the file COPYING.
 */

#ifdef RCSID
static char rcsid[] = "$Id: util.c,v 1.1 1994/12/14 04:29:37 roland Exp $";
#endif

#include <stddef.h>

/* I/O interface */
int (*unzip_read) (char *buf, size_t maxread);
void (*unzip_write) (const char *buf, size_t nwrite);
void (*unzip_read_error) (void);
void (*unzip_error) (const char *msg);
