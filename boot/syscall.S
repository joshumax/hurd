/* Temporary....
   Copyright (C) 1993, 1995 Free Software Foundation

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

#include <i386/asm.h>

	.globl EXT(errno)
.text
ENTRY(syscall)
	pop %ecx
	pop %eax
	push %ecx
	lcall $7, $0
	jb error
	ret
error:
	movl %eax,EXT(errno)
	movl $-1,%eax
	ret
