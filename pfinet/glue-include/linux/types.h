#ifndef _HACK_TYPES_H
#define _HACK_TYPES_H

#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>
#include <assert-backtrace.h>
#include <string.h>

#define	__u8	uint8_t
#define	__u16	uint16_t
#define	__u32	uint32_t
#define	__u64	uint64_t
#define	__s8	int8_t
#define	__s16	int16_t
#define	__s32	int32_t
#define	__s64	int64_t
#define	u8	uint8_t
#define	u16	uint16_t
#define	u32	uint32_t
#define	u64	uint64_t
#define	s8	int8_t
#define	s16	int16_t
#define	s32	int32_t
#define	s64	int64_t

#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/errno.h>

#endif
