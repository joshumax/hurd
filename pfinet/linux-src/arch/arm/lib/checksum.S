/*
 * linux/arch/arm/lib/checksum.S
 *
 * Copyright (C) 1995, 1996, 1997, 1998 Russell King
 */
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include "constants.h"

		.text

/* Function: __u32 csum_partial(const char *src, int len, __u32)
 * Params  : r0 = buffer, r1 = len, r2 = checksum
 * Returns : r0 = new checksum
 */

ENTRY(csum_partial)
		tst	r0, #2
		beq	1f
		subs	r1, r1, #2
		addmi	r1, r1, #2
		bmi	3f
		bic	r0, r0, #3
		ldr	r3, [r0], #4
		adds	r2, r2, r3, lsr #16
		adcs	r2, r2, #0
1:		adds	r2, r2, #0
		bics	ip, r1, #31
		beq	3f
		stmfd	sp!, {r4 - r6}
2:		ldmia	r0!, {r3 - r6}
		adcs	r2, r2, r3
		adcs	r2, r2, r4
		adcs	r2, r2, r5
		adcs	r2, r2, r6
		ldmia	r0!, {r3 - r6}
		adcs	r2, r2, r3
		adcs	r2, r2, r4
		adcs	r2, r2, r5
		adcs	r2, r2, r6
		sub	ip, ip, #32
		teq	ip, #0
		bne	2b
		adcs	r2, r2, #0
		ldmfd	sp!, {r4 - r6}
3:		ands	ip, r1, #0x1c
		beq	5f
4:		ldr	r3, [r0], #4
		adcs	r2, r2, r3
		sub	ip, ip, #4
		teq	ip, #0
		bne	4b
		adcs	r2, r2, #0
5:		ands	ip, r1, #3
		moveq	r0, r2
		RETINSTR(moveq,pc,lr)
		mov	ip, ip, lsl #3
		rsb	ip, ip, #32
		ldr	r3, [r0]
		mov	r3, r3, lsl ip
		adds	r2, r2, r3, lsr ip
		adc	r0, r2, #0
		RETINSTR(mov,pc,lr)

/* Function: __u32 csum_partial_copy_from_user (const char *src, char *dst, int len, __u32 sum, int *err_ptr)
 * Params  : r0 = src, r1 = dst, r2 = len, r3 = sum, [sp, #0] = &err
 * Returns : r0 = checksum, [[sp, #0], #0] = 0 or -EFAULT
 */
#if defined(CONFIG_CPU_32)

		.macro	save_regs
		stmfd	sp!, {r1 - r2, r4 - r8, fp, ip, lr, pc}
		.endm

#define LOAD_REGS(cond)					\
		LOADREGS(##cond##ea,fp,{r1 - r2, r4 - r8, fp, sp, pc})

		.macro	load1b,	reg1
9999:		ldrbt	\reg1, [r0], $1
		.section __ex_table, "a"
		.align	3
		.long	9999b, 6001f
		.previous
		.endm

		.macro	load2b, reg1, reg2
9999:		ldrbt	\reg1, [r0], $1
9998:		ldrbt	\reg2, [r0], $1
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.previous
		.endm

		.macro	load1l, reg1
9999:		ldrt	\reg1, [r0], $4
		.section __ex_table, "a"
		.align	3
		.long	9999b, 6001f
		.previous
		.endm

		.macro	load2l, reg1, reg2
9999:		ldrt	\reg1, [r0], $4
9998:		ldrt	\reg2, [r0], $4
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.previous
		.endm

		.macro	load4l, reg1, reg2, reg3, reg4
9999:		ldrt	\reg1, [r0], $4
9998:		ldrt	\reg2, [r0], $4
9997:		ldrt	\reg3, [r0], $4
9996:		ldrt	\reg4, [r0], $4
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.long	9997b, 6001f
		.long	9996b, 6001f
		.previous
		.endm

#elif defined(CONFIG_CPU_26)

		.macro	save_regs
		stmfd	sp!, {r1 - r2, r4 - r9, fp, ip, lr, pc}
		mov	r9, sp, lsr #13
		mov	r9, r9, lsl #13
		ldr	r9, [r9, #TSK_ADDR_LIMIT]
		mov	r9, r9, lsr #24
		.endm

#define LOAD_REGS(cond)					\
		LOADREGS(##cond##ea,fp,{r1 - r2, r4 - r9, fp, sp, pc})

		.macro	load1b,	reg1
		tst	r9, #0x01
9999:		ldreqbt	\reg1, [r0], #1
		ldrneb	\reg1, [r0], #1
		.section __ex_table, "a"
		.align	3
		.long	9999b, 6001f
		.previous
		.endm

		.macro	load2b, reg1, reg2
		tst	r9, #0x01
9999:		ldreqbt	\reg1, [r0], #1
		ldrneb	\reg1, [r0], #1
9998:		ldreqbt	\reg2, [r0], #1
		ldrneb	\reg2, [r0], #1
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.previous
		.endm

		.macro	load1l, reg1
		tst	r9, #0x01
9999:		ldreqt	\reg1, [r0], #4
		ldrne	\reg1, [r0], #4
		.section __ex_table, "a"
		.align	3
		.long	9999b, 6001f
		.previous
		.endm

		.macro	load2l, reg1, reg2
		tst	r9, #0x01
		ldmneia	r0!, {\reg1, \reg2}
9999:		ldreqt	\reg1, [r0], #4
9998:		ldreqt	\reg2, [r0], #4
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.previous
		.endm

		.macro	load4l, reg1, reg2, reg3, reg4
		tst	r9, #0x01
		ldmneia	r0!, {\reg1, \reg2, \reg3, \reg4}
9999:		ldreqt	\reg1, [r0], #4
9998:		ldreqt	\reg2, [r0], #4
9997:		ldreqt	\reg3, [r0], #4
9996:		ldreqt	\reg4, [r0], #4
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.long	9997b, 6001f
		.long	9996b, 6001f
		.previous
		.endm

#else
#error Unknown CPU architecture
#endif

ENTRY(csum_partial_copy_from_user)
		mov	ip, sp
		save_regs
		sub	fp, ip, #4
		cmp	r2, #4
		blt	.too_small_user
		tst	r1, #2			@ Test destination alignment
		beq	.dst_aligned_user
		subs	r2, r2, #2		@ We do not know if SRC is aligned...
		load2b	ip, r8
		orr	ip, ip, r8, lsl #8
		adds	r3, r3, ip
		adcs	r3, r3, #0
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1		@ Destination now aligned
.dst_aligned_user:
		tst	r0, #3
		bne	.src_not_aligned_user
		adds	r3, r3, #0
		bics	ip, r2, #15		@ Routine for src & dst aligned
		beq	2f
1:		load4l	r4, r5, r6, r7
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r4, r5
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		tst	ip, #4
		beq	4f
3:		load1l	r4
		str	r4, [r1], #4
		adcs	r3, r3, r4
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOAD_REGS(eq)
		load1l	r4
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
.exit:		tst	r2, #1
		strneb	r4, [r1], #1
		andne	r4, r4, #255
		adcnes	r3, r3, r4
		adcs	r0, r3, #0
		LOAD_REGS(al)

.too_small_user:
		teq	r2, #0
		LOAD_REGS(eq)
		cmp	r2, #2
		blt	.too_small_user1
		load2b	ip, r8
		orr	ip, ip, r8, lsl #8
		adds	r3, r3, ip
		strb	ip, [r1], #1
		strb	r8, [r1], #1
		tst	r2, #1
.too_small_user1:				@ C = 0
		beq	.csum_exit
		load1b	ip
		strb	ip, [r1], #1
		adcs	r3, r3, ip
.csum_exit:	adc	r0, r3, #0
		LOAD_REGS(al)

.src_not_aligned_user:
		cmp	r2, #4
		blt	.too_small_user
		and	ip, r0, #3
		bic	r0, r0, #3
		load1l	r4
		cmp	ip, #2
		beq	.src2_aligned_user
		bhi	.src3_aligned_user
		mov	r4, r4, lsr #8
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		mov	r6, r6, lsr #8
		orr	r6, r6, r7, lsl #24
		mov	r7, r7, lsr #8
		orr	r7, r7, r8, lsl #24
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #8
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #8
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lsl #24
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #8
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOAD_REGS(eq)
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		b	.exit

.src2_aligned_user:
		mov	r4, r4, lsr #16
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r7, lsl #16
		mov	r7, r7, lsr #16
		orr	r7, r7, r8, lsl #16
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #16
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #16
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lsl #16
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #16
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOAD_REGS(eq)
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		load1b	r4
		b	.exit

.src3_aligned_user:
		mov	r4, r4, lsr #24
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		mov	r6, r6, lsr #24
		orr	r6, r6, r7, lsl #8
		mov	r7, r7, lsr #24
		orr	r7, r7, r8, lsl #8
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #24
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #24
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lsl #8
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #24
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOAD_REGS(eq)
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		load1l	r4
		strb	r4, [r1], #1
		adcs	r3, r3, r4, lsl #24
		mov	r4, r4, lsr #8
		b	.exit

#if defined(CONFIG_CPU_32)
		.section .fixup,"ax"
#endif
		.align	4
6001:		mov	r4, #-EFAULT
		ldr	r5, [fp, #4]
		str	r4, [r5]
		ldmia	sp, {r1, r2}		@ retrieve original arguments
		add	r2, r2, r1
		mov	r3, #0			@ zero the buffer
6002:		teq	r2, r1
		strneb	r3, [r1], #1
		bne	6002b
		LOAD_REGS(al)
#if defined(CONFIG_CPU_32)
		.previous
#endif

/* Function: __u32 csum_partial_copy (const char *src, char *dst, int len, __u32 sum)
 * Params  : r0 = src, r1 = dst, r2 = len, r3 = checksum
 * Returns : r0 = new checksum
 */
ENTRY(csum_partial_copy_nocheck)
ENTRY(csum_partial_copy)
		mov	ip, sp
		stmfd	sp!, {r4 - r8, fp, ip, lr, pc}
		sub	fp, ip, #4
		cmp	r2, #4
		blt	Ltoo_small
		tst	r1, #2			@ Test destination alignment
		beq	Ldst_aligned
		subs	r2, r2, #2		@ We do not know if SRC is aligned...
		ldrb	ip, [r0], #1
		ldrb	r8, [r0], #1
		orr	ip, ip, r8, lsl #8
		adds	r3, r3, ip
		adcs	r3, r3, #0
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1		@ Destination now aligned
Ldst_aligned:	tst	r0, #3
		bne	Lsrc_not_aligned
		adds	r3, r3, #0
		bics	ip, r2, #15		@ Routine for src & dst aligned
		beq	3f
1:		ldmia	r0!, {r4, r5, r6, r7}
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
3:		ands	ip, r2, #12
		beq	5f
		tst	ip, #8
		beq	4f
		ldmia	r0!, {r4, r5}
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		tst	ip, #4
		beq	5f
4:		ldr	r4, [r0], #4
		str	r4, [r1], #4
		adcs	r3, r3, r4
5:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOADREGS(eqea,fp,{r4 - r8, fp, sp, pc})
		ldr	r4, [r0], #4
		tst	r2, #2
		beq	Lexit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		b	Lexit

Ltoo_small:	teq	r2, #0
		LOADREGS(eqea,fp,{r4 - r8, fp, sp, pc})
		cmp	r2, #2
		blt	Ltoo_small1
		ldrb	ip, [r0], #1
		ldrb	r8, [r0], #1
		orr	ip, ip, r8, lsl #8
		adds	r3, r3, ip
		strb	ip, [r1], #1
		strb	r8, [r1], #1
Lexit:		tst	r2, #1
Ltoo_small1:	ldrneb	ip, [r0], #1
		strneb	ip, [r1], #1
		adcnes	r3, r3, ip
		adcs	r0, r3, #0
		LOADREGS(ea,fp,{r4 - r8, fp, sp, pc})

Lsrc_not_aligned:
		cmp	r2, #4
		blt	Ltoo_small
		and	ip, r0, #3
		bic	r0, r0, #3
		ldr	r4, [r0], #4
		cmp	ip, #2
		beq	Lsrc2_aligned
		bhi	Lsrc3_aligned
		mov	r4, r4, lsr #8
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		ldmia	r0!, {r5, r6, r7, r8}
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		mov	r6, r6, lsr #8
		orr	r6, r6, r7, lsl #24
		mov	r7, r7, lsr #8
		orr	r7, r7, r8, lsl #24
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #8
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		ldmia	r0!, {r5, r6}
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #8
		tst	ip, #4
		beq	4f
3:		ldr	r5, [r0], #4
		orr	r4, r4, r5, lsl #24
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #8
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOADREGS(eqea,fp,{r4 - r8, fp, sp, pc})
		tst	r2, #2
		beq	Lexit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		b	Lexit

Lsrc2_aligned:	mov	r4, r4, lsr #16
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		ldmia	r0!, {r5, r6, r7, r8}
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r7, lsl #16
		mov	r7, r7, lsr #16
		orr	r7, r7, r8, lsl #16
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #16
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		ldmia	r0!, {r5, r6}
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #16
		tst	ip, #4
		beq	4f
3:		ldr	r5, [r0], #4
		orr	r4, r4, r5, lsl #16
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #16
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOADREGS(eqea,fp,{r4 - r8, fp, sp, pc})
		tst	r2, #2
		beq	Lexit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		ldrb	r4, [r0], #1
		b	Lexit

Lsrc3_aligned:	mov	r4, r4, lsr #24
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		ldmia	r0!, {r5, r6, r7, r8}
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		mov	r6, r6, lsr #24
		orr	r6, r6, r7, lsl #8
		mov	r7, r7, lsr #24
		orr	r7, r7, r8, lsl #8
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #24
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		ldmia	r0!, {r5, r6}
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #24
		tst	ip, #4
		beq	4f
3:		ldr	r5, [r0], #4
		orr	r4, r4, r5, lsl #8
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #24
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		LOADREGS(eqea,fp,{r4 - r8, fp, sp, pc})
		tst	r2, #2
		beq	Lexit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		ldr	r4, [r0], #4
		strb	r4, [r1], #1
		adcs	r3, r3, r4, lsl #24
		mov	r4, r4, lsr #8
		b	Lexit

ENTRY(__csum_ipv6_magic)
		stmfd	sp!, {lr}
		adds	ip, r2, r3
		ldmia	r1, {r1 - r3, lr}
		adcs	ip, ip, r1
		adcs	ip, ip, r2
		adcs	ip, ip, r3
		adcs	ip, ip, lr
		ldmia	r0, {r0 - r3}
		adcs	r0, ip, r0
		adcs	r0, r0, r1
		adcs	r0, r0, r2
		adcs	r0, r0, r3
		ldr	r3, [sp, #4]
		adcs	r0, r0, r3
		adcs	r0, r0, #0
		LOADREGS(fd, sp!, {pc})
