# 
# Mach Operating System
# Copyright (c) 1991,1990 Carnegie Mellon University
# All Rights Reserved.
# 
# Permission to use, copy, modify and distribute this software and its
# documentation is hereby granted, provided that both the copyright
# notice and this permission notice appear in all copies of the
# software, derivative works or modified versions, and any portions
# thereof, and that both notices appear in supporting documentation.
# 
# CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
# CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
# ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
# 
# Carnegie Mellon requests users of this software to return to
# 
#  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
#  School of Computer Science
#  Carnegie Mellon University
#  Pittsburgh PA 15213-3890
# 
# any improvements or extensions that they make and grant Carnegie Mellon
# the rights to redistribute these changes.
#
#
# HISTORY
# $Log:	cthread_inline.awk,v $
# Revision 2.5  91/05/14  17:57:03  mrt
# 	Correcting copyright
# 
# Revision 2.4  91/05/08  13:36:05  dbg
# 	Unlock lock with a locked instruction (xchg).
# 	[91/03/20            dbg]
# 
# Revision 2.3  91/02/14  14:20:06  mrt
# 	Added new Mach copyright
# 	[91/02/13  12:33:05  mrt]
# 
# Revision 2.2  90/05/03  15:54:56  dbg
#	Created (from 68020 version).
#	[90/02/05            dbg]
#
#	 Revision 2.2  89/12/08  19:54:30  rwd
#	 	Inlines are now spins instead of mutexes.
#	 	[89/10/23            rwd]
#	 
#	 Revision 2.1  89/08/04  15:15:14  rwd
#	 Created.
#	 
#	 Revision 1.3  89/05/05  19:00:33  mrt
#	 	Cleanup for Mach 2.5
#	 
#

# sun/cthread_inline.awk
#
# Awk script to inline critical C Threads primitives on i386

NF == 2 && $1 == "call" && $2 == "_spin_try_lock" {
	print	"/	BEGIN INLINE spin_try_lock"
	print	"	movl	(%esp),%ecx	/ point at mutex"
	print	"	movl	$1,%eax		/ set locked value in acc"
	print	"	xchg	%eax,(%ecx)	/ locked swap with mutex"
	print	"	xorl	$1,%eax		/ logical complement"
	print	"/	END INLINE spin_try_lock"
	continue
}
NF == 2 && $1 == "call" && $2 == "_spin_unlock" {
	print	"/	BEGIN INLINE " $2
	print	"	movl	(%esp),%ecx"
	print	"	xorl	%eax,%eax	/ set unlocked value in acc"
	print	"	xchg	%eax,(%ecx)	/ locked swap with mutex"
	print	"/	END INLINE " $2
	continue
}
NF == 2 && $1 == "call" && $2 == "_cthread_sp" {
	print	"/	BEGIN INLINE cthread_sp"
	print	"	movl	%esp,%eax"
	print	"/	END INLINE cthread_sp"
	continue
}
# default:
{
	print
}
