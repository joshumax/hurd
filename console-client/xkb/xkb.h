/*  Keyboard plugin for the Hurd console using XKB keymaps.

    Copyright (C) 2002,03  Marco Gerards
   
    Written by Marco Gerards <marco@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  */

#include <errno.h>
#include <argp.h>
#include <xkbcommon/xkbcommon.h>

extern struct xkb_context *ctx;

typedef int keycode_t;
typedef unsigned int scancode_t;

typedef struct keypress
{
  keycode_t keycode;
  unsigned short rel;		/* Key release.  */
} keypress_t;


/* Initialize XKB data structures.  */
error_t xkb_context_init (const char *rules, const char *model, const char *layout, const char *variant, const char* options, const char *composefile);
void xkb_context_cleanup (void);

void process_input (keypress_t key);
void process_keypress_event (keycode_t keycode);
void xkb_timer_notify_input (keypress_t key);

int get_min_keycode (void);
error_t xkb_init_repeat (int delay, int repeat);

int debug_printf (const char *f, ...);
