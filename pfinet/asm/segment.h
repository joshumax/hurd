#ifndef _HACK_ASM_SEGMENT_H_
#define _HACK_ASM_SEGMENT_H_

#define get_fs_long(addr) get_user_long((int *)(addr))

u_long get_user_long (const int *addr);

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))

void put_user_long (u_long, int *);

