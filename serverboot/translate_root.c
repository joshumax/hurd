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


#include "translate_root.h"

unsigned int atoh(ap)
        char *ap;
{
        register char *p;
        register unsigned int n;
        register int digit,lcase;

        p = ap;
        n = 0;
        while(*p == ' ')
                p++;
        while ((digit = (*p >= '0' && *p <= '9')) ||
                (lcase = (*p >= 'a' && *p <= 'f')) ||
                (*p >= 'A' && *p <= 'F')) {
                n *= 16;
                if (digit)      n += *p++ - '0';
                else if (lcase) n += 10 + (*p++ - 'a');
                else            n += 10 + (*p++ - 'A');
        }
        return(n);
}

/* 
 * Translate the root device from whatever strange encoding we might
 * be given.  Currently that includes BSD's slightly different name 
 * for IDE devices, and Linux's device number encoding (since that's 
 * what LILO passes us, for whatever reason).
 */
char *
translate_root(root_string)
        char *root_string;
{
	int linuxdev = atoh(root_string);

	/* LILO passes us a string representing the linux device number of
	 * our root device.  Since this is _not_ what we want, we'll make
	 * a stab at converting it.
	 *
	 * Linux major numbers we care about:
         *
         * 2  = fd
         * 3  = hd[ab]  (IDE channel 1)
         * 8  = sd
         * 22 = hd[cd]  (IDE channel 2)
         *
         */
	if (linuxdev) {
                if (LINUX_MAJOR(linuxdev) == 2) {
                        root_string[0] = 'f';
                        root_string[1] = 'd';
                        root_string[2] = LINUX_FD_DEVICE_NR(linuxdev) + '0';
                        root_string[3] = '\0';
                } else {
                        int shift;

                        switch (LINUX_MAJOR(linuxdev)) {
                        case 3:
                        case 22:
                                shift = 6;	
                                root_string[0] = 'h';
                                break;
                        case 8:
                                shift = 4;
                                root_string[0] = 's';
                                break;
                        default:
                                printf("Unknown linux device"
				       "(major = %d, minor = %d) passed as "
				       "root argument!\n"
				       "using hd0a as default.\n",
				       LINUX_MAJOR(linuxdev), 
				       LINUX_MINOR(linuxdev));
                                shift = 1;
                                root_string[0] = 'h';
                                linuxdev = 1;
                        }

                        root_string[1] = 'd';
                        root_string[2] = LINUX_DEVICE_NR(linuxdev, shift)+'0';
                        root_string[3] = LINUX_PARTN(linuxdev, shift)+'a' - 1;
                        root_string[4] = '\0';
                }
	} else 
		/* This could be handled much simpler in the BSD boot
		 * adapter code, but baford insists that the boot
		 * adapter code shouldn't be tainted by Mach's notion
		 * of the `correct' device naming.  Thus, we get wdxx
		 * instead of hdxx if booted from the BSD bootblocks,
		 * and this is the lame hack that tries to convert it.
		 */
		if (root_string[0] == 'w' && root_string[1] == 'd')
			root_string[0] = 'h';

        return root_string;
}



