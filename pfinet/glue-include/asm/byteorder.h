/* Provide the specified-byte-order access functions used in the Linux
   kernel, implemented as macros in terms of the GNU libc facilities.  */

#ifndef _HACK_ASM_BYTEORDER_H
#define _HACK_ASM_BYTEORDER_H 1

#include <endian.h>
#include <byteswap.h>
#include <hurd.h>		/* gets other includes that need BYTE_ORDER */

#define BO_cvt(bits, from, to, x) \
  ((from) == (to) ? (u_int##bits##_t) (x) : bswap_##bits (x))
#define BO_cvtp(bits, from, to, p) \
  BO_cvt (bits, from, to, *(const u_int##bits##_t *) (p))
#define BO_cvts(bits, from, to, p) \
  ({ const u_int##bits##_t *_p = (p); *_p = BO_cvt (bits, from, to, *_p); })

#define	__cpu_to_le64(x)	BO_cvt (64, BYTE_ORDER, LITTLE_ENDIAN,	(x))
#define	__le64_to_cpu(x)	BO_cvt (64, LITTLE_ENDIAN, BYTE_ORDER,	(x))
#define	__cpu_to_le32(x)	BO_cvt (32, BYTE_ORDER, LITTLE_ENDIAN,	(x))
#define	__le32_to_cpu(x)	BO_cvt (32, LITTLE_ENDIAN, BYTE_ORDER,	(x))
#define	__cpu_to_le16(x)	BO_cvt (16, BYTE_ORDER, LITTLE_ENDIAN,	(x))
#define	__le16_to_cpu(x)	BO_cvt (16, LITTLE_ENDIAN, BYTE_ORDER,	(x))
#define	__cpu_to_be64(x)	BO_cvt (64, BYTE_ORDER, BIG_ENDIAN,	(x))
#define	__be64_to_cpu(x)	BO_cvt (64, BIG_ENDIAN, BYTE_ORDER,	(x))
#define	__cpu_to_be32(x)	BO_cvt (32, BYTE_ORDER, BIG_ENDIAN,	(x))
#define	__be32_to_cpu(x)	BO_cvt (32, BIG_ENDIAN, BYTE_ORDER,	(x))
#define	__cpu_to_be16(x)	BO_cvt (16, BYTE_ORDER, BIG_ENDIAN,	(x))
#define	__be16_to_cpu(x)	BO_cvt (16, BIG_ENDIAN, BYTE_ORDER,	(x))
#define	__cpu_to_le64p(p)	BO_cvtp (64, BYTE_ORDER, LITTLE_ENDIAN,	(p))
#define	__le64_to_cpup(p)	BO_cvtp (64, LITTLE_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_le32p(p)	BO_cvtp (32, BYTE_ORDER, LITTLE_ENDIAN,	(p))
#define	__le32_to_cpup(p)	BO_cvtp (32, LITTLE_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_le16p(p)	BO_cvtp (16, BYTE_ORDER, LITTLE_ENDIAN,	(p))
#define	__le16_to_cpup(p)	BO_cvtp (16, LITTLE_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_be64p(p)	BO_cvtp (64, BYTE_ORDER, BIG_ENDIAN,	(p))
#define	__be64_to_cpup(p)	BO_cvtp (64, BIG_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_be32p(p)	BO_cvtp (32, BYTE_ORDER, BIG_ENDIAN,	(p))
#define	__be32_to_cpup(p)	BO_cvtp (32, BIG_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_be16p(p)	BO_cvtp (16, BYTE_ORDER, BIG_ENDIAN,	(p))
#define	__be16_to_cpup(p)	BO_cvtp (16, BIG_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_le64s(p)	BO_cvts (64, BYTE_ORDER, LITTLE_ENDIAN,	(p))
#define	__le64_to_cpus(p)	BO_cvts (64, LITTLE_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_le32s(p)	BO_cvts (32, BYTE_ORDER, LITTLE_ENDIAN,	(p))
#define	__le32_to_cpus(p)	BO_cvts (32, LITTLE_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_le16s(p)	BO_cvts (16, BYTE_ORDER, LITTLE_ENDIAN,	(p))
#define	__le16_to_cpus(p)	BO_cvts (16, LITTLE_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_be64s(p)	BO_cvts (64, BYTE_ORDER, BIG_ENDIAN,	(p))
#define	__be64_to_cpus(p)	BO_cvts (64, BIG_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_be32s(p)	BO_cvts (32, BYTE_ORDER, BIG_ENDIAN,	(p))
#define	__be32_to_cpus(p)	BO_cvts (32, BIG_ENDIAN, BYTE_ORDER,	(p))
#define	__cpu_to_be16s(p)	BO_cvts (16, BYTE_ORDER, BIG_ENDIAN,	(p))
#define	__be16_to_cpus(p)	BO_cvts (16, BIG_ENDIAN, BYTE_ORDER,	(p))

#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
#define cpu_to_be64 __cpu_to_be64
#define be64_to_cpu __be64_to_cpu
#define cpu_to_be32 __cpu_to_be32
#define be32_to_cpu __be32_to_cpu
#define cpu_to_be16 __cpu_to_be16
#define be16_to_cpu __be16_to_cpu
#define cpu_to_le64p __cpu_to_le64p
#define le64_to_cpup __le64_to_cpup
#define cpu_to_le32p __cpu_to_le32p
#define le32_to_cpup __le32_to_cpup
#define cpu_to_le16p __cpu_to_le16p
#define le16_to_cpup __le16_to_cpup
#define cpu_to_be64p __cpu_to_be64p
#define be64_to_cpup __be64_to_cpup
#define cpu_to_be32p __cpu_to_be32p
#define be32_to_cpup __be32_to_cpup
#define cpu_to_be16p __cpu_to_be16p
#define be16_to_cpup __be16_to_cpup
#define cpu_to_le64s __cpu_to_le64s
#define le64_to_cpus __le64_to_cpus
#define cpu_to_le32s __cpu_to_le32s
#define le32_to_cpus __le32_to_cpus
#define cpu_to_le16s __cpu_to_le16s
#define le16_to_cpus __le16_to_cpus
#define cpu_to_be64s __cpu_to_be64s
#define be64_to_cpus __be64_to_cpus
#define cpu_to_be32s __cpu_to_be32s
#define be32_to_cpus __be32_to_cpus
#define cpu_to_be16s __cpu_to_be16s
#define be16_to_cpus __be16_to_cpus


#if BYTE_ORDER == BIG_ENDIAN
# define __BIG_ENDIAN_BITFIELD
#elif BYTE_ORDER == LITTLE_ENDIAN
# define __LITTLE_ENDIAN_BITFIELD
#else
# error __FOO_ENDIAN_BITFIELD
#endif


#include <netinet/in.h>		/* for htonl et al */

/* Though the optimized macros defined by glibc do the constant magic,
   there are places in the Linux code that use these in constant-only
   places like initializers, and the ({...}) expressions the macros use are
   not valid in those contexts.  */
#if BYTE_ORDER == BIG_ENDIAN
#	if !defined(__constant_htonl)
#		define __constant_htonl(x) (x)
#	endif
#	if !defined(__constant_htons)
#		define __constant_htons(x) (x)
#	endif
#elif BYTE_ORDER == LITTLE_ENDIAN
#	if !defined(__constant_htonl)
#		define __constant_htonl(x) \
        ((unsigned long int)((((unsigned long int)(x) & 0x000000ffU) << 24) | \
                             (((unsigned long int)(x) & 0x0000ff00U) <<  8) | \
                             (((unsigned long int)(x) & 0x00ff0000U) >>  8) | \
                             (((unsigned long int)(x) & 0xff000000U) >> 24)))
#	endif
#	if !defined(__constant_htons)
#		define __constant_htons(x) \
        ((unsigned short int)((((unsigned short int)(x) & 0x00ff) << 8) | \
                              (((unsigned short int)(x) & 0xff00) >> 8)))
#	endif
#else
#	error "Don't know if bytes are big- or little-endian!"
#endif

/* The transformation is the same in both directions.  */
#define	__constant_ntohl	__constant_htonl
#define	__constant_ntohs	__constant_htons


/* Some Linux code (e.g. <net/tcp.h>) uses #ifdef __BIG_ENDIAN et al.
   This is not real wonderful with the glibc definitions, where
   __BIG_ENDIAN et al are always defined (as values for __BYTE_ORDER).  */
#if BYTE_ORDER == BIG_ENDIAN
#undef	__LITTLE_ENDIAN
#elif BYTE_ORDER == LITTLE_ENDIAN
#undef	__BIG_ENDIAN
#endif
#undef	__PDP_ENDIAN

/* Since we've now broken anything that does glibc-style tests,
   make sure they break loudly rather than silently.  Any headers
   that need __BYTE_ORDER will just have to be included before
   we diddle with __BIG_ENDIAN or __LITTLE_ENDIAN above.  */
#undef	__BYTE_ORDER
#define	__BYTE_ORDER    ?????crash?????


#endif /* asm/byteorder.h */
