#ifndef _HACK_ASM_SEGMENT_H_
#define _HACK_ASM_SEGMENT_H_

#include <sys/types.h>

#define get_fs_long(addr) (*(long *)(addr))
#define get_user_long(addr) (*(long *)(addr))

#define get_fs_byte(addr) (*(char *)(addr))
#define get_user_byte(addr) (*(char *)(addr))

#define put_fs_long(x,addr) (*(long *)(addr) = (x))
#define put_user_long(x,addr) (*(long *)(addr) = (x)

#define put_fs_byte(x,addr) (*(char *)(addr) = (x))
#define put_user_byte(x,addr) (*(char *)(addr) = (x))

#define memcpy_fromfs(a,b,s) (memcpy (a, b, s))
#define memcpy_tofs(a,b,s) (memcpy (a, b, s))

#endif
