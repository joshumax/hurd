#include <i386/asm.h>

.text
ENTRY(sigreturn)
	movl $0x67,%eax
	lcall $0x7,$0x0
	jb error
	ret
ENTRY(_sigreturn)
	addl $0xc,%esp
	call EXT(sigreturn)
	ret
ENTRY(sigvec)
	movl $0x6c,%eax
	movl $EXT(_sigreturn),%edx
	orl $0x80000000,%edx
	lcall $0x7,$0x0
	jb error
	ret
error:
        movl %eax,EXT(errno)
        movl $-1,%eax
        ret
