/* Access, formatting, & comparison routines for printing process info.

   Copyright (C) 1995,96,97,99,2001,02 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert-backtrace.h>
#define assert	assert_backtrace
#include <pwd.h>
#include <hurd/resource.h>
#include <unistd.h>
#include <string.h>
#include <timefmt.h>
#include <sys/time.h>

#include "ps.h"
#include "common.h"

/* XXX */
static char *get_syscall_name (int num) { return 0; }
static char *get_rpc_name (mach_msg_id_t it) { return 0; }

/* ---------------------------------------------------------------- */
/* Getter definitions */

typedef void (*vf)();

static int
ps_get_pid (struct proc_stat *ps)
{
  return proc_stat_pid (ps);
}
const struct ps_getter ps_pid_getter =
{"pid", PSTAT_PID, (vf) ps_get_pid};

static int
ps_get_thread_index (struct proc_stat *ps)
{
  return proc_stat_thread_index (ps);
}
const struct ps_getter ps_thread_index_getter =
{"thread_index", PSTAT_THREAD, (vf) ps_get_thread_index};

static struct ps_user *
ps_get_owner (struct proc_stat *ps)
{
  return proc_stat_owner (ps);
}
const struct ps_getter ps_owner_getter =
{"owner", PSTAT_OWNER, (vf) ps_get_owner};

static int
ps_get_owner_uid (struct proc_stat *ps)
{
  return proc_stat_owner_uid (ps);
}
const struct ps_getter ps_owner_uid_getter =
{"uid", PSTAT_OWNER_UID, (vf) ps_get_owner_uid};

static int
ps_get_ppid (struct proc_stat *ps)
{
  return proc_stat_proc_info (ps)->ppid;
}
const struct ps_getter ps_ppid_getter =
{"ppid", PSTAT_PROC_INFO, (vf) ps_get_ppid};

static int
ps_get_pgrp (struct proc_stat *ps)
{
  return proc_stat_proc_info (ps)->pgrp;
}
const struct ps_getter ps_pgrp_getter =
{"pgrp", PSTAT_PROC_INFO, (vf) ps_get_pgrp};

static int
ps_get_session (struct proc_stat *ps)
{
  return proc_stat_proc_info (ps)->session;
}
const struct ps_getter ps_session_getter =
{"session", PSTAT_PROC_INFO, (vf) ps_get_session};

static int
ps_get_login_col (struct proc_stat *ps)
{
  return proc_stat_proc_info (ps)->logincollection;
}
const struct ps_getter ps_login_col_getter =
{"login_col", PSTAT_PROC_INFO, (vf) ps_get_login_col};

static int
ps_get_num_threads (struct proc_stat *ps)
{
  return proc_stat_num_threads (ps);
}
const struct ps_getter ps_num_threads_getter =
{"num_threads", PSTAT_NUM_THREADS, (vf)ps_get_num_threads};

static void
ps_get_args (struct proc_stat *ps, char **args_p, int *args_len_p)
{
  *args_p = proc_stat_args (ps);
  *args_len_p = proc_stat_args_len (ps);
}
const struct ps_getter ps_args_getter =
{"args", PSTAT_ARGS, ps_get_args};

static void
ps_get_env (struct proc_stat *ps, char **env_p, int *env_len_p)
{
  *env_p = proc_stat_env (ps);
  *env_len_p = proc_stat_env_len (ps);
}
const struct ps_getter ps_env_getter =
{"env", PSTAT_ENV, ps_get_env};

static int
ps_get_state (struct proc_stat *ps)
{
  return proc_stat_state (ps);
}
const struct ps_getter ps_state_getter =
{"state", PSTAT_STATE, (vf) ps_get_state};

static void
ps_get_wait (struct proc_stat *ps, char **wait, int *rpc)
{
  *wait = ps->thread_wait;
  *rpc = ps->thread_rpc;
}
const struct ps_getter ps_wait_getter =
{"wait", PSTAT_THREAD_WAIT, ps_get_wait};

static size_t
ps_get_vsize (struct proc_stat *ps)
{
  return proc_stat_task_basic_info (ps)->virtual_size;
}
const struct ps_getter ps_vsize_getter =
{"vsize", PSTAT_TASK_BASIC, (vf) ps_get_vsize};

static size_t
ps_get_rsize (struct proc_stat *ps)
{
  return proc_stat_task_basic_info (ps)->resident_size;
}
const struct ps_getter ps_rsize_getter =
{"rsize", PSTAT_TASK_BASIC, (vf) ps_get_rsize};

static int
ps_get_cur_priority (struct proc_stat *ps)
{
  return proc_stat_thread_basic_info (ps)->cur_priority;
}
const struct ps_getter ps_cur_priority_getter =
{"cur_priority", PSTAT_THREAD_BASIC, (vf) ps_get_cur_priority};

static int
ps_get_base_priority (struct proc_stat *ps)
{
  return proc_stat_thread_basic_info (ps)->base_priority;
}
const struct ps_getter ps_base_priority_getter =
{"base_priority", PSTAT_THREAD_BASIC, (vf) ps_get_base_priority};

static int
ps_get_max_priority (struct proc_stat *ps)
{
  return proc_stat_thread_sched_info (ps)->max_priority;
}
const struct ps_getter ps_max_priority_getter =
{"max_priority", PSTAT_THREAD_SCHED, (vf) ps_get_max_priority};

static void
ps_get_usr_time (struct proc_stat *ps, struct timeval *tv)
{
  time_value_t tvt = proc_stat_thread_basic_info (ps)->user_time;
  tv->tv_sec = tvt.seconds;
  tv->tv_usec = tvt.microseconds;
}
const struct ps_getter ps_usr_time_getter =
{"usr_time", PSTAT_TIMES, ps_get_usr_time};

static void
ps_get_sys_time (struct proc_stat *ps, struct timeval *tv)
{
  time_value_t tvt = proc_stat_thread_basic_info (ps)->system_time;
  tv->tv_sec = tvt.seconds;
  tv->tv_usec = tvt.microseconds;
}
const struct ps_getter ps_sys_time_getter =
{"sys_time", PSTAT_TIMES, ps_get_sys_time};

static void
ps_get_tot_time (struct proc_stat *ps, struct timeval *tv)
{
  time_value_t tvt = proc_stat_thread_basic_info (ps)->user_time;
  time_value_add (&tvt, &proc_stat_thread_basic_info (ps)->system_time);
  tv->tv_sec = tvt.seconds;
  tv->tv_usec = tvt.microseconds;
}
const struct ps_getter ps_tot_time_getter =
{"tot_time", PSTAT_TIMES, ps_get_tot_time};

static void
ps_get_start_time (struct proc_stat *ps, struct timeval *tv)
{
  time_value_t *const tvt = &proc_stat_task_basic_info (ps)->creation_time;
  tv->tv_sec = tvt->seconds;
  tv->tv_usec = tvt->microseconds;
}
const struct ps_getter ps_start_time_getter =
{"start_time", PSTAT_TASK_BASIC, ps_get_start_time};

static float
ps_get_rmem_frac (struct proc_stat *ps)
{
  static size_t mem_size = 0;

  if (mem_size == 0)
    {
      host_basic_info_t info;
      error_t err = ps_host_basic_info (&info);
      if (err == 0)
	mem_size = info->memory_size;
    }

  if (mem_size > 0)
    return
      (float)proc_stat_task_basic_info (ps)->resident_size
	/ (float)mem_size;
  else
    return 0.0;
}
const struct ps_getter ps_rmem_frac_getter =
{"rmem_frac", PSTAT_TASK_BASIC, (vf) ps_get_rmem_frac};

static float
ps_get_cpu_frac (struct proc_stat *ps)
{
  return (float) proc_stat_thread_basic_info (ps)->cpu_usage
    / (float) TH_USAGE_SCALE;
}
const struct ps_getter ps_cpu_frac_getter =
{"cpu_frac", PSTAT_THREAD_BASIC, (vf) ps_get_cpu_frac};

static int
ps_get_sleep (struct proc_stat *ps)
{
  return proc_stat_thread_basic_info (ps)->sleep_time;
}
const struct ps_getter ps_sleep_getter =
{"sleep", PSTAT_THREAD_BASIC, (vf) ps_get_sleep};

static int
ps_get_susp_count (struct proc_stat *ps)
{
  return proc_stat_suspend_count (ps);
}
const struct ps_getter ps_susp_count_getter =
{"susp_count", PSTAT_SUSPEND_COUNT, (vf) ps_get_susp_count};

static int
ps_get_proc_susp_count (struct proc_stat *ps)
{
  return proc_stat_task_basic_info (ps)->suspend_count;
}
const struct ps_getter ps_proc_susp_count_getter =
{"proc_susp_count", PSTAT_TASK_BASIC, (vf) ps_get_proc_susp_count};

static int
ps_get_thread_susp_count (struct proc_stat *ps)
{
  return proc_stat_thread_basic_info (ps)->suspend_count;
}
const struct ps_getter ps_thread_susp_count_getter =
{"thread_susp_count", PSTAT_SUSPEND_COUNT, (vf) ps_get_thread_susp_count};

static struct ps_tty *
ps_get_tty (struct proc_stat *ps)
{
  return proc_stat_tty (ps);
}
const struct ps_getter ps_tty_getter =
{"tty", PSTAT_TTY, (vf)ps_get_tty};

static int
ps_get_page_faults (struct proc_stat *ps)
{
  return proc_stat_task_events_info (ps)->faults;
}
const struct ps_getter ps_page_faults_getter =
{"page_faults", PSTAT_TASK_EVENTS, (vf) ps_get_page_faults};

static int
ps_get_cow_faults (struct proc_stat *ps)
{
  return proc_stat_task_events_info (ps)->cow_faults;
}
const struct ps_getter ps_cow_faults_getter =
{"cow_faults", PSTAT_TASK_EVENTS, (vf) ps_get_cow_faults};

static int
ps_get_pageins (struct proc_stat *ps)
{
  return proc_stat_task_events_info (ps)->pageins;
}
const struct ps_getter ps_pageins_getter =
{"pageins", PSTAT_TASK_EVENTS, (vf) ps_get_pageins};

static int
ps_get_msgs_sent (struct proc_stat *ps)
{
  return proc_stat_task_events_info (ps)->messages_sent;
}
const struct ps_getter ps_msgs_sent_getter =
{"msgs_sent", PSTAT_TASK_EVENTS, (vf) ps_get_msgs_sent};

static int
ps_get_msgs_rcvd (struct proc_stat *ps)
{
  return proc_stat_task_events_info (ps)->messages_received;
}
const struct ps_getter ps_msgs_rcvd_getter =
{"msgs_rcvd", PSTAT_TASK_EVENTS, (vf) ps_get_msgs_rcvd};

static int
ps_get_zero_fills (struct proc_stat *ps)
{
  return proc_stat_task_events_info (ps)->zero_fills;
}
const struct ps_getter ps_zero_fills_getter =
{"zero_fills", PSTAT_TASK_EVENTS, (vf) ps_get_zero_fills};

static int
ps_get_num_ports (struct proc_stat *ps)
{
  return proc_stat_num_ports (ps);
}
const struct ps_getter ps_num_ports_getter =
{"num_ports", PSTAT_NUM_PORTS, (vf) ps_get_num_ports};

static void
ps_get_exe (struct proc_stat *ps, char **exe_p, int *exe_len_p)
{
  *exe_p = proc_stat_exe (ps);
  *exe_len_p = proc_stat_exe_len (ps);
}
const struct ps_getter ps_exe_getter =
{"exe", PSTAT_EXE, ps_get_exe};
/* ---------------------------------------------------------------- */
/* some printing functions */

/* G () is a helpful macro that just returns the getter G's access function
   cast into a function pointer returning TYPE, as how the function should be
   called varies depending on the getter.  */
#define G(getter,type) ((type (*)())((getter)->fn))

/* Similar to G, but takes a fmt field and uses its getter.  */
#define FG(field,type) G(field->spec->getter, type)

error_t
ps_emit_int (struct proc_stat *ps, struct ps_fmt_field *field,
	     struct ps_stream *stream)
{
  return ps_stream_write_int_field (stream, FG (field, int)(ps), field->width);
}

error_t
ps_emit_nz_int (struct proc_stat *ps, struct ps_fmt_field *field,
		struct ps_stream *stream)
{
  int value = FG (field, int)(ps);
  if (value)
    return ps_stream_write_int_field  (stream, value, field->width);
  else
    return ps_stream_write_field (stream, "-", field->width);
}

error_t
ps_emit_priority (struct proc_stat *ps, struct ps_fmt_field *field,
		  struct ps_stream *stream)
{
  return
    ps_stream_write_int_field (stream,
			       MACH_PRIORITY_TO_NICE (FG (field, int)(ps)),
			       field->width);
}

error_t
ps_emit_num_blocks (struct proc_stat *ps, struct ps_fmt_field *field,
		    struct ps_stream *stream)
{
  char buf[20];
  sprintf(buf, "%d", FG (field, int)(ps) / 1024);
  return ps_stream_write_field (stream, buf, field->width);
}

size_t
sprint_frac_value (char *buf,
		  size_t value, int min_value_len,
		  size_t frac, int frac_scale,
		  int width)
{
  int value_len = 0;
  int frac_len = 0;

  if (value >= 1000)            /* the integer part */
    value_len = 4;              /* values 1000-1023 */
  else if (value >= 100)
    value_len = 3;
  else if (value >= 10)
    value_len = 2;
  else
    value_len = 1;

  while (value_len < min_value_len--)
    *buf++ = '0';

  for (frac_len = frac_scale
       ; frac_len > 0 && (width < value_len + 1 + frac_len || frac % 10 == 0)
       ; frac_len--)
    frac /= 10;

  if (frac_len > 0)
    sprintf (buf, "%zd.%0*zd", value, frac_len, frac);
  else
    sprintf (buf, "%zd", value);

  return strlen (buf);
}

error_t
ps_emit_percent (struct proc_stat *ps, struct ps_fmt_field *field,
		 struct ps_stream *stream)
{
  char buf[20];
  int width = field->width;
  float perc = FG (field, float)(ps) * 100;

  if (width == 0)
    sprintf (buf, "%g", perc);
  else if (ABS (width) > 3)
    sprintf(buf, "%.*f", ABS (width) - 3, perc);
  else
    sprintf (buf, "%d", (int) perc);

  return ps_stream_write_field (stream, buf, width);
}

/* prints its value nicely */
error_t
ps_emit_nice_size_t (struct proc_stat *ps, struct ps_fmt_field *field,
		     struct ps_stream *stream)
{
  char buf[20];
  size_t value = FG (field, size_t)(ps);
  char *sfx = " KMG";
  size_t frac = 0;

  while (value >= 1024)
    {
      frac = ((value & 0x3FF) * 1000) >> 10;
      value >>= 10;
      sfx++;
    }

  sprintf(buf
	  + sprint_frac_value (buf, value, 1, frac, 3, ABS (field->width) - 1),
	  "%c", *sfx);

  return ps_stream_write_field (stream, buf, field->width);
}

error_t
ps_emit_seconds (struct proc_stat *ps, struct ps_fmt_field *field,
		 struct ps_stream *stream)
{
  char buf[20];
  struct timeval tv;
  int width = field->width, prec = field->precision;

  FG (field, void)(ps, &tv);

  if ((field->flags & PS_FMT_FIELD_COLON_MOD) && tv.tv_sec == 0)
    strcpy (buf, "-");
  else
    fmt_seconds (&tv, !(field->flags & PS_FMT_FIELD_AT_MOD), prec, ABS (width),
		 buf, sizeof (buf));

  return ps_stream_write_field (stream, buf, width);
}

error_t
ps_emit_minutes (struct proc_stat *ps, struct ps_fmt_field *field,
		 struct ps_stream *stream)
{
  char buf[20];
  struct timeval tv;
  int width = field->width;

  FG (field, void)(ps, &tv);

  if ((field->flags & PS_FMT_FIELD_COLON_MOD) && tv.tv_sec < 60)
    strcpy (buf, "-");
  else
    fmt_minutes (&tv, !(field->flags & PS_FMT_FIELD_AT_MOD), ABS (width),
		 buf, sizeof (buf));

  return ps_stream_write_field (stream, buf, width);
}

error_t
ps_emit_past_time (struct proc_stat *ps, struct ps_fmt_field *field,
		   struct ps_stream *stream)
{
  static struct timeval now;
  char buf[20];
  struct timeval tv;
  int width = field->width;

  FG (field, void)(ps, &tv);

  if (now.tv_sec == 0 && gettimeofday (&now, 0) < 0)
    return errno;

  fmt_past_time (&tv, &now, ABS (width), buf, sizeof buf);

  return ps_stream_write_field (stream, buf, width);
}

error_t
ps_emit_uid (struct proc_stat *ps, struct ps_fmt_field *field,
	     struct ps_stream *stream)
{
  int uid = FG (field, int)(ps);
  if (uid < 0)
    return ps_stream_write_field (stream, "-", field->width);
  else
    return ps_stream_write_int_field (stream, uid, field->width);
}

error_t
ps_emit_uname (struct proc_stat *ps, struct ps_fmt_field *field,
	       struct ps_stream *stream)
{
  int width = field->width;
  struct ps_user *u = FG (field, struct ps_user *)(ps);
  if (u)
    {
      struct passwd *pw = ps_user_passwd (u);
      if (pw == NULL)
	return ps_stream_write_int_field (stream, ps_user_uid (u), width);
      else
	return ps_stream_write_field (stream, pw->pw_name, width);
    }
  else
    return ps_stream_write_field (stream, "-", width);
}

error_t
ps_emit_user_name (struct proc_stat *ps, struct ps_fmt_field *field,
		   struct ps_stream *stream)
{
  int width = field->width;
  struct ps_user *u = FG (field, struct ps_user *)(ps);
  if (u)
    {
      struct passwd *pw = ps_user_passwd (u);
      if (pw == NULL)
	{
	  char buf[20];
	  sprintf (buf, "(UID %d)", u->uid);
	  return ps_stream_write_field (stream, buf, width);
	}
      else
	return ps_stream_write_field (stream, pw->pw_gecos, width);
    }
  else
    return ps_stream_write_field (stream, "-", width);
}

/* prints a string with embedded nuls as spaces */
error_t
ps_emit_args (struct proc_stat *ps, struct ps_fmt_field *field,
	      struct ps_stream *stream)
{
  char *s0, *p, *q;
  int s0len;
  int width = field->width;
  int fwidth = ABS (width);
  char static_buf[200];
  char *buf = static_buf;

  FG (field, void)(ps, &s0, &s0len);

  if (!s0 || s0len == 0 )
    strcpy (buf, "-");
  else
    {
      if (s0len > sizeof static_buf)
	{
	  buf = malloc (s0len + 1);
	  if (buf == NULL)
	    return ENOMEM;
	}

      if (fwidth == 0 || fwidth > s0len)
	fwidth = s0len;

      for (p = buf, q = s0; fwidth-- > 0; p++, q++)
	{
	  int ch = *q;
	  *p = (ch == '\0' ? ' ' : ch);
	}
      if (q > s0 && *(q - 1) == '\0')
	*--p = '\0';
      else
	*p = '\0';
    }

  {
    error_t err = ps_stream_write_trunc_field (stream, buf, width);
    if (buf != static_buf)
      free (buf);
    return err;
  }
}

error_t
ps_emit_string (struct proc_stat *ps, struct ps_fmt_field *field,
		struct ps_stream *stream)
{
  char *str;
  int len;

  FG (field, void)(ps, &str, &len);

  if (!str || len == 0)
    str = "-";

  return ps_stream_write_trunc_field (stream, str, field->width);
}

error_t
ps_emit_tty_name (struct proc_stat *ps, struct ps_fmt_field *field,
		  struct ps_stream *stream)
{
  const char *name = "-";
  struct ps_tty *tty = FG (field, struct ps_tty *)(ps);

  if (tty)
    {
      name = ps_tty_short_name (tty);
      if (name == NULL || *name == '\0')
	name = "?";
    }

  return ps_stream_write_field (stream, name, field->width);
}

struct state_shadow
{
  /* If any states in STATES are set, the states in shadow are suppressed.  */
  int states;
  int shadow;
};

static const struct state_shadow
state_shadows[] = {
  /* If the process has no parent, it's not a hurd process, and various hurd
     process bits are likely to be noise, so turn them off (but leave the
     noparent bit on).  */
  { PSTAT_STATE_P_NOPARENT,  (PSTAT_STATE_P_ATTRS & ~PSTAT_STATE_P_NOPARENT) },
  /* Don't show sleeping thread if one is running, or the process is stopped.*/
  { PSTAT_STATE_T_RUN | PSTAT_STATE_P_STOP,
    PSTAT_STATE_T_SLEEP | PSTAT_STATE_T_IDLE | PSTAT_STATE_T_WAIT },
  /* Only show the longest sleep.  */
  { PSTAT_STATE_T_IDLE,	     PSTAT_STATE_T_SLEEP | PSTAT_STATE_T_WAIT },
  { PSTAT_STATE_T_SLEEP,     PSTAT_STATE_T_WAIT },
  /* Turn off the thread stop bits if any thread is not stopped.  This is
     generally reasonable, as threads are often suspended to be frobed; if
     they're all suspended, then something's odd (probably in the debugger,
     or crashed).  */
  { PSTAT_STATE_T_STATES & ~PSTAT_STATE_T_HALT,
    PSTAT_STATE_T_HALT | PSTAT_STATE_T_UNCLEAN },
  { 0 }
};

error_t
ps_emit_state (struct proc_stat *ps, struct ps_fmt_field *field,
	       struct ps_stream *stream)
{
  char *tags;
  int raw_state = FG (field, int)(ps);
  int state = raw_state;
  char buf[20], *p = buf;
  const struct state_shadow *shadow = state_shadows;

  while (shadow->states)
    {
      if (raw_state & shadow->states)
	state &= ~shadow->shadow;
      shadow++;
    }

  for (tags = proc_stat_state_tags
       ; state != 0 && *tags != '\0'
       ; state >>= 1, tags++)
    if (state & 1)
      *p++ = *tags;

  *p = '\0';

  return ps_stream_write_field (stream, buf, field->width);
}

error_t
ps_emit_wait (struct proc_stat *ps, struct ps_fmt_field *field,
	      struct ps_stream *stream)
{
  int rpc;
  char *wait;
  char buf[80];

  FG (field, void)(ps, &wait, &rpc);

  if (wait == 0)
    return ps_stream_write_field (stream, "?", field->width);
  else if (*wait == 0)
    return ps_stream_write_field (stream, "-", field->width);
  else if (strcmp (wait, "kernel") == 0)
    /* A syscall.  RPC is actually the syscall number.  */
    {
      char *name = get_syscall_name (rpc);
      if (! name)
	{
	  sprintf (buf, "syscall:%d", -rpc);
	  name = buf;
	}
      return ps_stream_write_field (stream, name, field->width);
    }
  else if (rpc)
    /* An rpc (with msg id RPC); WAIT describes the dest port.  */
    {
      char port_name_buf[20];
      char *name = get_rpc_name (rpc);

      /* See if we should give a more useful name for the port.  */
      if (strcmp (wait, "init#0") == 0)
	wait = "cwd";		/* Current directory */
      else if (strcmp (wait, "init#1") == 0)
	wait = "root";		/* Root directory */
      else if (strcmp (wait, "init#2") == 0)
	wait = "auth";		/* Auth port */
      else if (strcmp (wait, "init#3") == 0)
	wait = "proc";		/* Proc port */
      else if (strcmp (wait, "init#4") == 0)
	wait = "cttyid";	/* Ctty id port */
      else if (strcmp (wait, "init#5") == 0)
	wait = "boot";		/* Bootstrap port */
      else
	/* See if we can shorten the name to fit better.  */
	{
	  char *abbrev = 0, *num = 0;
	  if (strncmp (wait, "fd#", 3) == 0)
	    abbrev = "fd", num = wait + 3;
	  else if (strncmp (wait, "bgfd#", 5) == 0)
	    abbrev = "bg", num = wait + 5;
	  else if (strncmp (wait, "port#", 5) == 0)
	    abbrev = "", num = wait + 5;
	  if (abbrev)
	    {
	      snprintf (port_name_buf, sizeof port_name_buf,
			"%s%s", abbrev, num);
	      wait = port_name_buf;
	    }
	}

      if (name)
	snprintf (buf, sizeof buf, "%s:%s", wait, name);
      else
	snprintf (buf, sizeof buf, "%s:%d", wait, rpc);

      return ps_stream_write_field (stream, buf, field->width);
    }
  else
    return ps_stream_write_field (stream, wait, field->width);
}
/* ---------------------------------------------------------------- */
/* comparison functions */

/* Evaluates CALL if both s1 & s2 are non-NULL, and otherwise returns -1, 0,
   or 1 ala strcmp, considering NULL to be less than non-NULL.  */
#define GUARDED_CMP(s1, s2, call) \
  ((s1) == NULL ? (((s2) == NULL) ? 0 : -1) : ((s2) == NULL ? 1 : (call)))

int
ps_cmp_ints (struct proc_stat *ps1, struct proc_stat *ps2,
	     const struct ps_getter *getter)
{
  int (*gf)() = G (getter, int);
  int v1 = gf(ps1), v2 = gf (ps2);
  return v1 == v2 ? 0 : v1 < v2 ? -1 : 1;
} 

int
ps_cmp_floats (struct proc_stat *ps1, struct proc_stat *ps2,
	       const struct ps_getter *getter)
{
  float (*gf)() = G (getter, float);
  float v1 = gf(ps1), v2 = gf (ps2);
  return v1 == v2 ? 0 : v1 < v2 ? -1 : 1;
} 

int
ps_cmp_size_ts (struct proc_stat *ps1, struct proc_stat *ps2,
		const struct ps_getter *getter)
{
  size_t (*gf)() = G (getter, size_t);
  size_t v1 = gf(ps1), v2 = gf (ps2);
  return v1 == v2 ? 0 : v1 < v2 ? -1 : 1;
} 

int
ps_cmp_uids (struct proc_stat *ps1, struct proc_stat *ps2,
	     const struct ps_getter *getter)
{
  struct ps_user *(*gf)() = G (getter, struct ps_user *);
  struct ps_user *u1 = gf (ps1), *u2 = gf (ps2);
  return (u1 ? ps_user_uid (u1) : -1) - (u2 ? ps_user_uid (u2) : -1);
}

int
ps_cmp_unames (struct proc_stat *ps1, struct proc_stat *ps2,
	       const struct ps_getter *getter)
{
  struct ps_user *(*gf)() = G (getter, struct ps_user *);
  struct ps_user *u1 = gf (ps1), *u2 = gf (ps2);
  struct passwd *pw1 = u1 ? ps_user_passwd (u1) : 0;
  struct passwd *pw2 = u2 ? ps_user_passwd (u2) : 0;
  return GUARDED_CMP (pw1, pw2, strcmp (pw1->pw_name, pw2->pw_name));
}

int
ps_cmp_strings (struct proc_stat *ps1, struct proc_stat *ps2,
		const struct ps_getter *getter)
{
  void (*gf)() = G (getter, void);
  char *s1, *s2;
  int s1len, s2len;

  /* Get both strings */
  gf (ps1, &s1, &s1len);
  gf (ps2, &s2, &s2len);

  return GUARDED_CMP(s1, s2, strncmp(s1, s2, MIN (s1len, s2len)));
}

int
ps_cmp_times (struct proc_stat *ps1, struct proc_stat *ps2,
	      const struct ps_getter *getter)
{
  void (*g)() = G (getter, void);
  struct timeval tv1, tv2;

  g (ps1, &tv1);
  g (ps2, &tv2);

  return
    tv1.tv_sec > tv2.tv_sec ? 1
      : tv1.tv_sec < tv2.tv_sec ? -1
	: tv1.tv_usec > tv2.tv_usec ? 1
	  : tv1.tv_usec < tv2.tv_usec ? -1
	    : 0;
}

/* ---------------------------------------------------------------- */
/* `Nominal' functions -- return true for `unexciting' values.  */

/* For many things, zero is not so interesting.  */
int
ps_nominal_zint (struct proc_stat *ps, const struct ps_getter *getter)
{
  return G (getter, int)(ps) == 0;
}

/* Neither is an empty string.  */
int
ps_nominal_string (struct proc_stat *ps, const struct ps_getter *getter)
{
  char *str;
  size_t len;
  G (getter, char *)(ps, &str, &len);
  return !str || len == 0 || (len == 1 && *str == '-');
}

/* Priorities are similar, but have to be converted to the unix nice scale
   first.  */
int
ps_nominal_pri (struct proc_stat *ps, const struct ps_getter *getter)
{
  return MACH_PRIORITY_TO_NICE(G (getter, int)(ps)) == 0;
}

/* Hurd processes usually have 2 threads;  XXX is there someplace we get get
   this number from?  */
int
ps_nominal_nth (struct proc_stat *ps, const struct ps_getter *getter)
{
  return G (getter, int)(ps) == 2;
}

static int own_uid = -2;	/* -1 means no uid at all.  */

/* A user is nominal if it's the current user.  */
int
ps_nominal_user (struct proc_stat *ps, const struct ps_getter *getter)
{
  struct ps_user *u = G (getter, struct ps_user *)(ps);
  if (own_uid == -2)
    own_uid = getuid ();
  return own_uid >= 0 && u && u->uid == own_uid;
}

/* A uid is nominal if it's that of the current user.  */
int
ps_nominal_uid (struct proc_stat *ps, const struct ps_getter *getter)
{
  uid_t uid = G (getter, uid_t)(ps);
  if (own_uid == -2)
    own_uid = getuid ();
  return own_uid >= 0 && uid == own_uid;
}

/* ---------------------------------------------------------------- */

/* Returns the first entry called NAME in the vector of fmt_specs SPECS.  If
   the result is in fact an alias entry, returns in ALIASED_TO the name of
   the desired source.  */
static const struct ps_fmt_spec *
specv_find (const struct ps_fmt_spec *specs, const char *name,
	    char **aliased_to)
{
  while (! ps_fmt_spec_is_end (specs))
    {
      char *alias = index (specs->name, '=');
      if (alias)
	{
	  unsigned name_len = strlen (name);

	  if (name_len == alias - specs->name
	      && strncasecmp (name, specs->name, name_len) == 0)
	    /* SPECS is an alias, lookup what it refs to. */
	    {
	      *aliased_to = alias + 1;
	      return specs;
	    }
	}
      else
	if (strcasecmp (specs->name, name) == 0)
	  return specs;
      specs++;
    }

  return 0;
}

/* Number of specs allocated in each block of expansions.  */
#define EXP_BLOCK_SIZE  20

/* A node in a linked list of spec vectors.  */
struct ps_fmt_spec_block
{
  struct ps_fmt_spec_block *next;
  struct ps_fmt_spec specs[EXP_BLOCK_SIZE];
};

/* Adds a new alias expansion, using fields from ALIAS, where non-zero,
   otherwise SRC, to SPECS.  */
struct ps_fmt_spec *
specs_add_alias (struct ps_fmt_specs *specs,
		 const struct ps_fmt_spec *alias,
		 const struct ps_fmt_spec *src)
{
  struct ps_fmt_spec *exp;
  struct ps_fmt_spec_block *block;
  char *name_end = index (alias->name, '=');
  size_t name_len = name_end ? name_end - alias->name : strlen (alias->name);

  for (block = specs->expansions; block; block = block->next)
    {
      exp = block->specs;
      while (! ps_fmt_spec_is_end (exp))
	exp++;
      if (exp + 1 < block->specs + EXP_BLOCK_SIZE)
	/* Found some empty space at EXP.  */
	break;
    }

  if (! block)
    /* Ran out of blocks, we gotta make a new one.  */
    {
      block = malloc (sizeof (struct ps_fmt_spec_block));
      if (! block)
	return 0;
      block->next = specs->expansions;
      specs->expansions = block;
      exp = block->specs;
    }

  /* EXP gets its name from ALIAS, but only the bit before the alias marker. */
  exp->name = malloc (name_len + 1);
  if (! exp->name)
    return 0;
  memcpy ((char *)exp->name, (char *)alias->name, name_len);
  ((char *)exp->name)[name_len] = '\0';

  /* Copy the rest of the fields from ALIAS, but defaulting to SRC.  */
  exp->title = alias->title ?: src->title;
  exp->width = alias->width ?: src->width;
  exp->precision = alias->precision >= 0 ? alias->precision : src->precision;
  exp->flags = src->flags ^ alias->flags;
  exp->getter = alias->getter ?: src->getter;
  exp->output_fn = alias->output_fn ?: src->output_fn;
  exp->cmp_fn = alias->cmp_fn ?: src->cmp_fn;
  exp->nominal_fn = alias->nominal_fn ?: src->nominal_fn;

  /* Now add the list-end marker.  */
  memset (exp + 1, 0, sizeof(*exp));

  return exp;
}

const struct ps_fmt_spec *
ps_fmt_specs_find (struct ps_fmt_specs *specs, const char *name)
{
  if (specs)			/* Allow NULL to make recursion more handy. */
    {
      struct ps_fmt_spec_block *block;
      char *aliased_to = 0;
      const struct ps_fmt_spec *s = 0;

      /* If SPECS contains any alias expansions, look there first.  */
      for (block = specs->expansions; block && !s; block = block->next)
	s = specv_find (block->specs, name, &aliased_to);

      if (! s)
	/* Look in the local list of specs.  */
	s = specv_find (specs->specs, name, &aliased_to);

      if (s)
	{
	  if (aliased_to)
	    {
	      const struct ps_fmt_spec *src; /* What S is an alias to.  */

	      if (strcasecmp (name, aliased_to) == 0)
		/* An alias to the same name (useful to just change some
		   property) -- start looking up in the parent.  */
		src = ps_fmt_specs_find (specs->parent, aliased_to);
	      else
		src = ps_fmt_specs_find (specs, aliased_to);

	      if (! src)
		return 0;

	      s = specs_add_alias (specs, s, src);
	    }
	}
      else
	/* Try again with our parent.  */
	s = ps_fmt_specs_find (specs->parent, name);

      return s;
    }
  else
    return 0;
}

/* ---------------------------------------------------------------- */

static const struct ps_fmt_spec specs[] =
{
  {"PID",	0,	-5, -1, 0,
   &ps_pid_getter,	   ps_emit_int,	    ps_cmp_ints,   0},
  {"TH",	"TH#",	-2, -1, 0,
   &ps_thread_index_getter,ps_emit_int,	    ps_cmp_ints,   0},
  {"PPID",	0,	-5, -1, 0,
   &ps_ppid_getter,	   ps_emit_int,     ps_cmp_ints,   0},
  {"UID",	0,	-4, -1, PS_FMT_FIELD_KEEP,
   &ps_owner_uid_getter,   ps_emit_uid,	    ps_cmp_ints,   ps_nominal_uid},
  {"User",	0,	 8, -1, PS_FMT_FIELD_KEEP,
   &ps_owner_getter,	   ps_emit_uname,   ps_cmp_unames, ps_nominal_user},
  {"NTh",	0,	-2, -1, 0,
   &ps_num_threads_getter, ps_emit_int,	    ps_cmp_ints,   ps_nominal_nth},
  {"PGrp",	0,	-5, -1, 0,
   &ps_pgrp_getter,	   ps_emit_int,	    ps_cmp_ints,   0},
  {"Sess",	0,	-5, -1, 0,
   &ps_session_getter,     ps_emit_int,     ps_cmp_ints,   0},
  {"LColl",	0,	-5, -1, 0,
   &ps_login_col_getter,   ps_emit_int,     ps_cmp_ints,   0},
  {"Args",	0,	 0, -1, 0,
   &ps_args_getter,	   ps_emit_args,    ps_cmp_strings,ps_nominal_string},
  {"Arg0",	0,	0, -1, 0,
   &ps_args_getter,	   ps_emit_string,  ps_cmp_strings,ps_nominal_string},
  {"Env",	0,	 0, -1, 0,
   &ps_env_getter,	   ps_emit_args,    ps_cmp_strings,ps_nominal_string},
  {"Start",	0,	-7, 1, 0,
   &ps_start_time_getter,  ps_emit_past_time, ps_cmp_times,0},
  {"Time",	0,	-8, 2, 0,
   &ps_tot_time_getter,    ps_emit_seconds, ps_cmp_times,  0},
  {"UTime",	0,	-8, 2, 0,
   &ps_usr_time_getter,    ps_emit_seconds, ps_cmp_times,  0},
  {"STime",	0,	-8, 2, 0,
   &ps_sys_time_getter,    ps_emit_seconds, ps_cmp_times,  0},
  {"VSize",	0,	-5, -1, 0,
   &ps_vsize_getter,	   ps_emit_nice_size_t,ps_cmp_size_ts,   0},
  {"RSize",	0,	-5, -1, 0,
   &ps_rsize_getter,	   ps_emit_nice_size_t,ps_cmp_size_ts,   0},
  {"Pri",	0,	-3, -1, 0,
   &ps_cur_priority_getter,ps_emit_priority,ps_cmp_ints,   ps_nominal_pri},
  {"BPri",	0,	-3, -1, 0,
   &ps_base_priority_getter,ps_emit_priority,ps_cmp_ints,  ps_nominal_pri},
  {"MPri",	0,	-3, -1, 0,
   &ps_max_priority_getter,ps_emit_priority,ps_cmp_ints,   ps_nominal_pri},
  {"Mem",	"%Mem",	-4, -1, 0,
   &ps_rmem_frac_getter,   ps_emit_percent, ps_cmp_floats, 0},
  {"CPU",	"%CPU",	-4, -1, 0,
   &ps_cpu_frac_getter,    ps_emit_percent, ps_cmp_floats, 0},
  {"State",	0,	4, -1, 0,
   &ps_state_getter,	   ps_emit_state,   0,   	   0},
  {"Wait",	0,	10, -1, 0,
   &ps_wait_getter,        ps_emit_wait,    0,		   0},
  {"Sleep",	0,	-2, -1, 0,
   &ps_sleep_getter,	   ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"Susp",	0,	-2, -1, 0,
   &ps_susp_count_getter,  ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"PSusp",	0,	-2, -1, 0,
   &ps_proc_susp_count_getter, ps_emit_int, ps_cmp_ints,   ps_nominal_zint},
  {"TSusp",	0,	-2, -1, 0,
   &ps_thread_susp_count_getter, ps_emit_int,ps_cmp_ints,  ps_nominal_zint},
  {"TTY",	0,	-2, -1, 0,
   &ps_tty_getter,	   ps_emit_tty_name,ps_cmp_strings,0},
  {"PgFlts",	0,	-5, -1, 0,
   &ps_page_faults_getter, ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"COWFlts",	0,	-5, -1, 0,
   &ps_cow_faults_getter,  ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"PgIns",	0,	-5, -1, 0,
   &ps_pageins_getter,     ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"MsgIn",	0,	-5, -1, 0,
   &ps_msgs_rcvd_getter,   ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"MsgOut",	0,	-5, -1, 0,
   &ps_msgs_sent_getter,   ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"ZFills",	0,	-5, -1, 0,
   &ps_zero_fills_getter,  ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint},
  {"Ports",	0,	-5, -1, 0,
   &ps_num_ports_getter,       ps_emit_int,	    ps_cmp_ints,   0},
  {"Exe",	0,	 0, -1, 0,
   &ps_exe_getter,	   ps_emit_string,  ps_cmp_strings,ps_nominal_string},
  {0}
};

struct ps_fmt_specs ps_std_fmt_specs = { specs, 0 };
