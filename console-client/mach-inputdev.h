/* mach-inputdev.h - Interfaces for the PC pc-kbd and mouse input drivers.
   Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
   Written by Marco Gerards.

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

/* This gross stuff is cut & pasted from Mach sources, as Mach doesn't
   export the interface we are using here.  */

/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef _INPUTDEV_H_
#define _INPUTDEV_H_ 1

#include <trans.h>

typedef u_short kev_type;               /* kd event type */

/* (used for event records) */
struct mouse_motion {
  short mm_deltaX;                /* units? */
  short mm_deltaY;
};
typedef u_char Scancode;

typedef struct {
  kev_type type;                  /* see below */
  struct timeval time;            /* timestamp */
  union {                         /* value associated with event */
    boolean_t up;           /* MOUSE_LEFT .. MOUSE_RIGHT */
    Scancode sc;            /* KEYBD_EVENT */
    struct mouse_motion mmotion;    /* MOUSE_MOTION */
  } value;
} kd_event;
#define m_deltaX        mmotion.mm_deltaX
#define m_deltaY        mmotion.mm_deltaY

/*
 * kd_event ID's.
 */
#define MOUSE_LEFT      1               /* mouse left button up/down */
#define MOUSE_MIDDLE    2
#define MOUSE_RIGHT     3
#define MOUSE_MOTION    4               /* mouse motion */
#define KEYBD_EVENT     5               /* key up/down */


#define IOCPARM_MASK    0x1fff          /* parameter length, at most 13 bits */
#define IOC_OUT         0x40000000      /* copy out parameters */
#define IOC_IN          0x80000000U     /* copy in parameters */

#ifndef _IOC
#define _IOC(inout,group,num,len) \
        (inout | ((len & IOCPARM_MASK) << 16) | ((group) << 8) | (num))
#endif
#ifndef _IOR
#define _IOR(g,n,t)     _IOC(IOC_OUT,   (g), (n), sizeof(t))
#endif
#ifndef _IOW
#define _IOW(g,n,t)     _IOC(IOC_IN,    (g), (n), sizeof(t))
#endif

#define KDSKBDMODE      _IOW('K', 1, int)       /* set keyboard mode */
#define KB_EVENT        1
#define KB_ASCII        2

#define KDGKBDTYPE      _IOR('K', 2, int)       /* get keyboard type */
#define KB_VANILLAKB    0

#define KDSETLEDS      _IOW('K', 5, int)        /* set keyboard leds */

/*
 * Low 3 bits of minor are the com port #.
 * The high 5 bits of minor are the mouse type
 */
#define MOUSE_SYSTEM_MOUSE      0
#define MICROSOFT_MOUSE         1
#define IBM_MOUSE               2
#define NO_MOUSE                3
#define LOGITECH_TRACKMAN       4
#define MICROSOFT_MOUSE7        5

#define DEV_COM0	"com0"
#define DEV_COM1	"com1"

/* End of Mach code.  */


/* Amount of times the device was opened.  Normally this translator
   should be only opened once.  */ 
extern int kbd_repeater_opened;

/* Place the keyboard event KEY in the keyboard buffer.  */
void kbd_repeat_key (kd_event *key);

/* Set the repeater translator.  The node will be named NODENAME and
   NODE will be filled with information about this node.  */
error_t kbd_setrepeater (const char *nodename, consnode_t *node);

#endif	/* _INPUTDEV_H_ */
