/* input-drv.h - The interface to an input driver.
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

#ifndef _INPUT_H_
#define _INPUT_H_ 1

#include <errno.h>
#include <argp.h>

#include "focus.h"


struct input_ops
{
  const char *name;

  /* Fill in ARGP with the argp_child structure for this display.  */
  error_t (*argp) (struct argp_child *argp);

  /* Initialize the subsystem.  */
  error_t (*init) (void);

  /* Assign the input device to the focus group FOCUS.  */
  error_t (*set_focus) (focus_t *focus);

  /* Output LENGTH bytes starting from BUFFER in the system encoding.
     Set BUFFER and LENGTH to the new values.  The exact semantics are
     just as in the iconv interface.  */
  error_t (*input) (char **buffer, size_t *length);
};
typedef struct input_ops *input_ops_t;

extern struct input_ops pckbd_input_ops;
extern struct input_ops mach_input_ops;

input_ops_t input_driver[] =
  {
#if defined (__i386__)
    &pckbd_input_ops,
#endif
    &mach_input_ops,
    0
  };

#endif
