/* display.c - The display component of a virtual console.
   Copyright (C) 1999 Kalle Olavi Niemitalo (emu.c from colortext 0.3).
   Copyright (C) 2002, 2003, 2010 Free Software Foundation, Inc.
   Written by Marcus Brinkmann and Kalle Olavi Niemitalo.

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

#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <wchar.h>
#include <iconv.h>
#include <argp.h>
#include <string.h>
#include <assert-backtrace.h>
#include <error.h>

#include <pthread.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/console.h>

#ifndef __STDC_ISO_10646__
#error It is required that wchar_t is UCS-4.
#endif

#include "display.h"
#include "pager.h"

#include "notify_S.h"

struct changes
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
    uint32_t cur_line;
    uint32_t scr_lines;
  } screen;

  uint32_t bell_audible;
  uint32_t bell_visible;

  off_t start;
  off_t end;

#define DISPLAY_CHANGE_CURSOR_POS	0x0001
#define DISPLAY_CHANGE_CURSOR_STATUS	0x0002
#define DISPLAY_CHANGE_SCREEN_CUR_LINE	0x0004
#define DISPLAY_CHANGE_SCREEN_SCR_LINES	0x0008
#define DISPLAY_CHANGE_BELL_AUDIBLE	0x0010
#define DISPLAY_CHANGE_BELL_VISIBLE	0x0020
#define DISPLAY_CHANGE_FLAGS		0x0030
#define DISPLAY_CHANGE_MATRIX		0x0040
  unsigned int which;
};

struct cursor
{
  uint32_t saved_x;
  uint32_t saved_y;
};
typedef struct cursor *cursor_t;

struct scrolling_region
{
  uint32_t top;
  uint32_t bottom;
};

struct parse
{
  /* The parsing state of output characters, needed to handle escape
     character sequences.  */
  enum
    {
      STATE_NORMAL = 0,
      /* An escape character has just been parsed.  */
      STATE_ESC,
      STATE_ESC_BRACKET_INIT,
      STATE_ESC_BRACKET,
      STATE_ESC_BRACKET_QUESTION,
      STATE_ESC_BRACKET_RIGHT_ANGLE
    } state;

  /* How many parameters an escape sequence may have.  */
#define PARSE_MAX_PARAMS 10
  int params[PARSE_MAX_PARAMS];
  int nparams;
};
typedef struct parse *parse_t;

struct output
{
  /* The state of the conversion of output characters.  */
  iconv_t cd;
  /* The output queue holds the characters that are to be outputted.
     The conversion routine might refuse to handle some incomplete
     multi-byte or composed character at the end of the buffer, so we
     have to keep them around.  */
  int stopped;
  pthread_cond_t resumed;
  char *buffer;
  size_t allocated;
  size_t size;

  /* The parsing state of output characters.  */
  struct parse parse;
};
typedef struct output *output_t;

struct attr
{
  conchar_attr_t attr_def;
  conchar_attr_t current;
  /* True if in alternate character set (ASCII graphic) mode.  */
  unsigned int altchar;
};
typedef struct attr *attr_t;

/* Pending directory and file modification requests.  */
struct modreq
{
  mach_port_t port;
  struct modreq *next;
  /* If the port should have been notified, but it was blocking, we
     set this.  */
  int pending;
};

/* For each display, a notification port is created to which the
   kernel sends message accepted notifications.  */
struct notify
{
  struct port_info pi;
  struct display *display;
};

struct display
{
  /* The lock for the virtual console display structure.  */
  pthread_mutex_t lock;

  /* Indicates if OWNER_ID is initialized.  */
  int has_owner;
  /* Specifies the ID of the process that should receive the WINCH
     signal for this virtual console.  */
  int owner_id;

  /* The pending changes.  */
  struct changes changes;

  /* The state of the virtual console.  */
  /* The saved cursor position.  */
  struct cursor cursor;
  /* The output queue and parser state.  */
  struct output output;
  /* The current video attributes.  */
  struct attr attr;
  /* Non-zero if we are in insert mode.  */
  int insert_mode;
  /* Scrolling region.  */
  struct scrolling_region csr;

  struct cons_display *user;

  /* The pager for the USER member.  */
  struct user_pager user_pager;

  /* A list of ports to send file change notifications to.  */
  struct modreq *filemod_reqs;
  /* Those ports which currently have a pending notification.  */
  struct modreq *filemod_reqs_pending;
  /* The notify port.  */
  struct notify *notify_port;
};


/* The bucket and class for notification messages.  */
static struct port_bucket *notify_bucket;
static struct port_class *notify_class;

#define msgh_request_port	msgh_remote_port
#define msgh_reply_port		msgh_local_port

/* SimpleRoutine file_changed */
kern_return_t
nowait_file_changed (mach_port_t notify_port, natural_t tickno,
		     file_changed_type_t change,
		     off_t start, off_t end, mach_port_t notify)
{
  typedef struct
  {
    mach_msg_header_t Head;
    mach_msg_type_t ticknoType;
    natural_t tickno;
    mach_msg_type_t changeType;
    file_changed_type_t change;
    mach_msg_type_t startType;
    loff_t start;
    mach_msg_type_t endType;
    loff_t end;
  } Request;
  union
  {
    Request In;
  } Mess;
  Request *InP = &Mess.In;

  static const mach_msg_type_t ticknoType = {
    /* msgt_name = */           2,
    /* msgt_size = */           32,
    /* msgt_number = */         1,
    /* msgt_inline = */         TRUE,
    /* msgt_longform = */       FALSE,
    /* msgt_deallocate = */     FALSE,
    /* msgt_unused = */         0
  };  

  static const mach_msg_type_t changeType = {
    /* msgt_name = */		2,
    /* msgt_size = */		32,
    /* msgt_number = */		1,
    /* msgt_inline = */		TRUE,
    /* msgt_longform = */	FALSE,
    /* msgt_deallocate = */	FALSE,
    /* msgt_unused = */		0
  };

  static const mach_msg_type_t startType = {
    /* msgt_name = */		11,
    /* msgt_size = */		64,
    /* msgt_number = */		1,
    /* msgt_inline = */		TRUE,
    /* msgt_longform = */	FALSE,
    /* msgt_deallocate = */	FALSE,
    /* msgt_unused = */		0
  };

  static const mach_msg_type_t endType = {
    /* msgt_name = */		11,
    /* msgt_size = */		64,
    /* msgt_number = */		1,
    /* msgt_inline = */		TRUE,
    /* msgt_longform = */	FALSE,
    /* msgt_deallocate = */	FALSE,
    /* msgt_unused = */		0
  };

  InP->ticknoType = ticknoType;
  InP->tickno = tickno;
  InP->changeType = changeType;
  InP->change = change;
  InP->startType = startType;
  InP->start = start;
  InP->endType = endType;
  InP->end = end;

  InP->Head.msgh_bits = MACH_MSGH_BITS(19, 0);
  /* msgh_size passed as argument.  */
  InP->Head.msgh_request_port = notify_port;
  InP->Head.msgh_reply_port = MACH_PORT_NULL;
  InP->Head.msgh_seqno = 0;
  InP->Head.msgh_id = 20501;

  if (notify == MACH_PORT_NULL)
    return mach_msg (&InP->Head, MACH_SEND_MSG | MACH_MSG_OPTION_NONE,
		     64, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
		     MACH_PORT_NULL);
  else
    return mach_msg (&InP->Head, MACH_SEND_MSG | MACH_SEND_NOTIFY,
		     64, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
		     notify);
}

/* Free the list of modification requests MR */
static void
free_modreqs (struct modreq *mr)
{
  error_t err;
  struct modreq *tmp;
  for (; mr; mr = tmp)
    {
      mach_port_t old;
      /* Cancel the dead-name notification.  */
      err = mach_port_request_notification (mach_task_self (), mr->port,
					    MACH_NOTIFY_DEAD_NAME, 0,
					    MACH_PORT_NULL,
					    MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);
      if (! err && MACH_PORT_VALID (old))
	mach_port_deallocate (mach_task_self(), old);

      /* Deallocate the user's port.  */
      mach_port_deallocate (mach_task_self (), mr->port);
      tmp = mr->next;
      free (mr);
    }
}

/* A port deleted notification is generated when we deallocate the
   user's notify port before it is dead.  */
error_t
do_mach_notify_port_deleted (struct port_info *pi, mach_port_t name)
{
  /* As we cancel the dead-name notification before deallocating the
     port, this should not happen.  */
  assert_backtrace (0);
}

/* We request dead name notifications for the user ports.  */
error_t
do_mach_notify_dead_name (struct port_info *pi, mach_port_t dead_name)
{
  struct notify *notify_port = (struct notify *) pi;
  struct display *display;
  struct modreq **preq;
  struct modreq *req;

  if (!notify_port
      || notify_port->pi.bucket != notify_bucket
      || notify_port->pi.class != notify_class)
    return EOPNOTSUPP;

  display = notify_port->display;
  pthread_mutex_lock (&display->lock);
  
  /* Find request in pending queue.  */
  preq = &display->filemod_reqs_pending;
  while (*preq && (*preq)->port != dead_name)
    preq = &(*preq)->next;
  if (! *preq)
    {
      /* Find request in queue.  */
      preq = &display->filemod_reqs;
      while (*preq && (*preq)->port != dead_name)
	preq = &(*preq)->next;
    }

  if (*preq)
    {
      req = *preq;
      *preq = req->next;

      mach_port_deallocate (mach_task_self (), req->port);
      free (req);
    }
  pthread_mutex_unlock (&display->lock);
  
  /* Drop gratuitous extra reference that the notification creates. */
  mach_port_deallocate (mach_task_self (), dead_name);
  
  return 0;
}

error_t
do_mach_notify_port_destroyed (struct port_info *pi, mach_port_t rights)
{
  assert_backtrace (0);
}

error_t
do_mach_notify_no_senders (struct port_info *pi, mach_port_mscount_t count)
{
  return ports_do_mach_notify_no_senders (pi, count);
}

kern_return_t
do_mach_notify_send_once (struct port_info *pi)
{
  return 0;
}

kern_return_t
do_mach_notify_msg_accepted (struct port_info *pi, mach_port_t send)
{
  struct notify *notify_port = (struct notify *) pi;
  struct display *display;
  struct modreq **preq;
  struct modreq *req;

  if (!notify_port
      || notify_port->pi.bucket != notify_bucket
      || notify_port->pi.class != notify_class)
    return EOPNOTSUPP;

  /* If we deallocated the send right in display_destroy before the
     notification was created.  We have nothing to do in this
     case.  */
  if (!send)
    {
      assert_backtrace (0);
      return 0;
    }

  display = notify_port->display;
  pthread_mutex_lock (&display->lock);
  /* Find request in pending queue.  */
  preq = &display->filemod_reqs_pending;
  while (*preq && (*preq)->port != send)
    preq = &(*preq)->next;
  /* If we don't find the request, it was destroyed in
     display_destroy.  In this case, there is nothing left to do
     here.  */
  if (! *preq)
    {
      assert_backtrace (0);
      pthread_mutex_unlock (&display->lock);
      return 0;
    }
  req = *preq;

  if (req->pending)
    {
      error_t err;
      /* A request was desired while we were blocking.  Send it now
	 and stay in pending queue.  */
      req->pending = 0;
      err = nowait_file_changed (req->port, 0, FILE_CHANGED_WRITE, -1, -1,
				 notify_port->pi.port_right);
      if (err && err != MACH_SEND_WILL_NOTIFY)
	{
	  error_t e;
	  mach_port_t old;
	  *preq = req->next;
	  pthread_mutex_unlock (&display->lock);

	  /* Cancel the dead-name notification.	 */
	  e = mach_port_request_notification (mach_task_self (), req->port,
					      MACH_NOTIFY_DEAD_NAME, 0,
					      MACH_PORT_NULL,
					      MACH_MSG_TYPE_MAKE_SEND_ONCE,
					      &old);
	  if (! e && MACH_PORT_VALID (old))
	    mach_port_deallocate (mach_task_self(), old);

	  mach_port_deallocate (mach_task_self (), req->port);
	  free (req);
	  return err;
	}
      if (err == MACH_SEND_WILL_NOTIFY)
	{
	  pthread_mutex_unlock (&display->lock);
	  return 0;
	}
      /* The message was successfully queued, fall through.  */
    }
  /* Remove request from pending queue.  */
  *preq = req->next;
  /* Insert request into active queue.  */
  req->next = display->filemod_reqs;
  display->filemod_reqs = req;
  pthread_mutex_unlock (&display->lock);
  return 0;
}

/* A top-level function for the notification thread that just services
   notification messages.  */
static void *
service_notifications (void *arg)
{
  struct port_bucket *notify_bucket = arg;
  extern int notify_server (mach_msg_header_t *inp, mach_msg_header_t *outp);

  for (;;)
    ports_manage_port_operations_one_thread (notify_bucket,
					     notify_server,
					     1000 * 60 * 10);
  return NULL;
}

error_t
display_notice_changes (display_t display, mach_port_t notify)
{
  error_t err;
  struct modreq *req;
  mach_port_t notify_port;
  mach_port_t old;

  pthread_mutex_lock (&display->lock);
  err = nowait_file_changed (notify, 0, FILE_CHANGED_NULL, 0, 0,
			     MACH_PORT_NULL);
  if (err)
    {
      pthread_mutex_unlock (&display->lock);
      return err;
    }

  req = malloc (sizeof (struct modreq));
  if (!req)
    {
      pthread_mutex_unlock (&display->lock);
      return errno;
    }

  notify_port = ports_get_right (display->notify_port);

  /* Request dead-name notification for the user's port.  */
  err = mach_port_request_notification (mach_task_self (), notify,
					MACH_NOTIFY_DEAD_NAME, 0,
					notify_port,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);
  if (err)
    {
      free (req);
      pthread_mutex_unlock (&display->lock);
      return err;
    }
  assert_backtrace (old == MACH_PORT_NULL);

  req->port = notify;
  req->pending = 0;
  req->next = display->filemod_reqs;
  display->filemod_reqs = req;
  pthread_mutex_unlock (&display->lock);
  return 0;
}

/* Requires DISPLAY to be locked.  */
static void
display_notice_filechange (display_t display)
{
  error_t err;
  struct modreq *req = display->filemod_reqs_pending;
  struct modreq **preq = &display->filemod_reqs;
  mach_port_t notify_port = ports_get_right (display->notify_port);

  while (req)
    {
      req->pending = 1;
      req = req->next;
    }

  while (*preq)
    {
      req = *preq;

      err = nowait_file_changed (req->port, 0, FILE_CHANGED_WRITE, -1, -1,
				 notify_port);
      if (err)
        {
	  /* Remove notify port.  */
	  *preq = req->next;

	  if (err == MACH_SEND_WILL_NOTIFY)
	    {
	      req->next = display->filemod_reqs_pending;
	      display->filemod_reqs_pending = req;
	    }
	  else
	    {
	      error_t e;
	      mach_port_t old;

	      /* Cancel the dead-name notification.  */
	      e = mach_port_request_notification (mach_task_self (), req->port,
						  MACH_NOTIFY_DEAD_NAME, 0,
						  MACH_PORT_NULL, 0, &old);
	      if (! e && MACH_PORT_VALID (old))
		mach_port_deallocate (mach_task_self(), old);
	      mach_port_deallocate (mach_task_self (), req->port);
	      free (req);
	    }
	}
      else
        preq = &req->next;
    }
}

static void
display_flush_filechange (display_t display, unsigned int type)
{
  struct cons_display *user = display->user;
  cons_change_t *next = &user->changes._buffer[user->changes.written
					       % _CONS_CHANGES_LENGTH];
  int notify = 0;
  int bump_written = 0;

  if (type & DISPLAY_CHANGE_MATRIX
      && display->changes.which & DISPLAY_CHANGE_MATRIX)
    {
      notify = 1;
      next->matrix.start = display->changes.start;
      next->matrix.end = display->changes.end;
      user->changes.written++;
      next = &user->changes._buffer[user->changes.written
				    % _CONS_CHANGES_LENGTH];
      display->changes.which &= ~DISPLAY_CHANGE_MATRIX;
    }

  memset (next, 0, sizeof (cons_change_t));
  next->what.not_matrix = 1;

  if (type & DISPLAY_CHANGE_CURSOR_POS
      && display->changes.which & DISPLAY_CHANGE_CURSOR_POS
      && (display->changes.cursor.col != user->cursor.col
	  || display->changes.cursor.row != user->cursor.row))
    {
      notify = 1;
      next->what.cursor_pos = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_CURSOR_POS;
    }

  if (type & DISPLAY_CHANGE_CURSOR_STATUS
      && display->changes.which & DISPLAY_CHANGE_CURSOR_STATUS
      && display->changes.cursor.status != user->cursor.status)
    {
      notify = 1;
      next->what.cursor_status = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_CURSOR_STATUS;
    }

  if (type & DISPLAY_CHANGE_SCREEN_CUR_LINE
      && display->changes.which & DISPLAY_CHANGE_SCREEN_CUR_LINE
      && display->changes.screen.cur_line != user->screen.cur_line)
    {
      notify = 1;
      next->what.screen_cur_line = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_SCREEN_CUR_LINE;
    }

  if (type & DISPLAY_CHANGE_SCREEN_SCR_LINES
      && display->changes.which & DISPLAY_CHANGE_SCREEN_SCR_LINES
      && display->changes.screen.scr_lines != user->screen.scr_lines)
    {
      notify = 1;
      next->what.screen_scr_lines = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_SCREEN_SCR_LINES;
    }

  if (type & DISPLAY_CHANGE_BELL_AUDIBLE
      && display->changes.which & DISPLAY_CHANGE_BELL_AUDIBLE
      && display->changes.bell_audible != user->bell.audible)
    {
      notify = 1;
      next->what.bell_audible = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_BELL_AUDIBLE;
    }

  if (type & DISPLAY_CHANGE_BELL_VISIBLE
      && display->changes.which & DISPLAY_CHANGE_BELL_VISIBLE
      && display->changes.bell_visible != user->bell.visible)
    {
      notify = 1;
      next->what.bell_visible = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_BELL_VISIBLE;
    }

  if (type & DISPLAY_CHANGE_FLAGS
      && display->changes.which & DISPLAY_CHANGE_FLAGS
      && display->changes.flags != user->flags)
    {
      notify = 1;
      next->what.flags = 1;
      bump_written = 1;
      display->changes.which &= ~DISPLAY_CHANGE_FLAGS;
    }

  if (bump_written)
    user->changes.written++;
  if (notify)
    display_notice_filechange (display);
}

/* Record a change in the matrix ringbuffer.  */
static void
display_record_filechange (display_t display, off_t start, off_t end)
{
  if (!(display->changes.which & DISPLAY_CHANGE_MATRIX))
    {
      display->changes.start = start;
      display->changes.end = end;
      display->changes.which |= DISPLAY_CHANGE_MATRIX;
    }
  else
    {
      off_t size = display->user->screen.width * display->user->screen.lines;
      off_t rotate = display->changes.start;
      off_t old_end = display->changes.end;
      int disjunct = 0;

      /* First rotate the buffer to reduce the number of cases.  */
      old_end -= rotate;
      if (old_end < 0)
	old_end += size;
      start -= rotate;
      if (start < 0)
	start += size;
      end -= rotate;
      if (end < 0)
	end += size;

      /* Now the old region starts at 0 and ends at OLD_END.  Try to
	 merge in the new region if it overlaps or touches the old
	 one.  */
      if (start <= end)
	{
	  if (start <= old_end + 1)
	    {
	      start = 0;
	      if (old_end > end)
		end = old_end;
	    }
	  else
	    {
	      if (end == size - 1)
		end = old_end;
	      else
		disjunct = 1;
	    }
	}
      else
	{
	  if (start <= old_end + 1)
	    {
	      start = 0;
	      end = size - 1;
	    }
	  else
	    {
	      if (old_end > end)
		end = old_end;
	    }
	}
      /* Now reverse the rotation.  */
      start += rotate;
      if (start >= size)
	start -= size;
      end += rotate;
      if (end >= size)
	end -= size;

      if (disjunct)
	{
	  /* The regions are disjunct, so we have to flush the old
	     changes.  */
	  display_flush_filechange (display, DISPLAY_CHANGE_MATRIX);
	  display->changes.which |= DISPLAY_CHANGE_MATRIX;
	}
      display->changes.start = start;
      display->changes.end = end;
    }
}
	    

static void
conchar_memset (conchar_t *conchar, wchar_t chr, conchar_attr_t attr,
		size_t size)
{
  int i;

  for (i = 0; i < size; i++)
    {
      conchar->chr = chr;
      conchar->attr = attr;
      conchar++;
    }
}


static error_t
user_create (display_t display, uint32_t width, uint32_t height,
	     uint32_t lines, wchar_t chr, conchar_attr_t attr)
{
  error_t err;
  struct cons_display *user;
  int npages = (round_page (sizeof (struct cons_display) + sizeof (conchar_t)
			    * width * lines)) / vm_page_size;

  err = user_pager_create (&display->user_pager, npages, &display->user);
  if (err)
    return err;

  user = display->user;
  user->magic = CONS_MAGIC;
  user->version = CONS_VERSION_MAJ << CONS_VERSION_MAJ_SHIFT | CONS_VERSION_AGE;
  user->changes.buffer = offsetof (struct cons_display, changes._buffer)
    / sizeof (uint32_t);
  user->changes.length = _CONS_CHANGES_LENGTH;
  user->screen.width = width;
  user->screen.height = height;
  user->screen.lines = lines;
  user->screen.cur_line = 0;
  user->screen.scr_lines = 0;
  user->screen.matrix = sizeof (struct cons_display) / sizeof (uint32_t);
  user->cursor.col = 0;
  user->cursor.row = 0;
  user->cursor.status = CONS_CURSOR_NORMAL;
  conchar_memset (user->_matrix, chr, attr,
		  user->screen.width * user->screen.lines);

  /* FIXME: it seems we don't properly handle getting paged out.
   * For now, just wire the pages to work around the issue.  */
  {
    mach_port_t host;

    error_t err = get_privileged_ports (&host, NULL);
    if (err)
      host = mach_host_self ();

    vm_wire (host, mach_task_self (), (vm_offset_t) user,
	     (vm_size_t) npages * vm_page_size, VM_PROT_READ);
    if (host != mach_host_self ())
	mach_port_deallocate (mach_task_self (), host);
  }
  return 0;
}

static void
user_destroy (display_t display)
{
  user_pager_destroy (&display->user_pager, display->user);
}


static void
screen_fill (display_t display, size_t col1, size_t row1, size_t col2,
	     size_t row2, wchar_t chr, conchar_attr_t attr)
{
  struct cons_display *user = display->user;
  off_t start = ((user->screen.cur_line % user->screen.lines) + row1)
    * user->screen.width + col1;
  off_t end = ((user->screen.cur_line % user->screen.lines) + row2)
    * user->screen.width + col2;
  off_t size = user->screen.width * user->screen.lines;

  if (start >= size && end >= size)
    {
      start -= size;
      end -= size;
    }

  if (end < size)
    {
      conchar_memset (user->_matrix + start, chr, attr, end - start + 1);
      display_record_filechange (display, start, end);
    }
  else
    {
      conchar_memset (user->_matrix + start, chr, attr, size - start);
      conchar_memset (user->_matrix, chr, attr, end - size + 1);
      display_record_filechange (display, start, end - size);
    }
}

static void
screen_shift_left (display_t display, size_t col1, size_t row1, size_t col2,
		   size_t row2, size_t shift, wchar_t chr, conchar_attr_t attr)
{
  struct cons_display *user = display->user;
  off_t start = ((user->screen.cur_line % user->screen.lines) + row1)
    * user->screen.width + col1;
  off_t end = ((user->screen.cur_line % user->screen.lines) + row2)
    * user->screen.width + col2;
  off_t size = user->screen.width * user->screen.lines;

  if (start >= size && end >= size)
    {
      start -= size;
      end -= size;
    }

  if (start + shift <= end)
    {
      /* Use a loop to copy the data.  Using wmemmove and wmemset on
	 the chunks is tiresome, as there are many cases.  */
      off_t src = start + shift;
      off_t dst = start;

      while (src <= end)
	user->_matrix[dst++ % size] = user->_matrix[src++ % size];
      while (dst <= end)
	{
	  user->_matrix[dst % size].chr = chr;
	  user->_matrix[dst++ % size].attr = attr;
	}

      display_record_filechange (display, start, end);
    }
  else
    screen_fill (display, col1, row1, col2, row2, chr, attr);
}

static void
screen_shift_right (display_t display, size_t col1, size_t row1, size_t col2,
		    size_t row2, size_t shift,
		    wchar_t chr, conchar_attr_t attr)
{
  struct cons_display *user = display->user;
  off_t start = ((user->screen.cur_line % user->screen.lines) + row1)
    * user->screen.width + col1;
  off_t end = ((user->screen.cur_line % user->screen.lines) + row2)
    * user->screen.width + col2;
  off_t size = user->screen.width * user->screen.lines;

  if (start >= size && end >= size)
    {
      start -= size;
      end -= size;
    }

  if (start + shift <= end)
    {
      /* Use a loop to copy the data.  Using wmemmove and wmemset on
	 the chunks is tiresome, as there are many cases.  */
      off_t src = end - shift;
      off_t dst = end;

      while (src >= start)
	user->_matrix[dst-- % size] = user->_matrix[src-- % size];
      while (dst >= start)
	{
	  user->_matrix[dst % size].chr = chr;
	  user->_matrix[dst-- % size].attr = attr;
	}

      display_record_filechange (display, start, end);
    }
  else
    screen_fill (display, col1, row1, col2, row2, chr, attr);
}


static error_t
output_init (output_t output, const char *encoding)
{
  pthread_cond_init (&output->resumed, NULL);
  output->stopped = 0;
  output->buffer = NULL;
  output->allocated = 0;
  output->size = 0;

  /* WCHAR_T happens to be UCS-4 on the GNU system.  */
  output->cd = iconv_open ("WCHAR_T", encoding);
  if (output->cd == (iconv_t) -1)
    return errno;
  return 0;
}

static void
output_deinit (output_t output)
{
  iconv_close (output->cd);
}


static void
handle_esc_bracket_hl (display_t display, int code, int flag)
{
  switch (code)
    {
    case 4:		/* ECMA-48 <SMIR>, <RMIR>.  */
      /* Insert mode: <smir>, <rmir>.  */
      display->insert_mode = flag ? 1 : 0;
      break;
    case 34:
      /* Cursor standout: <cnorm>, <cvvis>.  */
      if (flag)
	display->user->cursor.status = CONS_CURSOR_NORMAL;
      else
	display->user->cursor.status = CONS_CURSOR_VERY_VISIBLE;
      /* XXX Flag cursor status change.  */
      break;
    }
}

static void
handle_esc_bracket_m (attr_t attr, int code)
{
  switch (code)
    {
    case 0:
      /* All attributes off: <sgr0>.  */
      attr->current = attr->attr_def;
      attr->altchar = 0;
      break;
    case 1:
      /* Bold on: <bold>.  */
      attr->current.intensity = CONS_ATTR_INTENSITY_BOLD;
      break;
    case 2:
      /* Dim on: <dim>.  */
      attr->current.intensity = CONS_ATTR_INTENSITY_DIM;
      break;
    case 3:
      /* Italic on: <sitm>.  */
      attr->current.italic = 1;
      break;
    case 4:
      /* Underline on: <smul>.  */
      attr->current.underlined = 1;
      break;
    case 5:
      /* (Slow) blink on: <blink>.  */
      attr->current.blinking = 1;
      break;
    case 7:
      /* Reverse video on: <rev>, <smso>.  */
      attr->current.reversed = 1;
      break;
    case 8:
      /* Concealed on: <invis>.  */
      attr->current.concealed = 1;
      break;
    case 10:
      /* Alternate character set mode off: <rmacs>.  */
      attr->altchar = 0;
      break;
    case 11:
      /* Alternate character set mode on: <smacs>.  */
      attr->altchar = 1;
      break;
    case 21:
      /* Normal intensity (switch off bright).  */
      attr->current.intensity = CONS_ATTR_INTENSITY_NORMAL;
      break;
    case 22:
      /* Normal intensity (switch off dim).  */
      attr->current.intensity = CONS_ATTR_INTENSITY_NORMAL;
      break;
    case 23:
      /* Italic off: <ritm>.  */
      attr->current.italic = 0;
      break;
    case 24:
      /* Underline off: <rmul>.  */
      attr->current.underlined = 0;
      break;
    case 25:
      /* Blink off.  */
      attr->current.blinking = 0;
      break;
    case 27:
      /* Reverse video off: <rmso>.  */
      attr->current.reversed = 0;
      break;
    case 28:
      /* Concealed off.  */
      attr->current.concealed = 0;
      break;
    case 30 ... 37:
      /* Set foreground color: <setaf>.  */
      attr->current.fgcol = code - 30;
      break;
    case 39:
      /* Default foreground color; ANSI?.  */
      attr->current.fgcol = attr->attr_def.fgcol;
      break;
    case 40 ... 47:
      /* Set background color: <setab>.  */
      attr->current.bgcol = code - 40;
      break;
    case 49:
      /* Default background color; ANSI?.  */
      attr->current.bgcol = attr->attr_def.bgcol;
      break;
    }
}

static
void limit_cursor (display_t display)
{
  struct cons_display *user = display->user;

  if (user->cursor.col >= user->screen.width)
    user->cursor.col = user->screen.width - 1;
  else if (user->cursor.col < 0)
    user->cursor.col = 0;
      
  if (user->cursor.row >= user->screen.height)
    user->cursor.row = user->screen.height - 1;
  else if (user->cursor.row < 0)
    user->cursor.row = 0;
  
  /* XXX Flag cursor change.  */
}


static void
linefeed (display_t display)
{
  struct cons_display *user = display->user;

  if (display->csr.top == 0 && display->csr.bottom >= user->screen.height - 1)
    {
      /* No scrolling region active, do the normal scrolling activity.  */
      if (user->cursor.row < user->screen.height - 1)
	{
	  user->cursor.row++;
	  /* XXX Flag cursor update.  */
	}
      else
	{
	  user->screen.cur_line++;

	  screen_fill (display, 0, user->screen.height - 1,
		       user->screen.width - 1, user->screen.height - 1,
		       L' ', display->attr.current);
	  if (user->screen.scr_lines <
	      user->screen.lines - user->screen.height)
	    user->screen.scr_lines++;
	  /* XXX Flag current line change.  */
	  /* XXX Possibly flag change of length of scroll back buffer.  */
	}
    }
  else
    {
      /* With an active scrolling region, never actually scroll.  Just
	 shift the scrolling region if necessary.  */
      if (user->cursor.row != display->csr.bottom
	  && user->cursor.row < user->screen.height - 1)
	{
	  user->cursor.row++;
	  /* XXX Flag cursor update.  */
	}
      else if (user->cursor.row == display->csr.bottom)
	screen_shift_left (display, 0, display->csr.top,
			   user->screen.width - 1, display->csr.bottom,
			   user->screen.width,
			   L' ', display->attr.current);
    }       
}

static void
horizontal_tab (display_t display)
{
  struct cons_display *user = display->user;

  user->cursor.col = (user->cursor.col | 7) + 1;
  if (user->cursor.col >= user->screen.width)
    {
      user->cursor.col = 0;
      linefeed (display);
    }
  /* XXX Flag cursor update.  */
}

static void
handle_esc_bracket (display_t display, char op)
{
  struct cons_display *user = display->user;
  parse_t parse = &display->output.parse;
  int i;

  switch (op)
    {
    case 'H':		/* ECMA-48 <CUP>.  */
    case 'f':		/* ECMA-48 <HVP>.  */
      /* Cursor position: <cup>.  */
      user->cursor.col = (parse->params[1] ?: 1) - 1;
      user->cursor.row = (parse->params[0] ?: 1) - 1;
      limit_cursor (display);
      break;
    case 'G':		/* ECMA-48 <CHA>.  */
    case '`':		/* ECMA-48 <HPA>.  */
    case '\'':		/* VT100.  */
      /* Horizontal cursor position: <hpa>.  */
      user->cursor.col = (parse->params[0] ?: 1) - 1;
      limit_cursor (display);
      break;
    case 'a':		/* ECMA-48 <HPR>.  */
      /* Horizontal cursor position relative.  */
      user->cursor.col += (parse->params[1] ?: 1) - 1;
      limit_cursor (display);
      break;
    case 'd':		/* ECMA-48 <VPA>.  */
      /* Vertical cursor position: <vpa>.  */
      user->cursor.row = (parse->params[0] ?: 1) - 1;
      limit_cursor (display);
      break;
    case 'F':		/* ECMA-48 <CPL>.  */
      /* Beginning of previous line.  */
      user->cursor.col = 0;
      /* Fall through.  */
    case 'A':		/* ECMA-48 <CUU>.  */
    case 'k':		/* ECMA-48 <VPB>.  */
      /* Cursor up: <cuu>, <cuu1>.  */
      user->cursor.row -= (parse->params[0] ?: 1);
      limit_cursor (display);
      break;
    case 'E':		/* ECMA-48 <CNL>.  */
      /* Beginning of next line.  */
      user->cursor.col = 0;
      /* Fall through.  */
    case 'B':		/* ECMA-48 <CUD>.  */
    case 'e':		/* ECMA-48 <VPR>.  */
      /* Cursor down: <cud1>, <cud>.  */
      /* Most implementations scroll the screen.  */
      for (i = 0; i < (parse->params[0] ?: 1); i++)
	linefeed (display);
      break;
    case 'C':		/* ECMA-48 <CUF>.  */
      /* Cursor right: <cuf1>, <cuf>.  */
      user->cursor.col += (parse->params[0] ?: 1);
      limit_cursor (display);
      break;
    case 'D':		/* ECMA-48 <CUB>.  */
      /* Cursor left: <cub>, <cub1>.  */
      user->cursor.col -= (parse->params[0] ?: 1);
      limit_cursor (display);
      break;
    case 's':		/* ANSI.SYS: Save cursor and attributes.  */
      /* Save cursor position: <scp>.  */
      display->cursor.saved_x = user->cursor.col;
      display->cursor.saved_y = user->cursor.row;
      break;
    case 'u':		/* ANSI.SYS: Restore cursor and attributes.  */
      /* Restore cursor position: <rcp>.  */
      user->cursor.col = display->cursor.saved_x;
      user->cursor.row = display->cursor.saved_y;
      /* In case the screen was larger before:  */
      limit_cursor (display);
      break;
    case 'l':
      /* Reset mode.  */
      for (i = 0; i < parse->nparams; i++)
	handle_esc_bracket_hl (display, parse->params[i], 0);
      break;
    case 'h':
      /* Set mode.  */
      for (i = 0; i < parse->nparams; i++)
	handle_esc_bracket_hl (display, parse->params[i], 1);
      break;
    case 'm':		/* ECMA-48 <SGR>.  */
      for (i = 0; i < parse->nparams; i++)
	handle_esc_bracket_m (&display->attr, parse->params[i]);
      break;
    case 'J':		/* ECMA-48 <ED>.  */
      switch (parse->params[0])
	{
	case 0:
	  /* Clear to end of screen: <ed>.  */
	  screen_fill (display, user->cursor.col, user->cursor.row,
		       user->screen.width - 1, user->screen.height - 1,
		       L' ', display->attr.current);
	  break;
	case 1:
	  /* Clear to beginning of screen.  */
	  screen_fill (display, 0, 0,
		       user->cursor.col, user->cursor.row,
		       L' ', display->attr.current);
	  break;
	case 2:
	  /* Clear entire screen.  */
	  screen_fill (display, 0, 0,
		       user->screen.width - 1, user->screen.height - 1,
		       L' ', display->attr.current);
	  break;
	}
      break;
    case 'K':		/* ECMA-48 <EL>.  */
      switch (parse->params[0])
	{
	case 0:
	  /* Clear to end of line: <el>.  */
	  screen_fill (display, user->cursor.col, user->cursor.row,
		       user->screen.width - 1, user->cursor.row,
		       L' ', display->attr.current);
	  break;
	case 1:
	  /* Clear to beginning of line: <el1>.  */
	  screen_fill (display, 0, user->cursor.row,
		       user->cursor.col, user->cursor.row,
		       L' ', display->attr.current);
	  break;
	case 2:
	  /* Clear entire line.  */
	  screen_fill (display, 0, user->cursor.row,
		       user->screen.width - 1, user->cursor.row,
		       L' ', display->attr.current);
	  break;
	}
      break;
    case 'L':		/* ECMA-48 <IL>.  */
      /* Insert line(s): <il1>, <il>.  */
      screen_shift_right (display, 0, user->cursor.row,
			  user->screen.width - 1,
			  (user->cursor.row <= display->csr.bottom)
			  ? display->csr.bottom : user->screen.height - 1,
			  (parse->params[0] ?: 1) * user->screen.width,
			  L' ', display->attr.current);
      break;
    case 'M':		/* ECMA-48 <DL>.  */
      /* Delete line(s): <dl1>, <dl>.  */
      screen_shift_left (display, 0, user->cursor.row,
			 user->screen.width - 1,
			  (user->cursor.row <= display->csr.bottom)
			  ? display->csr.bottom : user->screen.height - 1,
			 (parse->params[0] ?: 1) * user->screen.width,
			 L' ', display->attr.current);
      break;
    case '@':		/* ECMA-48 <ICH>.  */
      /* Insert character(s): <ich1>, <ich>.  */
      screen_shift_right (display, user->cursor.col, user->cursor.row,
			  user->screen.width - 1, user->cursor.row,
			  parse->params[0] ?: 1,
			  L' ', display->attr.current);
      break;
    case 'P':		/* ECMA-48 <DCH>.  */
      /* Delete character(s): <dch1>, <dch>.  */
      screen_shift_left (display, user->cursor.col, user->cursor.row,
			 user->screen.width - 1, user->cursor.row,
			 parse->params[0] ?: 1,
			 L' ', display->attr.current);
      break;
    case 'r':		/* VT100: Set scrolling region.  */
      if (!parse->params[1])
	{
	  display->csr.top = 0;
	  display->csr.bottom = user->screen.height - 1;
	}
      else
	{
	  if (parse->params[1] <= user->screen.height
	      && parse->params[0] < parse->params[1])
	    {
	      display->csr.top = parse->params[0] ? parse->params[0] - 1 : 0;
	      display->csr.bottom = parse->params[1] - 1;
	      user->cursor.col = 0;
	      user->cursor.row = 0;
	      /* XXX Flag cursor change.  */
	    }
	}
      break;
    case 'S':		/* ECMA-48 <SU>.  */
      /* Scroll up: <ind>, <indn>.  */
      screen_shift_left (display, 0, display->csr.top,
			 user->screen.width - 1, display->csr.bottom,
			 (parse->params[0] ?: 1) * user->screen.width,
			 L' ', display->attr.current);
      break;
    case 'T':		/* ECMA-48 <SD>.  */
      /* Scroll down: <ri>, <rin>.  */
      screen_shift_right (display, 0, display->csr.top,
			  user->screen.width - 1, display->csr.bottom,
			  (parse->params[0] ?: 1) * user->screen.width,
			  L' ', display->attr.current);
      break;
    case 'X':		/* ECMA-48 <ECH>.  */
      /* Erase character(s): <ech>.  */
      {
	int col = user->cursor.col;
	if (parse->params[0] - 1 > 0)
	  col += parse->params[0] - 1;
	if (col > user->screen.width - 1)
	  col = user->screen.width - 1;

	screen_fill (display, user->cursor.col, user->cursor.row,
		     col, user->cursor.row, L' ', display->attr.current);
      }
      break;
    case 'I':		/* ECMA-48 <CHT>.  */
      /* Horizontal tab.  */
      if (!parse->params[0])
	parse->params[0] = 1;
      while (parse->params[0]--)
	horizontal_tab (display);
      break;
    case 'Z':		/* ECMA-48 <CBT>.  */
      /* Cursor backward tabulation: <cbt>.  */
      if (parse->params[0] > user->screen.height * (user->screen.width / 8))
	{
	  user->cursor.col = 0;
	  user->cursor.row = 0;
	}
      else
	{
	  int i = parse->params[0] ?: 1;

	  while (i--)
	    {
	      if (user->cursor.col == 0)
		{
		  if (user->cursor.row == 0)
		    break;
		  else
		    {
		      user->cursor.col = user->screen.width - 1;
		      user->cursor.row--;
		    }
		}
	      else
		user->cursor.col--;
	      user->cursor.col &= ~7;
	    }
	}
    }
}


static void
handle_esc_bracket_question_hl (display_t display, int code, int flag)
{
  switch (code)
    {
    case 25:
      /* Cursor invisibility: <civis>, <cnorm>.  */
      if (flag)
	display->user->cursor.status = CONS_CURSOR_NORMAL;
      else
	display->user->cursor.status = CONS_CURSOR_INVISIBLE;
      /* XXX Flag cursor status change.  */
      break;
    case 1000:
      /* XTerm mouse tracking.  */
      if (flag)
	display->user->flags |= CONS_FLAGS_TRACK_MOUSE;
      else
	display->user->flags &= ~CONS_FLAGS_TRACK_MOUSE;
      /* XXX Flag flags change.  */
      break;
    }
}


static void
handle_esc_bracket_question (display_t display, char op)
{
  parse_t parse = &display->output.parse;

  int i;
  switch (op)
    {
    case 'l':
      /* Reset mode.  */
      for (i = 0; i < parse->nparams; ++i)
	handle_esc_bracket_question_hl (display, parse->params[i], 0);
      break;
    case 'h':
      /* Set mode.  */
      for (i = 0; i < parse->nparams; ++i)
	handle_esc_bracket_question_hl (display, parse->params[i], 1);
      break;
    }
}


static void
handle_esc_bracket_right_angle_hl (display_t display, int code, int flag)
{
  switch (code)
    {
    case 1:
      /* Bold: <gsbom>, <grbom>.  This is a GNU extension.  */
      if (flag)
	display->attr.current.bold = 1;
      else
	display->attr.current.bold = 0;
      break;
    }
}


static void
handle_esc_bracket_right_angle (display_t display, char op)
{
  parse_t parse = &display->output.parse;

  int i;
  switch (op)
    {
    case 'l':
      /* Reset mode.  */
      for (i = 0; i < parse->nparams; ++i)
	handle_esc_bracket_right_angle_hl (display, parse->params[i], 0);
      break;
    case 'h':
      /* Set mode.  */
      for (i = 0; i < parse->nparams; ++i)
	handle_esc_bracket_right_angle_hl (display, parse->params[i], 1);
      break;
    }
}


static wchar_t
altchar_to_ucs4 (wchar_t chr)
{
  /* Alternative character set frobbing.  */
  switch (chr)
    {
    case L'+':
      return CONS_CHAR_RARROW;
    case L',':
      return CONS_CHAR_LARROW;
    case L'-':
      return CONS_CHAR_UARROW;
    case L'.':
      return CONS_CHAR_DARROW;
    case L'0':
      return CONS_CHAR_BLOCK;
    case L'I':
      return CONS_CHAR_LANTERN;
    case L'`':
      return CONS_CHAR_DIAMOND;
    case L'a':
      return CONS_CHAR_CKBOARD;
    case L'f':
      return CONS_CHAR_DEGREE;
    case L'g':
      return CONS_CHAR_PLMINUS;
    case L'h':
      return CONS_CHAR_BOARD;
    case L'j':
      return CONS_CHAR_LRCORNER;
    case L'k':
      return CONS_CHAR_URCORNER;
    case L'l':
      return CONS_CHAR_ULCORNER;
    case L'm':
      return CONS_CHAR_LLCORNER;
    case L'n':
      return CONS_CHAR_PLUS;
    case L'o':
      return CONS_CHAR_S1;
    case L'p':
      return CONS_CHAR_S3;
    case L'q':
      return CONS_CHAR_HLINE;
    case L'r':
      return CONS_CHAR_S7;
    case L's':
      return CONS_CHAR_S9;
    case L't':
      return CONS_CHAR_LTEE;
    case L'u':
      return CONS_CHAR_RTEE;
    case L'v':
      return CONS_CHAR_BTEE;
    case L'w':
      return CONS_CHAR_TTEE;
    case L'x':
      return CONS_CHAR_VLINE;
    case L'y':
      return CONS_CHAR_LEQUAL;
    case L'z':
      return CONS_CHAR_GEQUAL;
    case L'{':
      return CONS_CHAR_PI;
    case L'|':
      return CONS_CHAR_NEQUAL;
    case L'}':
      return CONS_CHAR_STERLING;
    case L'~':
      return CONS_CHAR_BULLET;
    default:
      return chr;
    }
}

/* Display must be locked.  */
static void
display_output_one (display_t display, wchar_t chr)
{
  struct cons_display *user = display->user;
  parse_t parse = &display->output.parse;

  switch (parse->state)
    {
    case STATE_NORMAL:
      switch (chr)
	{
	case L'\r':
	  /* Carriage return: <cr>.  */
	  if (user->cursor.col)
	    {
	      user->cursor.col = 0;
	      /* XXX Flag cursor update.  */
	    }
	  break;
	case L'\n':
	  /* Line feed.  */
	  linefeed (display);
	  break;
	case L'\b':
	  /* Backspace.  */
	  if (user->cursor.col > 0 || user->cursor.row > 0)
	    {
	      if (user->cursor.col > 0)
		user->cursor.col--;
	      else
		{
		  /* This implements the <bw> functionality.  */
		  user->cursor.col = user->screen.width - 1;
		  user->cursor.row--;
		}
	      /* XXX Flag cursor update.  */
	    }
	  break;
	case L'\t':
	  /* Horizontal tab: <ht> */
	  horizontal_tab (display);
	  break;
	case L'\033':
	  parse->state = STATE_ESC;
	  break;
	case L'\0':
	  /* Padding character: <pad>.  */
	  break;
	case L'\a':
	  /* Audible bell.  */
	  user->bell.audible++;
	  break;
	default:
	  {
	    int line;
	    int idx;

	    if (user->cursor.col >= user->screen.width)
	      {
	        user->cursor.col = 0;
	        linefeed (display);
	      }

	    line = (user->screen.cur_line + user->cursor.row)
	      % user->screen.lines;
	    idx = line * user->screen.width + user->cursor.col;
	    int width, i;

	    width = wcwidth (chr);
	    if (width < 0)
	      width = 1;

	    if (display->insert_mode
		&& user->cursor.col < user->screen.width - width)
	      {
		/* If in insert mode, do the same as <ich1>.  */
		screen_shift_right (display, user->cursor.col,
				    user->cursor.row,
				    user->screen.width - 1, user->cursor.row,
				    width, L' ', display->attr.current);
	      }

	    if (display->attr.altchar)
	      chr = altchar_to_ucs4 (chr);

	    for (i = 0; i < width; i++)
	      {
		if (user->cursor.col >= user->screen.width)
		  break;
		user->_matrix[idx+i].chr = chr;
		user->_matrix[idx+i].attr = display->attr.current;
		user->cursor.col++;
		chr |= CONS_WCHAR_CONTINUED;
	      }

	    if (i > 0)
	      display_record_filechange (display, idx, idx + i - 1);

	    if (user->cursor.col > user->screen.width)
	      {
	        user->cursor.col = 0;
	        linefeed (display);
	      }
	  }
	  break;
	}
      break;

    case STATE_ESC:
      parse->state = STATE_NORMAL;
      switch (chr)
	{
	case L'[':
	  parse->state = STATE_ESC_BRACKET_INIT;
	  break;
	case L'M':		/* ECMA-48 <RIS>.  */
	  /* Reset: <rs2>.  */
	  display->attr.current = display->attr.attr_def;
	  display->attr.altchar = 0;
	  display->insert_mode = 0;
	  display->csr.top = 0;
	  display->csr.bottom = user->screen.height - 1;
	  user->cursor.status = CONS_CURSOR_NORMAL;
	  /* Fall through.  */
	case L'c':
	  /* Clear screen and home cursor: <clear>.  */
	  screen_fill (display, 0, 0,
		       user->screen.width - 1, user->screen.height - 1,
		       L' ', display->attr.current);
	  user->cursor.col = user->cursor.row = 0;
	  /* XXX Flag cursor change.  */
	  break;
	case L'E':		/* ECMA-48 <NEL>.  */
	  /* Newline.  */
	  user->cursor.col = 0;
	  linefeed (display);
	  break;
	case L'7':		/* VT100: Save cursor and attributes.  */
	  /* Save cursor position: <sc>.  */
	  display->cursor.saved_x = user->cursor.col;
	  display->cursor.saved_y = user->cursor.row;
	  break;
	case L'8':		/* VT100: Restore cursor and attributes.  */
	  /* Restore cursor position: <rc>.  */
	  user->cursor.col = display->cursor.saved_x;
	  user->cursor.row = display->cursor.saved_y;
	  /* In case the screen was larger before:  */
	  limit_cursor (display);
	  break;
	case L'g':
	  /* Visible bell.  */
	  user->bell.visible++;
	  break;
	default:
	  /* Unsupported escape sequence.  */
	  break;
	}
      break;

    case STATE_ESC_BRACKET_INIT:
      memset (&parse->params, 0, sizeof parse->params);
      parse->nparams = 0;
      if (chr == '?')
	{
	  parse->state = STATE_ESC_BRACKET_QUESTION;
	  break;	/* Consume the question mark.  */
	}
      else if (chr == '>')
	{
	  parse->state = STATE_ESC_BRACKET_RIGHT_ANGLE;
	  break;	/* Consume the right angle.  */
	}
      else
	parse->state = STATE_ESC_BRACKET;
      /* Fall through.  */
    case STATE_ESC_BRACKET:
    case STATE_ESC_BRACKET_QUESTION:
    case STATE_ESC_BRACKET_RIGHT_ANGLE:
      if (chr >= '0' && chr <= '9')
	parse->params[parse->nparams]
	    = parse->params[parse->nparams]*10 + chr - '0';
      else if (chr == ';')
	{
	  if (++(parse->nparams) >= PARSE_MAX_PARAMS)
	    parse->state = STATE_NORMAL; /* too many */
	}
      else
	{
	  parse->nparams++;
	  if (parse->state == STATE_ESC_BRACKET)
	    handle_esc_bracket (display, chr);
	  else if (parse->state == STATE_ESC_BRACKET_RIGHT_ANGLE)
	    handle_esc_bracket_right_angle (display, chr);
	  else
	    handle_esc_bracket_question (display, chr);
	  parse->state = STATE_NORMAL;
	}
      break;
    default:
      abort ();
    }
}

/* Output LENGTH bytes starting from BUFFER in the system encoding.
   Set BUFFER and LENGTH to the new values.  The exact semantics are
   just as in the iconv interface.  */
static error_t
display_output_some (display_t display, char **buffer, size_t *length)
{
#define CONV_OUTBUF_SIZE 256
  error_t err = 0;

  display->changes.cursor.col = display->user->cursor.col;
  display->changes.cursor.row = display->user->cursor.row;
  display->changes.cursor.status = display->user->cursor.status;
  display->changes.screen.cur_line = display->user->screen.cur_line;
  display->changes.screen.scr_lines = display->user->screen.scr_lines;
  display->changes.bell_audible = display->user->bell.audible;
  display->changes.bell_visible = display->user->bell.visible;
  display->changes.flags = display->user->flags;
  display->changes.which = ~DISPLAY_CHANGE_MATRIX;

  while (!err && *length > 0)
    {
      size_t nconv;
      wchar_t outbuf[CONV_OUTBUF_SIZE];
      char *outptr = (char *) outbuf;
      size_t outsize = CONV_OUTBUF_SIZE * sizeof (wchar_t);
      error_t saved_err;
      int i;

      nconv = iconv (display->output.cd, buffer, length, &outptr, &outsize);
      saved_err = errno;

      /* First process all successfully converted characters.  */
      for (i = 0; i < CONV_OUTBUF_SIZE - outsize / sizeof (wchar_t); i++)
	display_output_one (display, outbuf[i]);

      if (nconv == (size_t) -1)
	{
	    /* Conversion is not completed, look for recoverable
	       errors.  */
#define UNICODE_REPLACEMENT_CHARACTER ((wchar_t) 0xfffd)
	  if (saved_err == EILSEQ)
	    {
	      assert_backtrace (*length);
	      (*length)--;
	      (*buffer)++;
	      display_output_one (display, UNICODE_REPLACEMENT_CHARACTER);
	    }
	  else if (saved_err == EINVAL)
	    /* This is only an unfinished byte sequence at the end of
	       the input buffer.  */
	    break;
	  else if (saved_err != E2BIG)
	    err = saved_err;
	}
    }

  display_flush_filechange (display, ~0);
  return err;
}


/* Forward declaration.  */
void display_destroy_complete (void *pi);

void
display_init (void)
{
  pthread_t thread;
  error_t err;

  user_pager_init ();

  /* Create the notify bucket, and start to serve notifications.  */
  notify_bucket = ports_create_bucket ();
  if (! notify_bucket)
    error (5, errno, "Cannot create notify bucket");
  notify_class = ports_create_class (display_destroy_complete, NULL);
  if (! notify_class)
    error (5, errno, "Cannot create notify class");

  err = pthread_create (&thread, NULL, service_notifications, notify_bucket);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
}


/* Create a new virtual console display, with the system encoding
   being ENCODING.  */
error_t
display_create (display_t *r_display, const char *encoding,
		conchar_attr_t def_attr, unsigned int lines,
		unsigned int width, unsigned int height)
{
  error_t err = 0;
  display_t display;

  *r_display = NULL;
  display = calloc (1, sizeof *display);
  if (!display)
    return ENOMEM;

  err = ports_create_port (notify_class, notify_bucket, sizeof (struct notify),
			   &display->notify_port);
  if (err)
    {
      free (display);
      return err;
    }
  display->notify_port->display = display;

  pthread_mutex_init (&display->lock, NULL);
  display->attr.attr_def = def_attr;
  display->attr.current = display->attr.attr_def;
  display->csr.bottom = height - 1;

  err = user_create (display, width, height, lines, L' ',
		     display->attr.current);
  if (err)
    {
      ports_destroy_right (display->notify_port);
      free (display);
      return err;
    }

  err = output_init (&display->output, encoding);
  if (err)
    {
      user_destroy (display);
      ports_destroy_right (display->notify_port);
      free (display);
    }
  *r_display = display;
  return err;
}


/* Destroy the display DISPLAY.  */
void
display_destroy (display_t display)
{
  pthread_mutex_lock (&display->lock);
  if (display->filemod_reqs_pending)
    {
      free_modreqs (display->filemod_reqs_pending);
      display->filemod_reqs_pending = NULL;
    }
  if (display->filemod_reqs)
    {
      free_modreqs (display->filemod_reqs);
      display->filemod_reqs = NULL;
    }
  ports_destroy_right (display->notify_port);
  output_deinit (&display->output);
  user_destroy (display);
  pthread_mutex_unlock (&display->lock);

  /* We can not free the display structure here, because it might
     still be needed by pending modification requests when msg
     accepted notifications are handled.  So we have to wait until all
     notifications have arrived and the notify port is completely
     deallocated, which will invoke display_destroy_complete
     below.  */
}


/* Complete destruction of the display DISPLAY.  */
void
display_destroy_complete (void *pi)
{
  struct display *display = ((struct notify *) pi)->display;
  free (display);
}


/* Return the dimension of the display in bytes.  */
off_t
display_get_size (display_t display)
{
  return sizeof (struct cons_display)
    + (sizeof (conchar_t) * display->user->screen.width
       * display->user->screen.lines);
}


/* Return the dimensions of the display DISPLAY in *WINSIZE.  */
void
display_getsize (display_t display, struct winsize *winsize)
{
  pthread_mutex_lock (&display->lock);
  winsize->ws_row = display->user->screen.height;
  winsize->ws_col = display->user->screen.width;
  winsize->ws_xpixel = 0;
  winsize->ws_ypixel = 0;
  pthread_mutex_unlock (&display->lock);
}


/* Set the owner of the display DISPLAY to PID.  The owner receives
   the SIGWINCH signal when the terminal size changes.  */
error_t
display_set_owner (display_t display, pid_t pid)
{
  pthread_mutex_lock (&display->lock);
  display->has_owner = 1;
  display->owner_id = pid;
  pthread_mutex_unlock (&display->lock);
  return 0;
}


/* Return the owner of the display DISPLAY in PID.  If there is no
   owner, return ENOTTY.  */
error_t
display_get_owner (display_t display, pid_t *pid)
{
  error_t err = 0;
  pthread_mutex_lock (&display->lock);
  if (!display->has_owner)
    err = ENOTTY;
  else
    *pid = display->owner_id;
  pthread_mutex_unlock (&display->lock);
  return err;
}

/* Output DATALEN characters from the buffer DATA on display DISPLAY.
   The DATA must be supplied in the system encoding configured for
   DISPLAY.  The function returns the amount of bytes written (might
   be smaller than DATALEN) or -1 and the error number in errno.  If
   NONBLOCK is not zero, return with -1 and set errno to EWOULDBLOCK
   if operation would block for a long time.  */
ssize_t
display_output (display_t display, int nonblock, char *data, size_t datalen)
{
  output_t output = &display->output;
  error_t err;
  char *buffer;
  size_t buffer_size;
  ssize_t amount;

  error_t ensure_output_buffer_size (size_t new_size)
    {
      /* Must be a power of two.  */
#define OUTPUT_ALLOCSIZE 32

      if (output->allocated < new_size)
	{
	  char *new_buffer;
	  new_size = (new_size + OUTPUT_ALLOCSIZE - 1)
	    & ~(OUTPUT_ALLOCSIZE - 1);
	  new_buffer = realloc (output->buffer, new_size);
	  if (!new_buffer)
	    return ENOMEM;
	  output->buffer = new_buffer;
	  output->allocated = new_size;
	}
      return 0;
    }

  pthread_mutex_lock (&display->lock);
  while (output->stopped)
    {
      if (nonblock)
        {
          pthread_mutex_unlock (&display->lock);
          errno = EWOULDBLOCK;
          return -1;
        }
      if (pthread_hurd_cond_wait_np (&output->resumed, &display->lock))
        {
          pthread_mutex_unlock (&display->lock);
          errno = EINTR;
          return -1;
        }
    }

  if (output->size)
    {
      err = ensure_output_buffer_size (output->size + datalen);
      if (err)
        {
          pthread_mutex_unlock (&display->lock);
          errno = ENOMEM;
          return -1;
        }
      buffer = output->buffer;
      buffer_size = output->size;
      memcpy (buffer + buffer_size, data, datalen);
      buffer_size += datalen;
    }
  else
    {
      buffer = data;
      buffer_size = datalen;
    }
  amount = buffer_size;
  err = display_output_some (display, &buffer, &buffer_size);
  amount -= buffer_size;

  if (err && !amount)
    {
      pthread_mutex_unlock (&display->lock);
      errno = err;
      return err;
    }
  if (buffer_size)
    {
      /* If we used the caller's buffer DATA, the remaining bytes
         might not fit in our internal output buffer.  In this case we
         can reallocate the buffer in VCONS without needing to update
         OUTPUT (as it points into DATA). */
      err = ensure_output_buffer_size (buffer_size);
      if (err)
        {
          pthread_mutex_unlock (&display->lock);
          return err;
        }
      memmove (output->buffer, buffer, buffer_size);
    }
  output->size = buffer_size;
  amount += buffer_size;

  pthread_mutex_unlock (&display->lock);
  return amount;
}


ssize_t
display_read (display_t display, int nonblock, off_t off,
	      char *data, size_t len)
{
  pthread_mutex_lock (&display->lock);
  memcpy (data, ((char *) display->user) + off, len);
  pthread_mutex_unlock (&display->lock);
  return len;
}


/* Resume the output on the display DISPLAY.  */
void
display_start_output (display_t display)
{
  pthread_mutex_lock (&display->lock);
  if (display->output.stopped)
    {
      display->output.stopped = 0;
      pthread_cond_broadcast (&display->output.resumed);
    }
  display->changes.flags = display->user->flags;
  display->changes.which = DISPLAY_CHANGE_FLAGS;
  display->user->flags &= ~CONS_FLAGS_SCROLL_LOCK;
  display_flush_filechange (display, DISPLAY_CHANGE_FLAGS);
  pthread_mutex_unlock (&display->lock);
}


/* Stop all output on the display DISPLAY.  */
void
display_stop_output (display_t display)
{
  pthread_mutex_lock (&display->lock);
  display->output.stopped = 1;
  display->changes.flags = display->user->flags;
  display->changes.which = DISPLAY_CHANGE_FLAGS;
  display->user->flags |= CONS_FLAGS_SCROLL_LOCK;
  display_flush_filechange (display, DISPLAY_CHANGE_FLAGS);
  pthread_mutex_unlock (&display->lock);
}


/* Return the number of pending output bytes for DISPLAY.  */
size_t
display_pending_output (display_t display)
{
  int output_size;
  pthread_mutex_lock (&display->lock);
  output_size = display->output.size;
  pthread_mutex_unlock (&display->lock);
  return output_size;
}


/* Flush the output buffer, discarding all pending data.  */
void
display_discard_output (display_t display)
{
  pthread_mutex_lock (&display->lock);
  display->output.size = 0;
  pthread_mutex_unlock (&display->lock);
}


mach_port_t
display_get_filemap (display_t display, vm_prot_t prot)
{
  mach_port_t memobj;
  pthread_mutex_lock (&display->lock);
  memobj = user_pager_get_filemap (&display->user_pager, prot);
  pthread_mutex_unlock (&display->lock);
  return memobj;
}
