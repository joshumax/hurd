/* display.h - The interface to a display driver.
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

#ifndef _DISPLAY_H_
#define _DISPLAY_H_ 1

#include <errno.h>
#include <argp.h>
#include <sys/ioctl.h>


struct display_ops
{
  /* Initialize the subsystem.  */
  error_t (*init) (void);
  
  /* Create a new (virtual) console display, with the system encoding
     being ENCODING.  A failure at the first time this is called is
     critical.  Subsequent calls might return an error if multiple
     virtual consoles are not supported.  Further operations on this
     console will be called with the first parameter being *HOOK,
     which should be set to some unique identifier for this
     console.  */
  error_t (*create) (void **console, const char *encoding);

  /* Destroy the console CONSOLE.  The caller will first activate a
     different console as the active one.  */
  void (*destroy) (void *console);

  /* Change the active console of WHO to CONSOLE.  WHO is a unique identifier
   for the entity requesting the activation (which can be used by the
   display driver to group several activation requests together).  */
  void (*activate) (void *console, int who);

  /* Scroll the console CONSOLE by the desired amount.  This is only a
     hint, the actual amount scrolled might depend on the capability
     of the subsystem.  Negative AMOUNT scrolls back in history.  */
  error_t (*scroll) (void *console, int amount);

  /* Output LENGTH bytes starting from BUFFER in the system encoding.
     Set BUFFER and LENGTH to the new values.  The exact semantics are
     just as in the iconv interface.  */
  error_t (*output) (void *console, char **buffer, size_t *length);

  /* Return the current size of CONSOLE in WINSIZE.  */
  void (*getsize) (void *console, struct winsize *size);
};
typedef struct display_ops *display_ops_t;

extern struct display_ops vga_display_ops;
extern struct display_ops mach_display_ops;

display_ops_t display_driver[] =
  {
#if defined(__i386__)
    &vga_display_ops,
#endif
    &mach_display_ops,
    0
  };

#endif	/* _DISPLAY_H_ */
