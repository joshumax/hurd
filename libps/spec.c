/* Access, formatting, & comparison routines for printing process info.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pwd.h>
#include <hurd/resource.h>
#include <unistd.h>
#include <string.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */
/* Getter definitions */

typedef void (*vf)();

static int 
ps_get_pid(proc_stat_t ps)
{
  return proc_stat_pid(ps);
}
struct ps_getter ps_pid_getter =
{"pid", PSTAT_PID, (vf) ps_get_pid};

static int 
ps_get_thread_index(proc_stat_t ps)
{
  return proc_stat_thread_index(ps);
}
struct ps_getter ps_thread_index_getter =
{"thread_index", PSTAT_THREAD, (vf) ps_get_thread_index};

static ps_user_t
ps_get_owner(proc_stat_t ps)
{
  return proc_stat_owner(ps);
}
struct ps_getter ps_owner_getter =
{"owner", PSTAT_OWNER, (vf) ps_get_owner};

static int 
ps_get_ppid(proc_stat_t ps)
{
  return proc_stat_info(ps)->ppid;
}
struct ps_getter ps_ppid_getter =
{"ppid", PSTAT_INFO, (vf) ps_get_ppid};

static int 
ps_get_pgrp(proc_stat_t ps)
{
  return proc_stat_info(ps)->pgrp;
}
struct ps_getter ps_pgrp_getter =
{"pgrp", PSTAT_INFO, (vf) ps_get_pgrp};

static int 
ps_get_session(proc_stat_t ps)
{
  return proc_stat_info(ps)->session;
}
struct ps_getter ps_session_getter =
{"session", PSTAT_INFO, (vf) ps_get_session};

static int 
ps_get_login_col(proc_stat_t ps)
{
  return proc_stat_info(ps)->logincollection;
}
struct ps_getter ps_login_col_getter =
{"login_col", PSTAT_INFO, (vf) ps_get_login_col};

static int 
ps_get_num_threads(proc_stat_t ps)
{
  return proc_stat_num_threads(ps);
}
struct ps_getter ps_num_threads_getter =
{"num_threads", PSTAT_NUM_THREADS, (vf)ps_get_num_threads};

static void 
ps_get_args(proc_stat_t ps, char **args_p, int *args_len_p)
{
  *args_p = proc_stat_args(ps);
  *args_len_p = proc_stat_args_len(ps);
}
struct ps_getter ps_args_getter =
{"args", PSTAT_ARGS, ps_get_args};

static int 
ps_get_state(proc_stat_t ps)
{
  return proc_stat_state(ps);
}
struct ps_getter ps_state_getter =
{"state", PSTAT_STATE, (vf) ps_get_state};

static int 
ps_get_vsize(proc_stat_t ps)
{
  return proc_stat_info(ps)->taskinfo.virtual_size;
}
struct ps_getter ps_vsize_getter =
{"vsize", PSTAT_INFO, (vf) ps_get_vsize};

static int 
ps_get_rsize(proc_stat_t ps)
{
  return proc_stat_info(ps)->taskinfo.resident_size;
}
struct ps_getter ps_rsize_getter =
{"rsize", PSTAT_INFO, (vf) ps_get_rsize};

static int 
ps_get_cur_priority(proc_stat_t ps)
{
  return proc_stat_thread_sched_info(ps)->cur_priority;
}
struct ps_getter ps_cur_priority_getter =
{"cur_priority", PSTAT_THREAD_INFO, (vf) ps_get_cur_priority};

static int 
ps_get_base_priority(proc_stat_t ps)
{
  return proc_stat_thread_sched_info(ps)->base_priority;
}
struct ps_getter ps_base_priority_getter =
{"base_priority", PSTAT_THREAD_INFO, (vf) ps_get_base_priority};

static int 
ps_get_max_priority(proc_stat_t ps)
{
  return proc_stat_thread_sched_info(ps)->max_priority;
}
struct ps_getter ps_max_priority_getter =
{"max_priority", PSTAT_THREAD_INFO, (vf) ps_get_max_priority};

static void 
ps_get_usr_time(proc_stat_t ps, time_value_t * tv_out)
{
  *tv_out = proc_stat_thread_basic_info(ps)->user_time;
}
struct ps_getter ps_usr_time_getter =
{"usr_time", PSTAT_THREAD_INFO, ps_get_usr_time};

static void 
ps_get_sys_time(proc_stat_t ps, time_value_t * tv_out)
{
  *tv_out = proc_stat_thread_basic_info(ps)->system_time;
}
struct ps_getter ps_sys_time_getter =
{"sys_time", PSTAT_THREAD_INFO, ps_get_sys_time};

static void 
ps_get_tot_time(proc_stat_t ps, time_value_t * tv_out)
{
  *tv_out = proc_stat_thread_basic_info(ps)->user_time;
  time_value_add(tv_out, &proc_stat_thread_basic_info(ps)->system_time);
}
struct ps_getter ps_tot_time_getter =
{"tot_time", PSTAT_THREAD_INFO, ps_get_tot_time};

static float 
ps_get_rmem_frac(proc_stat_t ps)
{
  static int mem_size = 0;

  if (mem_size == 0)
    {
      host_basic_info_t info;
      error_t err = ps_host_basic_info(&info);
      if (err == 0)
	mem_size = info->memory_size;
    }
  
  if (mem_size > 0)
    return (float)proc_stat_info(ps)->taskinfo.resident_size / (float)mem_size;
  else
    return 0.0;
}
struct ps_getter ps_rmem_frac_getter =
{"rmem_frac", PSTAT_INFO, (vf) ps_get_rmem_frac};

static float 
ps_get_cpu_frac(proc_stat_t ps)
{
  return (float) proc_stat_thread_basic_info(ps)->cpu_usage
    / (float) TH_USAGE_SCALE;
}
struct ps_getter ps_cpu_frac_getter =
{"cpu_frac", PSTAT_THREAD_INFO, (vf) ps_get_cpu_frac};

static int 
ps_get_sleep(proc_stat_t ps)
{
  return proc_stat_thread_basic_info(ps)->sleep_time;
}
struct ps_getter ps_sleep_getter =
{"sleep", PSTAT_THREAD_INFO, (vf) ps_get_sleep};

static int 
ps_get_susp_count(proc_stat_t ps)
{
  return proc_stat_suspend_count(ps);
}
struct ps_getter ps_susp_count_getter =
{"susp_count", PSTAT_SUSPEND_COUNT, (vf) ps_get_susp_count};

static int 
ps_get_proc_susp_count(proc_stat_t ps)
{
  return proc_stat_info(ps)->taskinfo.suspend_count;
}
struct ps_getter ps_proc_susp_count_getter =
{"proc_susp_count", PSTAT_INFO, (vf) ps_get_proc_susp_count};

static int 
ps_get_thread_susp_count(proc_stat_t ps)
{
  return proc_stat_thread_basic_info(ps)->suspend_count;
}
struct ps_getter ps_thread_susp_count_getter =
{"thread_susp_count", PSTAT_SUSPEND_COUNT, (vf) ps_get_thread_susp_count};

static ps_tty_t
ps_get_tty(proc_stat_t ps)
{
  return proc_stat_tty(ps);
}
struct ps_getter ps_tty_getter =
{"tty", PSTAT_TTY, (vf)ps_get_tty};

static int 
ps_get_page_faults(proc_stat_t ps)
{
  return proc_stat_task_events_info(ps)->faults;
}
struct ps_getter ps_page_faults_getter =
{"page_faults", PSTAT_TASK_EVENTS_INFO, (vf) ps_get_page_faults};

static int 
ps_get_cow_faults(proc_stat_t ps)
{
  return proc_stat_task_events_info(ps)->cow_faults;
}
struct ps_getter ps_cow_faults_getter =
{"cow_faults", PSTAT_TASK_EVENTS_INFO, (vf) ps_get_cow_faults};

static int 
ps_get_pageins(proc_stat_t ps)
{
  return proc_stat_task_events_info(ps)->pageins;
}
struct ps_getter ps_pageins_getter =
{"pageins", PSTAT_TASK_EVENTS_INFO, (vf) ps_get_pageins};

static int 
ps_get_msgs_sent(proc_stat_t ps)
{
  return proc_stat_task_events_info(ps)->messages_sent;
}
struct ps_getter ps_msgs_sent_getter =
{"msgs_sent", PSTAT_TASK_EVENTS_INFO, (vf) ps_get_msgs_sent};

static int 
ps_get_msgs_rcvd(proc_stat_t ps)
{
  return proc_stat_task_events_info(ps)->messages_received;
}
struct ps_getter ps_msgs_rcvd_getter =
{"msgs_rcvd", PSTAT_TASK_EVENTS_INFO, (vf) ps_get_msgs_rcvd};

static int 
ps_get_zero_fills(proc_stat_t ps)
{
  return proc_stat_task_events_info(ps)->zero_fills;
}
struct ps_getter ps_zero_fills_getter =
{"zero_fills", PSTAT_TASK_EVENTS_INFO, (vf) ps_get_zero_fills};

/* ---------------------------------------------------------------- */
/* some printing functions */

/* G() is a helpful macro that just returns the getter G's access function
   cast into a function pointer returning TYPE, as how the function should be
   called varies depending on the getter */
#define G(g,type)((type (*)())ps_getter_function(g))

error_t
ps_emit_int(proc_stat_t ps, ps_getter_t getter, int width, FILE *stream, unsigned *count)
{
  return ps_write_int_field(G(getter, int)(ps), width, stream, count);
}

error_t
ps_emit_priority(proc_stat_t ps, ps_getter_t getter, int width, FILE *stream, unsigned *count)
{
  return
    ps_write_int_field(MACH_PRIORITY_TO_NICE(G(getter, int)(ps)),
		       width, stream, count);
}

error_t
ps_emit_num_blocks(proc_stat_t ps, ps_getter_t getter, int width, FILE
		   *stream, unsigned *count)
{
  char buf[20];
  sprintf(buf, "%d", G(getter, int)(ps) / 1024);
  return ps_write_field(buf, width, stream, count);
}

int 
sprint_frac_value(char *buf,
		  int value, int min_value_len,
		  int frac, int frac_scale,
		  int width)
{
  int value_len;
  int frac_len;

  if (value >= 100)		/* the integer part */
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
    sprintf(buf, "%d.%0*d", value, frac_len, frac);
  else
    sprintf(buf, "%d", value);

  return strlen(buf);
}

error_t
ps_emit_percent(proc_stat_t ps, ps_getter_t getter,
		int width, FILE *stream, unsigned *count)
{
  char buf[20];
  float perc = G(getter, float)(ps) * 100;

  if (width == 0)
    sprintf(buf, "%g", perc);
  else if (ABS(width) > 3)
    sprintf(buf, "%.*f", ABS(width) - 3, perc);
  else
    sprintf(buf, "%d", (int) perc);

  return ps_write_field(buf, width, stream, count);
}

/* prints its value nicely */
error_t
ps_emit_nice_int(proc_stat_t ps, ps_getter_t getter,
		 int width, FILE *stream, unsigned *count)
{
  char buf[20];
  int value = G(getter, int)(ps);
  char *sfx = " KMG";
  int frac = 0;

  while (value >= 1024)
    {
      frac = ((value & 0x3FF) * 1000) >> 10;
      value >>= 10;
      sfx++;
    }

  sprintf(buf + sprint_frac_value(buf, value, 1, frac, 3, ABS(width) - 1),
	  "%c", *sfx);

  return ps_write_field(buf, width, stream, count);
}

#define MINUTE	60
#define HOUR	(60*MINUTE)
#define DAY	(24*HOUR)
#define WEEK 	(7*DAY)

static int 
sprint_long_time(char *buf, int seconds, int width)
{
  char *p = buf;
  struct tscale
    {
      int length;
      char *sfx;
      char *short_sfx;
    }
  time_scales[] =
  {
    { WEEK,  " week", "wk"} ,
    { DAY,   " day",  "dy"} ,
    { HOUR,  " hour", "hr"} ,
    { MINUTE," min",   "m"} ,
    { 0}
  };
  struct tscale *ts = time_scales;

  while (ts->length > 0 && width > 0)
    {
      if (ts->length < seconds)
	{
	  int len;
	  int num = seconds / ts->length;
	  seconds %= ts->length;
	  sprintf(p, "%d%s", num, ts->sfx);
	  len = strlen(p);
	  width -= len;
	  if (width < 0 && p > buf)
	    break;
	  p += len;
	}
      ts++;
    }

  *p = '\0';

  return p - buf;
}

error_t
ps_emit_nice_seconds(proc_stat_t ps, ps_getter_t getter,
		     int width, FILE *stream, unsigned *count)
{
  char buf[20];
  time_value_t tv;

  G(getter, int)(ps, &tv);

  if (tv.seconds == 0)
    {
      if (tv.microseconds < 500)
	sprintf(buf, "%dus", tv.microseconds);
      else
	strcpy(buf
	       + sprint_frac_value(buf,
				   tv.microseconds / 1000, 1,
				   tv.microseconds % 1000, 3,
				   ABS(width) - 2),
	       "ms");
    }
  else if (tv.seconds < MINUTE)
    sprint_frac_value(buf, tv.seconds, 1, tv.microseconds, 6, ABS(width));
  else if (tv.seconds < HOUR)
    {
      /* 0:00.00... */
      int min_len;
      sprintf(buf, "%d:", tv.seconds / 60);
      min_len = strlen(buf);
      sprint_frac_value(buf + min_len,
			tv.seconds % 60, 2,
			tv.microseconds, 6,
			ABS(width) - min_len);
    }
  else
    sprint_long_time(buf, tv.seconds, width);

  return ps_write_field(buf, width, stream, count);
}

static int 
append_fraction(char *buf, int frac, int digits, int width)
{
  int slen = strlen(buf);
  int left = width - strlen(buf);
  if (left > 1)
    {
      buf[slen] = '.';
      left--;
      while (digits > left)
	frac /= 10, digits--;
      sprintf(buf + slen + 1, "%0*d", digits, frac);
      return slen + 1 + digits;
    }
  else
    return slen;
}

error_t
ps_emit_seconds(proc_stat_t ps, ps_getter_t getter, int width, FILE *stream,
		unsigned *count)
{
  int max = (width == 0 ? 999 : ABS(width));
  char buf[20];
  time_value_t tv;

  G(getter, void)(ps, &tv);

  if (tv.seconds > DAY)
    sprint_long_time(buf, tv.seconds, max);
  else if (tv.seconds > HOUR)
    if (max >= 8)
      {
	/* 0:00:00.00... */
	sprintf(buf, "%2d:%02d:%02d",
		tv.seconds / HOUR,
		(tv.seconds % HOUR) / MINUTE, (tv.seconds % MINUTE));
	append_fraction(buf, tv.microseconds, 6, max);
      }
    else
      sprint_long_time(buf, tv.seconds, max);
  else if (max >= 5 || tv.seconds > MINUTE)
    {
      /* 0:00.00... */
      sprintf(buf, "%2d:%02d", tv.seconds / MINUTE, tv.seconds % MINUTE);
      append_fraction(buf, tv.microseconds, 6, max);
    }
  else
    sprint_frac_value(buf, tv.seconds, 1, tv.microseconds, 6, max);

  return ps_write_field(buf, width, stream, count);
}

error_t
ps_emit_uid(proc_stat_t ps, ps_getter_t getter, int width, FILE *stream, unsigned *count)
{
  ps_user_t u = G(getter, ps_user_t)(ps);
  return ps_write_int_field(ps_user_uid(u), width, stream, count);
}

error_t
ps_emit_uname(proc_stat_t ps, ps_getter_t getter, int width, FILE *stream, unsigned *count)
{
  ps_user_t u = G(getter, ps_user_t)(ps);
  struct passwd *pw = ps_user_passwd(u);
  if (pw == NULL)
    return ps_write_int_field(ps_user_uid(u), width, stream, count);
  else
    return ps_write_field(pw->pw_name, width, stream, count);
}

/* prints a string with embedded nuls as spaces */
error_t
ps_emit_string0(proc_stat_t ps, ps_getter_t getter,
		int width, FILE *stream, unsigned *count)
{
  char *s0, *p, *q;
  int s0len;
  int fwidth = ABS(width);
  char static_buf[200];
  char *buf = static_buf;

  G(getter, void)(ps, &s0, &s0len);

  if (s0 == NULL)
    *buf = '\0';
  else
    {
      if (s0len > sizeof static_buf)
	{
	  buf = malloc(s0len + 1);
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
    error_t err = ps_write_field(buf, width, stream, count);
    if (buf != static_buf)
      free(buf);
    return err;
  }
}

error_t
ps_emit_string(proc_stat_t ps, ps_getter_t getter,
	       int width, FILE *stream, unsigned *count)
{
  char *str;
  int len;

  G(getter, void)(ps, &str, &len);

  if (str == NULL)
    str = "";
  else if (width != 0 && len > ABS(width))
    str[ABS(width)] = '\0';

  return ps_write_field(str, width, stream, count);
}

error_t
ps_emit_tty_name(proc_stat_t ps, ps_getter_t getter,
		 int width, FILE *stream, unsigned *count)
{
  char *name = "-";
  ps_tty_t tty = G(getter, ps_tty_t)(ps);

  if (tty)
    {
      name = ps_tty_short_name(tty);
      if (name == NULL || *name == '\0')
	name = "?";
    }

  return ps_write_field(name, width, stream, count);
}

struct state_shadow
{
  /* If any states in STATES are set, the states in shadow are suppressed.  */
  int states;
  int shadow;
};

struct state_shadow state_shadows[] = {
  /* Don't show sleeping thread if one is running, or the process is stopped.*/
  { PSTAT_STATE_T_RUN | PSTAT_STATE_P_STOP,
    PSTAT_STATE_T_SLEEP | PSTAT_STATE_T_IDLE | PSTAT_STATE_T_WAIT },
  /* Only show the longest sleep.  */
  { PSTAT_STATE_T_IDLE,		PSTAT_STATE_T_SLEEP | PSTAT_STATE_T_WAIT },
  { PSTAT_STATE_T_SLEEP,	PSTAT_STATE_T_WAIT },
  /* Turn off the per-thread stop bits when the process is stopped, as
     they're expected.  */
  { PSTAT_STATE_P_STOP,		PSTAT_STATE_T_HALT | PSTAT_STATE_T_UNCLEAN },
  { 0 }
};

error_t
ps_emit_state(proc_stat_t ps, ps_getter_t getter,
	      int width, FILE *stream, unsigned *count)
{
  char *tags;
  int raw_state = G(getter, int)(ps);
  int state = raw_state;
  char buf[20], *p = buf;
  struct state_shadow *shadow = state_shadows;

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

  return ps_write_field(buf, width, stream, count);
}

/* ---------------------------------------------------------------- */
/* comparison functions */

/* Evaluates CALL if both s1 & s2 are non-NULL, and otherwise returns -1, 0,
   or 1 ala strcmp, considering NULL to be less than non-NULL.  */
#define GUARDED_CMP(s1, s2, call) \
  ((s1) == NULL ? (((s2) == NULL) ? 0 : -1) : ((s2) == NULL ? 1 : (call)))

int 
ps_cmp_ints(proc_stat_t ps1, proc_stat_t ps2, ps_getter_t getter)
{
  int (*gf)() = G(getter, int);
  int v1 = gf(ps1), v2 = gf(ps2);
  return v1 == v2 ? 0 : v1 < v2 ? -1 : 1;
}

int 
ps_cmp_floats(proc_stat_t ps1, proc_stat_t ps2, ps_getter_t getter)
{
  float (*gf)() = G(getter, float);
  float v1 = gf(ps1), v2 = gf(ps2);
  return v1 == v2 ? 0 : v1 < v2 ? -1 : 1;
}

int 
ps_cmp_uids(proc_stat_t ps1, proc_stat_t ps2, ps_getter_t getter)
{
  ps_user_t (*gf)() = G(getter, ps_user_t);
  ps_user_t u1 = gf(ps1), u2 = gf(ps2);
  return ps_user_uid(u1) - ps_user_uid(u2);
}

int 
ps_cmp_unames(proc_stat_t ps1, proc_stat_t ps2, ps_getter_t getter)
{
  ps_user_t (*gf)() = G(getter, ps_user_t);
  ps_user_t u1 = gf(ps1), u2 = gf(ps2);
  struct passwd *pw1 = ps_user_passwd(u1), *pw2 = ps_user_passwd(u2);
  return GUARDED_CMP(pw1, pw2, strcmp(pw1->pw_name, pw2->pw_name));
}

int 
ps_cmp_strings(proc_stat_t ps1, proc_stat_t ps2, ps_getter_t getter)
{
  void (*gf)() = G(getter, void);
  char *s1, *s2;
  int s1len, s2len;

  /* Get both strings */
  gf(ps1, &s1, &s1len);
  gf(ps2, &s2, &s2len);

  return GUARDED_CMP(s1, s2, strncmp(s1, s2, MIN(s1len, s2len)));
}

/* ---------------------------------------------------------------- */
/* `Nominal' functions -- return true for `unexciting' values.  */

/* For many things, zero is not so interesting.  */
bool
ps_nominal_zint (proc_stat_t ps, ps_getter_t getter)
{
  return G(getter, int)(ps) == 0;
}

/* Priorities are similar, but have to be converted to the unix nice scale
   first.  */
bool
ps_nominal_pri (proc_stat_t ps, ps_getter_t getter)
{
  return MACH_PRIORITY_TO_NICE(G(getter, int)(ps)) == 0;
}

/* Hurd processes usually have 2 threads;  XXX is there someplace we get get
   this number from?  */
bool
ps_nominal_nth (proc_stat_t ps, ps_getter_t getter)
{
  return G(getter, int)(ps) == 2;
}

/* A user is nominal if it's the current user.  */
bool 
ps_nominal_user (proc_stat_t ps, ps_getter_t getter)
{
  static int own_uid = -1;
  ps_user_t u = G(getter, ps_user_t)(ps);

  if (own_uid < 0)
    own_uid = getuid();

  return u->uid == own_uid;
}

/* ---------------------------------------------------------------- */

ps_fmt_spec_t 
find_ps_fmt_spec(char *name, ps_fmt_spec_t specs)
{
  while (!ps_fmt_spec_is_end(specs))
    if (strcasecmp(ps_fmt_spec_name(specs), name) == 0)
      return specs;
    else
      specs++;
  return NULL;
}

/* ---------------------------------------------------------------- */

struct ps_fmt_spec ps_std_fmt_specs[] =
{
  {"PID",
   &ps_pid_getter,	   ps_emit_int,	    ps_cmp_ints,   0,		   -5},
  {"TH#",
   &ps_thread_index_getter,ps_emit_int,	    ps_cmp_ints,   0,		   -2},
  {"PPID",
   &ps_ppid_getter,	   ps_emit_int,     ps_cmp_ints,   0,		   -5},
  {"UID",
   &ps_owner_getter,	   ps_emit_uid,	    ps_cmp_uids,   ps_nominal_user,-5},
  {"User",
   &ps_owner_getter,	   ps_emit_uname,   ps_cmp_unames, ps_nominal_user, 8},
  {"NTh",
   &ps_num_threads_getter, ps_emit_int,	    ps_cmp_ints,   ps_nominal_nth, -2},
  {"PGrp",
   &ps_pgrp_getter,	   ps_emit_int,	    ps_cmp_ints,   0,		   -5},
  {"Sess",
   &ps_session_getter,     ps_emit_int,     ps_cmp_ints,   0,		   -5},
  {"LColl",
   &ps_login_col_getter,   ps_emit_int,     ps_cmp_ints,   0,		   -5},
  {"Args",
   &ps_args_getter,	   ps_emit_string0, ps_cmp_strings,0,		    0},
  {"Arg0",
   &ps_args_getter,	   ps_emit_string,  ps_cmp_strings,0,	            0},
  {"Time",
   &ps_tot_time_getter,    ps_emit_seconds, ps_cmp_ints,   0,		   -8},
  {"UTime",
   &ps_usr_time_getter,    ps_emit_seconds, ps_cmp_ints,   0,		   -8},
  {"STime",
   &ps_sys_time_getter,    ps_emit_seconds, ps_cmp_ints,   0,		   -8},
  {"VSize",
   &ps_vsize_getter,	   ps_emit_nice_int,ps_cmp_ints,   0,		   -5},
  {"RSize",
   &ps_rsize_getter,	   ps_emit_nice_int,ps_cmp_ints,   0,		   -5},
  {"Pri",
   &ps_cur_priority_getter,ps_emit_priority,ps_cmp_ints,   ps_nominal_pri, -3},
  {"BPri",
   &ps_base_priority_getter,ps_emit_priority,ps_cmp_ints,  ps_nominal_pri, -3},
  {"MPri",
   &ps_max_priority_getter,ps_emit_priority,ps_cmp_ints,   ps_nominal_pri, -3},
  {"%Mem",
   &ps_rmem_frac_getter,   ps_emit_percent, ps_cmp_floats, 0,		   -4},
  {"%CPU",
   &ps_cpu_frac_getter,    ps_emit_percent, ps_cmp_floats, 0,		   -4},
  {"State",
   &ps_state_getter,	   ps_emit_state,   0,   	   0,		    4},
  {"Sleep",
   &ps_sleep_getter,	   ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-2},
  {"Susp",
   &ps_susp_count_getter,  ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-2},
  {"PSusp",
   &ps_proc_susp_count_getter, ps_emit_int, ps_cmp_ints,   ps_nominal_zint,-2},
  {"TSusp",
   &ps_thread_susp_count_getter, ps_emit_int,ps_cmp_ints,  ps_nominal_zint,-2},
  {"TTY",
   &ps_tty_getter,	   ps_emit_tty_name,ps_cmp_strings,0,		    2},
  {"PgFlts",
   &ps_page_faults_getter, ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-5},
  {"COWFlts",
   &ps_cow_faults_getter,  ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-5},
  {"PgIns",
   &ps_pageins_getter,     ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-5},
  {"MsgIn",
   &ps_msgs_rcvd_getter,   ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-5},
  {"MsgOut",
   &ps_msgs_sent_getter,   ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-5},
  {"ZFills",
   &ps_zero_fills_getter,  ps_emit_int,	    ps_cmp_ints,   ps_nominal_zint,-5},
  {0}
};
