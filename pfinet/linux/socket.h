#ifndef _HACK_SOCKET_H_
#define _HACK_SOCKET_H_

#include <sys/socket.h>
#include <sys/ioctl.h>

#define IP_MAX_MEMBERSHIPS 10

#define IPTOS_LOWDELAY 0x10
#define IPTOS_THROUGHPUT 0x08
#define IPTOS_RELIABILITY 0x04

#define SOPRI_INTERACTIVE 0
#define SOPRI_NORMAL 1
#define SOPRI_BACKGROUND 2

#define SOL_IP 0 /* XXX */

#define SO_NO_CHECK 11
#define SO_PRIORITY 12

#endif
