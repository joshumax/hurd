/* GNU Hurd RTC interface

   Copyright (C) 2024 Free Software Foundation, Inc.

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

#ifndef _RTC_H
#define _RTC_H	1

#include <hurd/ioctl.h>

struct rtc_time
{
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};
typedef struct rtc_time rtc_time_t;

#define _IOT_rtc_time _IOT(_IOTS(int),9,0,0,0,0)

/* ioctl calls that are permitted to the /dev/rtc interface, if
   any of the RTC drivers are enabled.  */

#define RTC_UIE_ON  _IO('p', 0x03) /* Update int. enable on.  */
#define RTC_UIE_OFF _IO('p', 0x04) /* ... off.  */

#define RTC_RD_TIME	_IOR('p', 0x09, struct rtc_time) /* Read RTC time.  */
#define RTC_SET_TIME	_IOW('p', 0x0a, struct rtc_time) /* Set RTC time.  */

#endif /* rtc.h */
