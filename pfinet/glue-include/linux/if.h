#include <net/if.h>

#define IFF_VOLATILE    (IFF_LOOPBACK|IFF_POINTOPOINT|IFF_BROADCAST|IFF_ALLMULTI)
#define IFF_DYNAMIC     0x8000          /* dialup device with changing addresses*/
