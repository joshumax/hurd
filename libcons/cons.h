/* cons.h - Definitions for cons helper and callback functions.
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

#ifndef _HURD_CONS_H
#define _HURD_CONS_H

#include <dirent.h>

#include <hurd/ports.h>
#include <mach.h>

#include <hurd/console.h>

typedef struct vcons *vcons_t;
typedef struct cons *cons_t;
typedef struct cons_notify *cons_notify_t;

struct vcons
{
  vcons_t next;
  vcons_t prev;
  cons_t cons;
  int id;

  struct mutex lock;
  /* The FD of the input node.  */
  int input;
  struct cons_display *display;
  size_t display_size;
  cons_notify_t notify;

  struct
  {
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
};

struct cons
{
  /* Protects the cons structure and the linked list in
     VCONS_LIST.  */
  struct mutex lock;
  vcons_t vcons_list;
  vcons_t vcons_last;
  vcons_t active;

  struct port_class *port_class;
  struct port_bucket *port_bucket;
  DIR *dir;
  io_t dirport;
  int slack;
};

struct cons_notify
{
  struct port_info pi;

  /* This is set for the dir notification port.  */
  cons_t cons;

  /* This is set for the file notification ports.  */
  vcons_t vcons;
};


/* The user must define this variable.  Set this to the name of the
   console client.  */
extern const char *cons_client_name;

/* The user must define this variables.  Set this to be the client
   version number.  */
extern const char *cons_client_version;

/* The user may define this variable.  Set this to be any additional
   version specification that should be printed for --version. */
extern char *cons_extra_version;

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
   down by -DELTA if DELTA is negative.  This call will be immediately
   followed by corresponding cons_vcons_write calls to fill the
   resulting gap on the screen, and VCONS will be looked throughout
   the whole time.  */
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
   VCONS, which is locked, beep audible.  */
void cons_vcons_beep (vcons_t vcons);

/* The user must define this function.  Make the virtual console
   VCONS, which is locked, flash visibly.  */
void cons_vcons_flash (vcons_t vcons);

/* The user must define this function.  It is called whenever a
   virtual console is selected to be the active one.  */
error_t cons_vcons_activate (vcons_t vcons);

/* The user may define this function.  It is called whenever a
   virtual console is added.  VCONS->cons is locked.  */
void cons_vcons_add (vcons_t vcons);

/* The user may define this function.  It is called whenever a
   virtual console is removed.  VCONS->cons is locked.  */
void cons_vcons_remove (vcons_t vcons);

/* Switch the active console in CONS to ID or the current one plus
   DELTA.  This will call back into the user code by doing a
   cons_vcons_activate.  */
error_t cons_switch (cons_t cons, int id, int delta);


extern const struct argp cons_startup_argp;

extern struct port_bucket *cons_port_bucket;
extern struct port_class *cons_port_class;

error_t cons_init (void);
void cons_server_loop (void);
int cons_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp);

/* Lookup the virtual console with number ID in the console CONS,
   acquire a reference for it, and return it in R_VCONS.  If CREATE is
   true, the virtual console will be created if it doesn't exist yet.
   If CREATE is true, and ID 0, the first free virtual console id is
   used.  CONS must be locked.  */
error_t cons_lookup (cons_t cons, int id, int create, vcons_t *r_vcons);

/* Open the virtual console VCONS.  VCONS->cons is locked.  */
error_t cons_vcons_open (vcons_t vcons);

/* Close the virtual console VCONS.  VCONS->cons is locked.  */
void cons_vcons_close (vcons_t vcons);

/* Redraw the virtual console VCONS, which is locked.  */
void cons_vcons_refresh (vcons_t vcons);

#endif	/* hurd/cons.h */
