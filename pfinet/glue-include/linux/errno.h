#ifndef _HACK_ERRNO_H
#define _HACK_ERRNO_H

#include <errno.h>
#include <hurd.h>

#define ERESTARTSYS	EINTR
#define ENOPKG		ENOSYS
#define ENOIOCTLCMD	ENOTTY

#undef	errno

#endif
