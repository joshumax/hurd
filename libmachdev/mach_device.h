/*
   Copyright (C) 2010 Free Software Foundation, Inc.
   Written by Zheng Da.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef	_MACHDEV_MACH_DEVICE_H
#define	_MACHDEV_MACH_DEVICE_H

/*
 * Generic device header.  May be allocated with the device,
 * or built when the device is opened.
 */
struct mach_device {
	struct port_info port;
	struct machdev_emul_device	dev;		/* the real device structure */
};
typedef	struct mach_device *mach_device_t;
#define	MACH_DEVICE_NULL ((mach_device_t)0)

#endif	/* _MACHDEV_MACH_DEVICE_H */
