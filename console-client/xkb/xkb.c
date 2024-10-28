/*  xkb.c -- Main XKB routines.

    Copyright (C) 2002, 2003, 2004  Marco Gerards
   
    Written by Marco Gerards <metgerards@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
  
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <locale.h>
#include <error.h>
#include <device/device.h>
#include <mach/mach_port.h>

#include "xkb.h"
#include <hurd/console.h>
#include <driver.h>
#include <mach-inputdev.h>
#include <wctype.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <assert.h>


/* The xkbcommon kdb context */
struct xkb_context *ctx;

/* The loaded keymap (immutable) */
struct xkb_keymap *keymap;

/* The keyboard state */
struct xkb_state *state;

/* The compose context and table */
struct xkb_compose_table *compose_table;
struct xkb_compose_state *compose_state;


int
debug_printf (const char *f, ...)
{
  va_list ap;
  int ret = 0;

  va_start (ap, f);
#ifdef XKB_DEBUG
  ret = vfprintf (stderr, f, ap);
#endif
  va_end (ap);

  return ret;  
}

static int
symtoctrlsym (xkb_keysym_t c)
{
  c = xkb_keysym_to_upper(c);

  switch (c)
    {
    case 'A' ... 'Z':
      c = c - 'A' + 1;
      break;
    case '[': case '3':
      c = '\e';
      break;
    case '\\': case '4':
      c = '';
      break;
    case ']': case '5':
      c = '';
      break;
    case '^': case '6':
      c = '';
      break;
    case '/':
      c = '/';
      break;
    case ' ':
      c = '\0';
      break;
    case '_': case '7':
      c= '';
      break;
    case '8':
      c = '\x7f';
      break;
    }
  
  return (int) c;
}

int
execute_action(keycode_t keycode)
{

  xkb_keysym_t keysym = xkb_state_key_get_one_sym (state, keycode);
  /* if CTRL+ALT+Delete is pressed notify the caller */
  if (keysym == XKB_KEY_Delete &&
      xkb_state_mod_names_are_active (state, XKB_STATE_MODS_EFFECTIVE, XKB_STATE_MATCH_ALL, XKB_MOD_NAME_CTRL,
                                      XKB_MOD_NAME_ALT, NULL) > 0)
    {
      console_exit ();
      return 1;
    }

  if (xkb_state_mod_name_is_active (state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
    {
      if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F35)
        {
          console_switch (keysym - XKB_KEY_F1 + 1, 0);
          return 1;
        }
      if (keysym == XKB_KEY_Right)
        {
          console_switch (0, 1);
          return 1;
        }
      if (keysym == XKB_KEY_Left)
        {
          console_switch (0, -1);
          return 1;
        }
      if (keysym == XKB_KEY_Up)
        {
          console_scrollback (CONS_SCROLL_DELTA_LINES, 1);
          return 1;
        }
      if (keysym == XKB_KEY_Down)
        {
          console_scrollback (CONS_SCROLL_DELTA_LINES, -1);
          return 1;
        }
      if (keysym == XKB_KEY_Page_Up)
        {
          console_scrollback (CONS_SCROLL_DELTA_SCREENS, -0.5);
          return 1;
        }
      if (keysym == XKB_KEY_Page_Up)
        {
          console_scrollback (CONS_SCROLL_DELTA_SCREENS, 0.5);
          return 1;

        }
      if (keysym == XKB_KEY_Home)
        {
          console_scrollback (CONS_SCROLL_ABSOLUTE_PERCENTAGE, 100 - 0);
          return 1;
        }
      if (keysym == XKB_KEY_End)
        {
          console_scrollback (CONS_SCROLL_ABSOLUTE_PERCENTAGE, 100 - 100);
          return 1;
        }
    }

    if (xkb_state_mod_name_is_active (state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
    {
      if (keysym == XKB_KEY_Home)
        {
          console_scrollback (CONS_SCROLL_ABSOLUTE_PERCENTAGE, 100 - 25);
          return 1;
        }
      if (keysym == XKB_KEY_End)
        {
          console_scrollback (CONS_SCROLL_ABSOLUTE_PERCENTAGE, 100 - 75);
          return 1;
        }
    }
  return 0;
}

char*
get_special_char_interpretation(xkb_keysym_t input)
{
  /* Special key, generate escape sequence.  */
  char *escseq = NULL;

  switch (input)
    {
    case XKB_KEY_Up: case XKB_KEY_KP_Up:
      escseq = CONS_KEY_UP;
      break;
    case XKB_KEY_Down: case XKB_KEY_KP_Down:
      escseq = CONS_KEY_DOWN;
      break;
    case XKB_KEY_Left: case XKB_KEY_KP_Left:
      escseq = CONS_KEY_LEFT;
      break;
    case XKB_KEY_Right: case XKB_KEY_KP_Right:
      escseq = CONS_KEY_RIGHT;
      break;
    case XKB_KEY_BackSpace:
      escseq = CONS_KEY_BACKSPACE;
      break;
    case XKB_KEY_F1: case XKB_KEY_KP_F1:
      escseq = CONS_KEY_F1;
      break;
    case XKB_KEY_F2: case XKB_KEY_KP_F2:
      escseq = CONS_KEY_F2;
      break;
    case XKB_KEY_F3: case XKB_KEY_KP_F3:
      escseq = CONS_KEY_F3;
      break;
    case XKB_KEY_F4: case XKB_KEY_KP_F4:
      escseq = CONS_KEY_F4;
      break;
    case XKB_KEY_F5:
      escseq = CONS_KEY_F5;
      break;
    case XKB_KEY_F6:
      escseq = CONS_KEY_F6;
      break;
    case XKB_KEY_F7:
      escseq = CONS_KEY_F7;
      break;
    case XKB_KEY_F8:
      escseq = CONS_KEY_F8;
      break;
    case XKB_KEY_F9:
      escseq = CONS_KEY_F9;
      break;
    case XKB_KEY_F10:
      escseq = CONS_KEY_F10;
      break;
    case XKB_KEY_F11:
      escseq = CONS_KEY_F11;
      break;
    case XKB_KEY_F12:
      escseq = CONS_KEY_F12;
      break;
    case XKB_KEY_F13:
      escseq = CONS_KEY_F13;
      break;
    case XKB_KEY_F14:
      escseq = CONS_KEY_F14;
      break;
    case XKB_KEY_F15:
      escseq = CONS_KEY_F15;
      break;
    case XKB_KEY_F16:
      escseq = CONS_KEY_F16;
      break;
    case XKB_KEY_F17:
      escseq = CONS_KEY_F17;
      break;
    case XKB_KEY_F18:
      escseq = CONS_KEY_F18;
      break;
    case XKB_KEY_F19:
      escseq = CONS_KEY_F19;
      break;
    case XKB_KEY_F20:
      escseq = CONS_KEY_F20;
      break;
    case XKB_KEY_Home: case XKB_KEY_KP_Home:
      escseq = CONS_KEY_HOME;
      break;
    case XKB_KEY_Insert: case XKB_KEY_KP_Insert:
      escseq = CONS_KEY_IC;
      break;
    case XKB_KEY_Delete: case XKB_KEY_KP_Delete:
      escseq = CONS_KEY_DC;
      break;
    case XKB_KEY_End: case XKB_KEY_KP_End:
      escseq = CONS_KEY_END;
      break;
    case XKB_KEY_Prior: case XKB_KEY_KP_Prior:
      escseq = CONS_KEY_PPAGE;
      break;
    case XKB_KEY_Next: case XKB_KEY_KP_Next:
      escseq = CONS_KEY_NPAGE;
      break;
    case XKB_KEY_KP_Begin:
      escseq = CONS_KEY_B2;
      break;
    case XKB_KEY_ISO_Left_Tab:
      escseq = CONS_KEY_BTAB;
      break;
    case XKB_KEY_Return: case XKB_KEY_KP_Enter:
      escseq = "\x0d";
      break;
    case XKB_KEY_Tab: case XKB_KEY_KP_Tab:
      escseq = "\t";
      break;
    case XKB_KEY_Escape:
      escseq = "\e";
      break;
    }
  return escseq;
}

void
xkb_compose_update (keypress_t key)
{
  if (!key.rel)
    xkb_compose_state_feed(compose_state, key.keycode);
}

void
xkb_compose_update_fini (void)
{
  enum xkb_compose_status status = xkb_compose_state_get_status(compose_state);
  if (status == XKB_COMPOSE_CANCELLED || status == XKB_COMPOSE_COMPOSED)
    xkb_compose_state_reset(compose_state);
}

/* update the xkb state with the key event*/
void
xkb_state_update_input_key (keypress_t key)
{
  enum xkb_key_direction direction = key.rel ? XKB_KEY_UP : XKB_KEY_DOWN;
  xkb_state_update_key (state, key.keycode, direction);
}

/* convert the keycode to a xkb keysym */
static xkb_keysym_t
get_keysym_from_keycode (keycode_t keycode)
{
  xkb_keysym_t sym;

  sym = xkb_state_key_get_one_sym (state, keycode);
  enum xkb_compose_status status = xkb_compose_state_get_status (compose_state);
  if (status == XKB_COMPOSE_COMPOSING || status == XKB_COMPOSE_CANCELLED)
    return XKB_KEYSYM_MAX + 1;

  if (status == XKB_COMPOSE_COMPOSED) {
      sym = xkb_compose_state_get_one_sym(compose_state);
    }

  return sym;
}

/* Take an input event and apply his effect, sending the result to the console */
void
process_keypress_event (keycode_t keycode)
{
  char buf[100];
  size_t size = 0;
  xkb_keysym_t input;

  debug_printf ("input: %d\n", keycode);

  if (execute_action (keycode)) // execute action if any
    return;

  input = get_keysym_from_keycode (keycode); // convert key to input

  debug_printf ("handle: %d\n", input);
  if (input > XKB_KEYSYM_MAX)
    return;

  /* If the real modifier MOD1 (AKA Alt) is set generate an ESC symbol.  */
  if(xkb_state_mod_name_is_active (state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
    buf[size++] = '\e';

  buf[size] = '\0';

  char *escseq = get_special_char_interpretation (input);

  if (escseq != NULL)
    {
      strcat (buf + size, escseq);
      size += strlen (escseq);
    }
  else
    {
      char *buffer = &buf[size];
      size_t left = sizeof (buf) - size;
      int nr;

      /* Control key behaviour.  */
      if (xkb_state_mod_name_is_active (state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        {
          input = symtoctrlsym (input);
          buffer[0] = input;
          buffer[1] = '\0';
          nr = 2;
          size++;
        }
      else
        {
          nr = xkb_keysym_to_utf8 (input, buffer, left);
        }

      if (nr == -1)
        {
          console_error (L"Input buffer overflow");
          size = 0;
        }
      else if (nr == 0)
        size = 0;
      else
        size = nr - 1; // don’t include terminating byte
    }

  if (size > 0 && size <= sizeof(buf))
    console_input (buf, size);
}

void
process_input(keypress_t key)
{
  debug_printf ("keyboard event, keycode: %i, rel %i\n", key.keycode, key.rel);

  /* update the compose state to be able to know the modifiers when retrieving the key */
  xkb_compose_update (key);

  /* activate timers */
  if (key.rel || xkb_keymap_key_repeats (keymap, key.keycode))
    xkb_timer_notify_input (key);
  /* send input for key pressed */
  if (!key.rel)
    process_keypress_event (key.keycode);
  /* clear compose if finished or canceled */
  xkb_compose_update_fini ();
  /* update the keyboard state for the next input */
  xkb_state_update_input_key (key);
}


error_t
xkb_load_keymap (const char *rules, const char *model, const char *layout, const char *variant, const char* options)
{
  struct xkb_rule_names names = {
    .rules = rules,
    .model = model,
    .layout = layout,
    .variant = variant,
    .options = options
  };
  keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);

  if (!keymap)
    return ENOMEM;

  return 0;
}

error_t
xkb_compose_init (const char *composefile) {
  const char *locale;
  FILE *cf;
  locale = setlocale (LC_CTYPE, NULL);

  if (composefile != NULL)
    {
      cf = fopen (composefile, "r");
      if (cf == NULL)
        return errno;
      compose_table = xkb_compose_table_new_from_file (ctx, cf, locale, XKB_COMPOSE_FORMAT_TEXT_V1, XKB_COMPOSE_COMPILE_NO_FLAGS);
    }
  else
    compose_table = xkb_compose_table_new_from_locale (ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);

  if (!compose_table)
    return ENOMEM;

  compose_state = xkb_compose_state_new (compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
  if (!compose_state)
    return ENOMEM;
  return 0;
}

error_t
xkb_context_init (const char *rules, const char *model, const char *layout, const char *variant, const char* options, const char *composefile)
{
  /* initialize a xka context */
  ctx = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (!ctx)
    return ENOMEM;

  xkb_load_keymap (rules, model, layout, variant, options);

  /* load the state */
  state = xkb_state_new (keymap);
  if (!state)
    return ENOMEM;

  return xkb_compose_init (composefile);
}

void
xkb_context_cleanup (void)
{
  /* compose cleanup */
  xkb_compose_state_unref (compose_state);
  xkb_compose_table_unref (compose_table);

  /* state cleanup */
  xkb_state_unref (state);

  /* keymap cleanup */
  xkb_keymap_unref (keymap);

  /* context cleanup */
  xkb_context_unref (ctx);
}

int
get_min_keycode (void)
{
  return xkb_keymap_min_keycode (keymap);
}
