.globl _sigreturn
_sigreturn:
	movl $0x67,%eax
	lcall $0x7,$0x0
	ret

.globl __sigreturn
__sigreturn:
	addl $0xc,%esp
	call _sigreturn
	ret

.globl _sigvec
_sigvec:
	movl $0x6c,%eax
	movl $__sigreturn,%edx
	orl $0x80000000,%edx
	lcall $0x7,$0x0
	ret
