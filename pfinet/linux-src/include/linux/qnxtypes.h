/*
 *  Name                         : qnxtypes.h
 *  Author                       : Richard Frowijn
 *  Function                     : standard qnx types
 *  Version                      : 1.0
 *  Last modified                : 22-03-1998
 * 
 *  History                      : 22-03-1998 created
 * 
 */

#ifndef _QNX4TYPES_H
#define _QNX4TYPES_H

typedef unsigned short _nxtnt_t;
typedef unsigned char _ftype_t;

typedef struct {
	long xtnt_blk;
	long xtnt_size;
} _xtnt_t;

typedef unsigned short muid_t;
typedef unsigned short mgid_t;
typedef unsigned long qnx_off_t;
typedef unsigned short qnx_nlink_t;

#endif
