#ifndef _HACK_INTERRUPT_H_
#define _HACK_INTERRUPT_H_

#include <linux/netdevice.h>
#include "pfinet.h"

#define NET_BH 1

extern void mark_bh (int);

#endif
