#
# Mach Operating System
# Copyright (c) 1991,1990,1989 Carnegie Mellon University
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
# $Log:	Makefile,v $
# Revision 2.18  91/12/10  16:33:11  jsb
# 	New files from Intel
# 	[91/12/10  16:19:40  jsb]
# 
# Revision 2.17  91/09/04  11:29:10  jsb
# 	Use cc860 for i860.
# 	[91/09/04  11:23:25  jsb]
# 
# 	From Intel SSD: Added I860_CPP for i860 support.
# 	[91/09/02  08:57:36  jsb]
# 
# Revision 2.16  91/08/24  12:32:09  af
# 	Added MACHINE_CFLAGS switch to support the -Wf,-XNk150 flag for
# 	 mips string space. Apparently "danner/n3" is enough longer than
# 	 "dbg" to exhaust string space.
# 	[91/08/21            danner]
# 	Added better cpp technology to deal with gcc limitations. 
# 	      Currently only an issue on luna88k.
# 	[91/07/26            danner]
# 	Made use of -ES vs. -E machine dependent, as luna88k does not
# 	 like -ES.
# 	[91/07/19            danner]
# 
# Revision 2.15.2.2  91/08/21  17:23:08  danner
# 	Added MACHINE_CFLAGS switch to support the -Wf,-XNk150 flag for
# 	 mips string space. Apparently "danner/n3" is enough longer than
# 	 "dbg" to exhaust string space.
# 	[91/08/21            danner]
# 
# Revision 2.15.2.1  91/08/19  13:49:22  danner
# 	Added better cpp technology to deal with gcc limitations. 
# 	      Currently only an issue on luna88k.
# 	[91/07/26            danner]
# 	Made use of -ES vs. -E machine dependent, as luna88k does not
# 	 like -ES.
# 	[91/07/19            danner]
# 
# Revision 2.15  91/07/31  18:32:16  dbg
# 	Directly compile C files if INLINE is null (depends
# 	on the machine).
# 	Remove IBMRT-dependent fiddling for malloc.
# 	[91/07/30  17:32:13  dbg]
# 
# Revision 2.14  91/05/14  17:55:53  mrt
# 	Correcting copyright
# 
# Revision 2.13  91/02/14  14:19:15  mrt
# 	Added new Mach copyright
# 	[91/02/13  12:41:48  mrt]
# 
# Revision 2.12  90/11/05  14:36:32  rpd
# 	Change MACHINE to TARGET_MACHINE.
# 	[90/10/29            rwd]
# 
# Revision 2.11  90/09/09  14:34:39  rpd
# 	Remove call.o
# 	[90/08/24            rwd]
# 
# Revision 2.10  90/06/02  15:13:30  rpd
# 	Compile with -DMACH_IPC_COMPAT=0.
# 	[90/03/26  23:29:39  rpd]
# 
# Revision 2.9  90/05/29  18:40:08  rwd
# 	Fix PMAX_ASFLAGS.
# 	[90/05/22            rwd]
# 
# Revision 2.8  90/05/03  15:54:28  dbg
# 	Use -ES switch to preprocess assembler files.
# 	Not all machines' 'cc' can directly assemble .s files.
# 	[90/04/23            dbg]
# 
# Revision 2.7  90/01/19  14:36:46  rwd
# 	Added call.o
# 	[90/01/03            rwd]
# 
# Revision 2.6  89/12/08  19:53:01  rwd
# 	Use ASFLAGS for assembler, the mips one wants to know which
# 	optimization level the C code was compiled at.
# 	[89/12/06            af]
# 
# 	Changed CFLAGS to remove -g and add -O
# 	[89/10/24            rwd]
# 	Worked over for new merged coroutine-thread version.  Removed
# 	task version.
# 	[89/10/24            rwd]
# 
#

LIB = libthreads.a

COMMONFLAGS = -I. -DMACH_IPC_COMPAT=0
COPTS = -MD $(COMMONFLAGS) $(PROFILE)
CPPFLAGS = ${COPTS}
CPPSWITCH = -ES
DEFAULT_CPP = 	$(CC) $(CPPSWITCH) $(CPPFLAGS) 

PMAX_CFLAGS = -Wf,-XNk150
PMAX_ASFLAGS= -O -nocpp

LUNA88K_CPP =   cpp ${COPTS}

I860_CPP = /lib/cpp ${COPTS}
I860_CC = cc860
I860_AS = as860
I860_AR = ar860
I860_RANLIB = ranlib860

CFLAGS =  ${${TARGET_MACHINE}_CFLAGS} -O ${COPTS}
ASFLAGS = ${${TARGET_MACHINE}_ASFLAGS}
NCPP = $($(TARGET_MACHINE)_CPP?$($(TARGET_MACHINE)_CPP):$(DEFAULT_CPP)
CC = $($(TARGET_MACHINE)_CC?$($(TARGET_MACHINE)_CC):cc)
AS = $($(TARGET_MACHINE)_AS?$($(TARGET_MACHINE)_AS):as)
AR = $($(TARGET_MACHINE)_AR?$($(TARGET_MACHINE)_AR):ar)
RANLIB = $($(TARGET_MACHINE)_RANLIB?$($(TARGET_MACHINE)_RANLIB):ranlib)

I = -c -d -v

# Use the following definition to enable inline expansion.
#VAX_INLINE = awk -f cthread_inline.awk
#SUN3_INLINE = awk -f cthread_inline.awk

INLINE = ${${TARGET_MACHINE}_INLINE}

INLINE_C_RULE = \
	$(CC) $(CFLAGS) -S $*.c &&	\
	$(MV) $*.s $*.S &&		\
	$(INLINE) $*.S > $*.s &&	\
	$(AS) $(ASFLAGS) -o $@ $*.s &&	\
	$(RM) $(RMFLAGS) $*.s $*.S

NORMAL_C_RULE = \
	$(CC) $(CFLAGS) -c -o $@ $*.c

C_RULE = ${${TARGET_MACHINE}_INLINE?${INLINE_C_RULE}:${NORMAL_C_RULE}}

.SUFFIXES: .c .o

.c.o:
	${C_RULE}

SRCS = cprocs.c cthreads.c malloc.c \
       mig_support.c stack.c sync.c \
       machine/thread.c

OBJS = $(LIB)(cprocs.o) $(LIB)(cthreads.o) $(LIB)(malloc.o) \
       $(LIB)(mig_support.o) $(LIB)(stack.o) $(LIB)(sync.o) \
       $(LIB)(thread.o) $(LIB)(lock.o) $(LIB)(csw.o)

lib: $(LIB)

include: machine ckdir
	mach_install $I cthreads.h $(DSTBASE)/include

machine:
	-if [ -d machine ]; then true; else ln -s $(machine) machine; fi

clean:
	$(RM) $(RMFLAGS) machine lib*.a Makedep* a.out core errs \
			 *.d *.s *.S *.o *.BAK *.CKP */*.BAK */*.CKP

#
# Get inline expansion file before we do anything else.
#
.INIT:	cthread_inline.awk

cthread_inline.awk: machine/cthread_inline.awk
	-if [ -f $@ ]; then chmod 644 $@; else true; fi
	cp -p machine/$@ $@

# The following are the "generic" rules,
# parameterized by LIB, IMPL, and PROFILE.

$(LIB): $(OBJS)
	$(AR) r$(ARFLAGS) $(LIB) $?
	$(RANLIB) $(LIB)
	$(RM) $(RMFLAGS) $?
# Avoid complaints from md in case Makedep doesn't exist yet.
	touch Makedep
	md -m Makedep -d *.d
# Massage dependency file to reflect object files within library.
	cp Makedep Makedep.BAK
	sed 's/^[^ #$$]*\.o/$$(LIB)(&)/' < Makedep.BAK > Makedep
	rm Makedep.BAK

$(LIB)(thread.o): machine/thread.c
	$(CC) $(CFLAGS) -c machine/thread.c

$(LIB)(lock.o): machine/lock.s
	$(NCPP)machine/lock.s > $*.as
	$(AS) $(ASFLAGS) -o $@ $*.as
	$(RM) $(RMFLAGS) $*.as

$(LIB)(csw.o): machine/csw.s
	$(NCPP) machine/csw.s > $*.as
	$(AS) $(ASFLAGS) -o $@ $*.as
	$(RM) $(RMFLAGS) $*.as

-include Makedep
