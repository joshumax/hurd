#ifndef _LINUX_LP_INTERN_H_
#define _LINUX_LP_INTERN_H_

/*
 * split in two parts by Joerg Dorchain
 * usr/include/linux/lp.h  modified for Amiga by Michael Rausch
 * modified for Atari by Andreas Schwab
 * bug fixed by Jes Sorensen 18/8-94:
 *     It was not possible to compile the kernel only for Atari or Amiga.
 *
 * linux i386 version  c.1991-1992 James Wiegand
 * many modifications copyright (C) 1992 Michael K. Johnson
 * Interrupt support added 1993 Nigel Gamble
 */

#include <linux/types.h>
#include <linux/lp_m68k.h>

int lp_internal_init(void);

#endif
