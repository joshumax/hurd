#ifndef _HACK_ASM_SEGMENT_H_
#define _HACK_ASM_SEGMENT_H_

#include <sys/types.h>

#define get_fs_long(addr) get_user_long((int *)(addr))

unsigned long get_user_long (const int *addr);

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))

void put_user_long (unsigned long, int *);

void memcpy_fromfs (void *, void *, size_t);
void memcpy_tofs (void *, void *, size_t);

#endif
