/*
 * Copyright (c) 1995 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *      Author: Stephen Clawson, University of Utah CSL
 */

#ifndef _TRANSLATE_ROOT_H_
#define _TRANSLATE_ROOT_H_

#define DEFAULT_ROOT "hd0a"

extern char *translate_root(char *);

#define LINUX_MAJOR(a) (int)((unsigned short)(a) >> 8)
#define LINUX_MINOR(a) (int)((unsigned short)(a) & 0xFF)

#define LINUX_PARTN(device, shift) \
        (LINUX_MINOR(device) & ((1 << (shift)) - 1))
#define LINUX_DEVICE_NR(device, shift) \
        (LINUX_MINOR(device) >> (shift))
#define LINUX_FD_DEVICE_NR(device) \
        ( ((device) & 3) | (((device) & 0x80 ) >> 5 ))

#endif /* _TRANSLATE_ROOT_H_ */
