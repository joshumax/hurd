/* cons.h - Definitions for cons helper and callback functions.
   Copyright (C) 2002, 2003, 2005 Free Software Foundation, Inc.
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

#ifndef _HURD_CONS_H
#define _HURD_CONS_H

#include <dirent.h>

#include <hurd/ports.h>
#include <mach.h>

#include <hurd/console.h>

typedef struct cons *cons_t;
typedef struct vcons_list *vcons_list_t;
typedef struct vcons *vcons_t;
typedef struct cons_notify *cons_notify_t;

struct vcons_list
{
  cons_t cons;
  vcons_list_t next;
  vcons_list_t prev;

  /* The ID of the virtual console entry in the list.  */
  int id;

  /* The opened vcons port on which we receive notifications.  */
  vcons_t vcons;
};

struct cons_notify
{
  struct port_info pi;

  /* This is set for the dir notification port.  */
  cons_t cons;
};

struct vcons
{
  /* This must come first for the port info structure.  */
  struct cons_notify notify;

  /* These elements are static from creation time.  */
  cons_t cons;
  vcons_list_t vcons_entry;
  int id;

  /* The lock that protects all other members.  */
  pthread_mutex_t lock;

  /* The FD of the input node.  */
  int input;

  /* The shared memory of the display.  */
  struct cons_display *display;
  size_t display_size;

  struct
  {
    uint32_t flags;
    struct
    {
      uint32_t col;
      uint32_t row;
      uint32_t status;
    } cursor;
    struct
    {
      uint32_t width;
      uint32_t height;
      uint32_t lines;
      uint32_t cur_line;
      uint32_t scr_lines;
      conchar_t *matrix;
    } screen;
    struct
    {
      uint32_t audible;
      uint32_t visible;
    } bell;
    struct
    {
      uint32_t written;
      uint32_t length;
      cons_change_t *buffer;
    } changes;
  } state;

  uint32_t scrolling;
};

struct cons
{
  /* Protects the cons structure and the linked list in
     VCONS_LIST.  */
  pthread_mutex_t lock;
  vcons_list_t vcons_list;
  vcons_list_t vcons_last;

  struct port_class *port_class;
  struct port_bucket *port_bucket;
  DIR *dir;
  io_t dirport;
  int slack;
};

/* Determines if the mouse moves relatively, to an absolute location
   or to an absolute location expressed by a percentage.  */
enum mouse_movement
  {
    CONS_VCONS_MOUSE_MOVE_REL,
    CONS_VCONS_MOUSE_MOVE_ABS,
    CONS_VCONS_MOUSE_MOVE_ABS_PERCENT
  };

/* The status of a mouse button.  */
enum mouse_button
  {
    CONS_VCONS_MOUSE_BUTTON_NO_OP,
    CONS_VCONS_MOUSE_BUTTON_PRESSED,
    CONS_VCONS_MOUSE_BUTTON_RELEASED
  };

/* An event produced by mouse movement an button presses.  */
typedef struct mouse_event
{ 
  enum mouse_movement mouse_movement;
  float x;
  float y;
  
  enum mouse_button mouse_button;
  int button;
} *mouse_event_t;


/* The user must define this variable.  Set this to the name of the
   console client.  */
extern const char *cons_client_name;

/* The user must define this variable.  Set this to be the client
   version number.  */
extern const char *cons_client_version;

/* The user may define this variable.  Set this to be any additional
   version specification that should be printed for --version. */
extern char *cons_extra_version;

/* The user must define this function.  Deallocate the scarce
   resources (like font glyph slots, colors etc) in the LENGTH entries
   of the screen matrix starting from position COL and ROW.  This call
   is immediately followed by calls to cons_vcons_write that cover the
   same area.  If there are no scarce resources, the caller might do
   nothing.  */
void cons_vcons_clear (vcons_t vcons, size_t length,
		       uint32_t col, uint32_t row);

/* The user must define this function.  Write LENGTH characters
   starting from STR on the virtual console VCONS, which is locked,
   starting from position COL and ROW.  */
void cons_vcons_write (vcons_t vcons, conchar_t *str, size_t length,
		       uint32_t col, uint32_t row);

/* The user must define this function.  Set the cursor on virtual
   console VCONS, which is locked, to position COL and ROW.  */
void cons_vcons_set_cursor_pos (vcons_t vcons, uint32_t col, uint32_t row);

/* The user must define this function.  Set the cursor status of
   virtual console VCONS, which is locked, to STATUS.  */
void cons_vcons_set_cursor_status (vcons_t vcons, uint32_t status);

/* The user must define this function.  Scroll the content of virtual
   console VCONS, which is locked, up by DELTA if DELTA is positive or
   down by -DELTA if DELTA is negative.  DELTA will never be zero, and
   the absolute value if DELTA will be smaller than or equal to the
   height of the screen matrix.

   This call will be immediately followed by corresponding
   cons_vcons_write calls to fill the resulting gap on the screen, and
   VCONS will be looked throughout the whole time.  The purpose of the
   function is two-fold: It is called with an absolute value of DELTA
   smaller than the screen height to perform scrolling.  It is called
   with an absolute value of DELTA equal to the screen height to
   prepare a full refresh of the screen.  In the latter case the user
   should not really perform any scrolling.  Instead it might
   deallocate limited resources (like display glyph slots and palette
   colors) if that helps to perform the subsequent write, just like
   cons_vcons_clear.  It goes without saying that the same
   deallocation, if any, should be performed on the area that will be
   filled with the scrolled in content.

   XXX Possibly need a function to invalidate scrollback buffer, or in
   general to signal a switch of the console so state can be reset.
   Only do this if we make guarantees about validity of scrollback
   buffer, of course.

   The driver is allowed to delay the effect of this operation until
   the UPDATE function is called.  */
void cons_vcons_scroll (vcons_t vcons, int delta);

/* The user may define this function.  Make the changes from
   cons_vcons_write, cons_vcons_set_cursor_pos,
   cons_vcons_set_cursor_status and cons_vcons_scroll active.  VCONS
   is locked and will have been continuously locked from the first
   change since the last update on.  This is the latest possible point
   the user must make the changes visible from.  The user can always
   make the changes visible at a more convenient, earlier time.  */
void cons_vcons_update (vcons_t vcons);

/* The user must define this function.  Make the virtual console
   VCONS, which is locked, beep audibly.  */
void cons_vcons_beep (vcons_t vcons);

/* The user must define this function.  Make the virtual console
   VCONS, which is locked, flash visibly.  */
void cons_vcons_flash (vcons_t vcons);

/* The user must define this function.  Notice the current status of
   the scroll lock flag.  */
void cons_vcons_set_scroll_lock (vcons_t vcons, int onoff);

/* The user must define this function.  It is called whenever a
   virtual console is selected to be the active one.  It is the user's
   responsibility to close the console at some later time.  */
error_t cons_vcons_activate (vcons_t vcons);

/* The user may define this function.  It is called after a
   virtual console entry was added.  CONS is locked.  */
void cons_vcons_add (cons_t cons, vcons_list_t vcons_entry);

/* The user may define this function.  It is called just before a
   virtual console entry is removed.  CONS is locked.  */
void cons_vcons_remove (cons_t cons, vcons_list_t vcons_entry);

/* Open the virtual console ID or the virtual console DELTA steps away
   from VCONS in the linked list and return it in R_VCONS, which will
   be locked.  */
error_t cons_switch (vcons_t vcons, int id, int delta, vcons_t *r_vcons);

/* Enter SIZE bytes from the buffer BUF into the virtual console
   VCONS.  */
error_t cons_vcons_input (vcons_t vcons, char *buf, size_t size);

/* The user must define this function.  Clear the existing screen
   matrix and set the size of the screen matrix to the dimension COL x
   ROW.  This call will be immediately followed by a call to
   cons_vcons_write that covers the whole new screen matrix.  */
error_t cons_vcons_set_dimension (vcons_t vcons,
				  uint32_t col, uint32_t row);

typedef enum
  {
    CONS_SCROLL_DELTA_LINES, CONS_SCROLL_DELTA_SCREENS,
    CONS_SCROLL_ABSOLUTE_LINE, CONS_SCROLL_ABSOLUTE_PERCENTAGE
  } cons_scroll_t;

/* Scroll back into the history of VCONS.  If TYPE is
   CONS_SCROLL_DELTA_LINES, scroll up or down by VALUE lines.  If TYPE
   is CONS_SCROLL_DELTA_SCREENS, scroll up or down by VALUE multiples
   of a screen height.  If TYPE is CONS_SCROLL_ABSOLUTE_LINE, scroll to
   line VALUE (where 0 is the lowest line).  If TYPE is
   CONS_SCROLL_ABSOLUTE_PERCENTAGE, scroll to the position determined
   by VALUE, where 0 is the bottom and 1 is the top.

   The function returns the number of lines actually scrolled up or
   down.  */
int cons_vcons_scrollback (vcons_t vcons, cons_scroll_t type, float value);

/* Set the mouse cursor position to X, Y.  VCONS is locked.  */
error_t cons_vcons_set_mousecursor_pos (vcons_t vcons, float x, float y);

/* If STATUS is set to 0, hide the mouse cursor, otherwise show
   it.  VCONS is locked.  */
error_t cons_vcons_set_mousecursor_status (vcons_t vcons, int status);



extern const struct argp cons_startup_argp;

extern struct port_bucket *cons_port_bucket;
extern struct port_class *cons_port_class;

/* The filename of the console server.  */
extern char *cons_file;

error_t cons_init (void);
void cons_server_loop (void);
int cons_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp);

/* Lookup the virtual console with number ID in the console CONS,
   acquire a reference for it, and return its list entry in R_VCONS.
   If CREATE is true, the virtual console will be created if it
   doesn't exist yet.  If CREATE is true, and ID 0, the first free
   virtual console id is used.  CONS must be locked.  */
error_t cons_lookup (cons_t cons, int id, int create, vcons_list_t *r_vcons);

/* Open the virtual console for VCONS_ENTRY.  CONS is locked.  */
error_t cons_vcons_open (cons_t cons, vcons_list_t vcons_entry,
			 vcons_t *r_vcons);

/* Close the virtual console VCONS.  VCONS->cons is locked.  */
void cons_vcons_close (vcons_t vcons);

/* Destroy the virtual console VCONS.  */
void cons_vcons_destroy (void *port);

/* Redraw the virtual console VCONS, which is locked.  */
void cons_vcons_refresh (vcons_t vcons);

/* Handle the event EV on the virtual console VCONS.  */
error_t cons_vcons_move_mouse (vcons_t vcons, mouse_event_t ev);

#endif	/* hurd/cons.h */
