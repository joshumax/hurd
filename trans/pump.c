/* Initialize an Ethernet interface
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

/*
 * Copyright 1999 Red Hat Software, Inc.
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* This probably doesn't work on anything other then Ethernet! It may work
   on PLIP as well, but ARCnet and Token Ring are unlikely at best. */

#define DHCP_FLAG_NODAEMON	(1 << 0)
#define DHCP_FLAG_NOCONFIG	(1 << 1)
#define DHCP_FLAG_FORCEHNLOOKUP	(1 << 2)

#define RESULT_OKAY		0
#define RESULT_FAILED		1
#define RESULT_UNKNOWNIFACE	2

#define N_(foo) (foo)

#define PROGNAME "pump"
#define CONTROLSOCKET "/var/run/pump.sock"

#define _(foo) ((foo))
#include <stdarg.h>

void logMessage(char * mess, ...) {
    va_list args;

    va_start(args, mess);
    vprintf(mess, args);
    va_end(args);
    puts("");
}

typedef int bp_int32;
typedef short bp_int16;

#define BOOTP_OPTION_NETMASK		1
#define BOOTP_OPTION_GATEWAY		3
#define BOOTP_OPTION_DNS		6
#define BOOTP_OPTION_HOSTNAME		12
#define BOOTP_OPTION_BOOTFILE		13
#define BOOTP_OPTION_DOMAIN		15
#define BOOTP_OPTION_BROADCAST		28

#define DHCP_OPTION_REQADDR		50
#define DHCP_OPTION_LEASE		51
#define DHCP_OPTION_OVERLOAD		52
#define DHCP_OPTION_TYPE		53
#define DHCP_OPTION_SERVER		54
#define DHCP_OPTION_OPTIONREQ		55
#define DHCP_OPTION_MAXSIZE		57
#define DHCP_OPTION_T1			58

#define BOOTP_CLIENT_PORT	68
#define BOOTP_SERVER_PORT	67

#define BOOTP_OPCODE_REQUEST	1
#define BOOTP_OPCODE_REPLY	2

#define NORESPONSE		-10
#define DHCP_TYPE_DISCOVER	1
#define DHCP_TYPE_OFFER		2
#define DHCP_TYPE_REQUEST	3
#define DHCP_TYPE_DECLINE	4
#define DHCP_TYPE_ACK		5
#define DHCP_TYPE_NAK		6
#define DHCP_TYPE_RELEASE	7
#define DHCP_TYPE_INFORM	8

#define BOOTP_VENDOR_LENGTH	64
#define DHCP_VENDOR_LENGTH	312

struct bootpRequest {
    char opcode;
    char hw;
    char hwlength;
    char hopcount;
    bp_int32 id;
    bp_int16 secs;
    bp_int16 flags;
    bp_int32 ciaddr, yiaddr, server_ip, bootp_gw_ip;
    char hwaddr[16];
    char servername[64];
    char bootfile[128];
    char vendor[DHCP_VENDOR_LENGTH];
} ;

struct psuedohUdpHeader {
    bp_int32 source, dest;
    char zero;
    char protocol;
    bp_int16 len;
};

struct command {
    enum { CMD_STARTIFACE, CMD_RESULT, CMD_DIE, CMD_STOPIFACE, 
	   CMD_FORCERENEW, CMD_REQSTATUS, CMD_STATUS } type;
    union {
	struct {
	    char device[20];
	    int flags;
	    int reqLease;			/* in hours */
	    char reqHostname[200];
	} start;
	int result;				/* 0 for success */
	struct {
	    char device[20];
	} stop;
	struct {
	    char device[20];
	} renew;
	struct {
	    char device[20];
	} reqstatus;
	struct {
	    struct intfInfo intf;
	    char hostname[1024];
	    char domain[1024];
	    char bootFile[1024];
	} status;
    } u;
};

static char * doDhcp(char * device, int flags, int lease,
		     char * reqHostname, struct intfInfo * intf,
		     struct overrideInfo * override);

static const char vendCookie[] = { 99, 130, 83, 99, 255 };

static char * perrorstr(char * msg);
static char * setupInterface(struct intfInfo * intf, int s);
static char * prepareInterface(struct intfInfo * intf, int s);
static char * getInterfaceInfo(struct intfInfo * intf, int s);
static char * disableInterface(char * device);
static void parseReply(struct bootpRequest * breq, struct intfInfo * intf);
static char * prepareRequest(struct bootpRequest * breq,
			     int sock, char * device, time_t startTime);
static void parseLease(struct bootpRequest * bresp, struct intfInfo * intf);
static void initVendorCodes(struct bootpRequest * breq);
static char * handleTransaction(int s, struct overrideInfo * override, 
				struct bootpRequest * breq,
			        struct bootpRequest * bresp, 
			        struct sockaddr_in * serverAddr,
			        struct sockaddr_in * respondant,
				int useBootPacket, int dhcpResponseType);
static int dhcpMessageType(struct bootpRequest * response);
static void setMissingIpInfo(struct intfInfo * intf);
static int openControlSocket(char * configFile);
static int dhcpRenew(struct intfInfo * intf);
static int dhcpRelease(struct intfInfo * intf);

/* Various hurd-specific decls here */
#define _HURD_PFINET _HURD "pfinet"
io_t underlying_pfinet;
device_t ethernet_device;
char ethernet_address[ETH_ALEN];


/* Open the ethernet device */
char *
prepareInterface (struct intfInfo *intf, int s);
{
  u_int count;
  int addr[2];
  
  err = device_open (master_device, D_WRITE|D_READ, 
		     intf->device, &ethernet_device);
  if (err)
    return err;
  
  err = device_get_status (ethernet_device, NET_ADDRESS, addr, &count);
  if (err)
    return err;
  
  addr[0] = ntohl (addr[0]);
  addr[1] = ntohl (addr[1]);
  bcopy (addr, ethernet_address, ETH_ALEN);
  return 0;
}

/* Open the node underlying the pfinet translator, and set
   underlying_pfinet to a port to that file. */
error_t
open_underlying_pfinet ()
{
  char *path;
  
  if (underlying_pfinet != MACH_PORT_NULL)
    return 0;
  
  asprintf (&path, "%s/%d", _SERVERS_SOCKET, PF_INET);
  
  underlying_pfinet = file_name_lookup (path, O_NOTRANS, 0);
  free (path);
  return underlying_pfinet == MACH_PORT_NULL ? errno : 0;
}

/* Start pfinet with the specified arguments */
error_t
start_pfinet (char *argz, int argz_len)
{
  error_t open_function (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type)
    {
      int err;
      
      err = open_underlying_pfinet ();
      if (err)
	return err;
      
      *underlying = underlying_pfinet;
      *underlying_type = MACH_MSG_TYPE_COPY_SEND;
      return 0;
    }
  
  err = fshelp_start_translator (open_function, 
				   _HURD_PFINET, argz, argz_len,
				   60 * 1000, &control);
  if (err)
    return err;
  
  err = file_set_translator (underlying_pfinet,
			     0, FS_TRANS_SET,
			     FSYS_GOAWAY_FORCE,
			     argz, argz_len,
			     control, MACH_MSG_TYPE_MOVE_SEND);

  /* Force the C library to forget about any old cached server
     access port. */
  _hurd_socket_server (PFINET, 1);
}



int newKernel(void) {
#ifdef NOTHURD
    struct utsname ubuf;
    int major1, major2;

    uname(&ubuf);
    if (!strcasecmp(ubuf.sysname, "linux")) {
	if (sscanf(ubuf.release, "%d.%d", &major1, &major2) != 2 ||
		(major1 < 2) || (major1 == 2 && major2 == 0)) {
	    return 0;
	}
    }
#endif

    return 1;
}

static char * disableInterface(char * device) {
#ifdef NOTHURD
    struct ifreq req;
    int s;

    s = socket(AF_INET, SOCK_DGRAM, 0);

    strcpy(req.ifr_name, device);
    if (ioctl(s, SIOCGIFFLAGS, &req)) {
	close(s);
	return perrorstr("SIOCGIFFLAGS");
    }

    req.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
    if (ioctl(s, SIOCSIFFLAGS, &req)) {
	close(s);
	return perrorstr("SIOCSIFFLAGS");
    }

    close(s);

    return NULL;
#else
    error_t err;

    err = open_underlying_pfinet ();
    if (err)
      return strerror (err);
    
    err = file_set_translator (underlying_pfinet,
			       0, FS_TRANS_SET | FS_TRANS_FORCE,
			       FSYS_GOAWAY_FORCE,
			       0, 0, MACH_PORT_NULL);
    return err ? strerror (err) : 0;
#endif
}

static char * setupInterface(struct intfInfo * intf, int s) {
    char * rc;
    char * s;
    char * argz = 0;
    int argz_len = 0;

    /* Construct the args to pfinet. */
    /* /hurd/pfinet */
    argz_add (&argz, &argz_len, _HURD_PFINET);

    /* Interface name */
    asprintf (&s, "--interface=%s", intf->decive);
    argz_add (&argz, &argz_len, s);
    free (s);
    
    /* Address */
    asprintf (&s, "--address=%s", inet_ntoa (intf->ip));
    argz_add (&argz, &argz_len, s);
    free (s);
    
    /* Netmask */
    asprintf (&s, "--netmask=%s", inet_ntoa (intf->netmask));
    argz_add (&argz, &argz_len, s);
    free (s);
    
    /* Gateway */
    asprintf (&s, "--gateway=%s", inet_ntoa (intf->gateway));
    argz_add (&argz, &argz_len, s);
    free (s);
    
    /* Now set it up */
    err = start_pfinet (argz, argz_len);
    free (argz);

    return err ? strerror (err) : 0;


#ifdef NOTHURD
    struct sockaddr_in * addrp;
    struct ifreq req;
    struct rtentry route;

    if ((rc = disableInterface(intf->device))) return rc;

    /* we have to have basic information to get this far */
    addrp = (struct sockaddr_in *) &req.ifr_addr;
    addrp->sin_family = AF_INET;
    strcpy(req.ifr_name, intf->device);
   
    addrp->sin_addr = intf->ip;
    if (ioctl(s, SIOCSIFADDR, &req))
	return perrorstr("SIOCSIFADDR");

    addrp->sin_addr = intf->netmask;
    if (ioctl(s, SIOCSIFNETMASK, &req))
	return perrorstr("SIOCSIFNETMASK");

    addrp->sin_addr = intf->broadcast;
    if (ioctl(s, SIOCSIFBRDADDR, &req))
	return perrorstr("SIOCSIFBRDADDR");

    /* bring up the device, and specifically allow broadcasts through it */
    req.ifr_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    if (ioctl(s, SIOCSIFFLAGS, &req))
	return perrorstr("SIOCSIFFLAGS");

    /* add a route for this network */
    route.rt_dev = intf->device;
    route.rt_flags = RTF_UP;
    route.rt_metric = 0;

    if (!newKernel()) {
	addrp->sin_family = AF_INET;
	addrp->sin_port = 0;
	addrp->sin_addr = intf->network;
	memcpy(&route.rt_dst, addrp, sizeof(*addrp));
	addrp->sin_addr = intf->netmask;
	memcpy(&route.rt_genmask, addrp, sizeof(*addrp));

	if (ioctl(s, SIOCADDRT, &route)) {
	    /* the route cannot already exist, as we've taken the device down */
	    return perrorstr("SIOCADDRT 1");
	}
    }
    return NULL;
}

#ifdef NOTHURD
static char * setupDefaultGateway(struct in_addr * gw, int s) {
    struct sockaddr_in addr;
    struct rtentry route;

    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = INADDR_ANY;
    memcpy(&route.rt_dst, &addr, sizeof(addr));
    memcpy(&route.rt_genmask, &addr, sizeof(addr));
    addr.sin_addr = *gw;
    memcpy(&route.rt_gateway, &addr, sizeof(addr));
    
    route.rt_flags = RTF_UP | RTF_GATEWAY;
    route.rt_metric = 0;
    route.rt_dev = NULL;

    if (ioctl(s, SIOCADDRT, &route)) {
	/* the route cannot already exist, as we've taken the device 
	   down */
	return perrorstr("SIOCADDRT 2");
    }

    return NULL;
}

static char * getInterfaceInfo(struct intfInfo * intf, int s) {
    struct ifreq req;
    struct sockaddr_in * addrp;

    strcpy(req.ifr_name, intf->device);
    if (ioctl(s, SIOCGIFBRDADDR, &req))
	return perrorstr("SIOCGIFBRDADDR");

    addrp = (struct sockaddr_in *) &req.ifr_addr;
    intf->broadcast = addrp->sin_addr;
    intf->set = INTFINFO_HAS_BROADCAST;

    return NULL;
}

static char * prepareInterface(struct intfInfo * intf, int s) {
    struct sockaddr_in * addrp;
    struct ifreq req;
    struct rtentry route;

    addrp = (struct sockaddr_in *) &req.ifr_addr;

    strcpy(req.ifr_name, intf->device);
    addrp->sin_family = AF_INET;
    addrp->sin_port = 0;
    memset(&addrp->sin_addr, 0, sizeof(addrp->sin_addr));

    addrp->sin_family = AF_INET;
    addrp->sin_addr.s_addr = htonl(0);

    if (ioctl(s, SIOCSIFADDR, &req))
	return perrorstr("SIOCSIFADDR");

    if (!newKernel()) {
	if (ioctl(s, SIOCSIFNETMASK, &req))
	    return perrorstr("SIOCSIFNETMASK");

	/* the broadcast address is 255.255.255.255 */
	memset(&addrp->sin_addr, 255, sizeof(addrp->sin_addr));
	if (ioctl(s, SIOCSIFBRDADDR, &req))
	    return perrorstr("SIOCSIFBRDADDR");
    }

    req.ifr_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING;
    if (ioctl(s, SIOCSIFFLAGS, &req))
	return perrorstr("SIOCSIFFLAGS");

    memset(&route, 0, sizeof(route));
    memcpy(&route.rt_gateway, addrp, sizeof(*addrp));

    addrp->sin_family = AF_INET;
    addrp->sin_port = 0;
    addrp->sin_addr.s_addr = INADDR_ANY;
    memcpy(&route.rt_dst, addrp, sizeof(*addrp));
    memcpy(&route.rt_genmask, addrp, sizeof(*addrp));

    route.rt_dev = intf->device;
    route.rt_flags = RTF_UP;
    route.rt_metric = 0;

    if (ioctl(s, SIOCADDRT, &route)) {
	if (errno != EEXIST) {
	    close(s);
	    return perrorstr("SIOCADDRT 3");
	}
    }

    return NULL;
}
#endif

static int dhcpMessageType(struct bootpRequest * response) {
    unsigned char * chptr;
    unsigned char option, length;
   
    chptr = response->vendor;

    chptr += 4;
    while (*chptr != 0xFF) {
	option = *chptr++;
	if (!option) continue;
	length = *chptr++;
	if (option == DHCP_OPTION_TYPE)
	    return *chptr;

	chptr += length;
    }

    return -1;
}

static void setMissingIpInfo(struct intfInfo * intf) {
    bp_int32 ipNum = *((bp_int32 *) &intf->ip);
    bp_int32 nmNum = *((bp_int32 *) &intf->netmask);
    bp_int32 ipRealNum = ntohl(ipNum);

    if (!(intf->set & INTFINFO_HAS_NETMASK)) {
	if (((ipRealNum & 0xFF000000) >> 24) <= 127)
	    nmNum = 0xFF000000;
	else if (((ipRealNum & 0xFF000000) >> 24) <= 191)
	    nmNum = 0xFFFF0000;
	else 
	    nmNum = 0xFFFFFF00;
	*((bp_int32 *) &intf->netmask) = nmNum = htonl(nmNum);
	syslog (LOG_DEBUG, "intf: netmask: %s", inet_ntoa (intf->netmask));
    }

    if (!(intf->set & INTFINFO_HAS_BROADCAST)) {
	*((bp_int32 *) &intf->broadcast) = (ipNum & nmNum) | ~(nmNum);
	syslog (LOG_DEBUG, "intf: broadcast: %s", inet_ntoa (intf->broadcast));
    }

    if (!(intf->set & INTFINFO_HAS_NETWORK)) {
	*((bp_int32 *) &intf->network) = ipNum & nmNum;
	syslog (LOG_DEBUG, "intf: network: %s", inet_ntoa (intf->network));
    }

    intf->set |= INTFINFO_HAS_BROADCAST | INTFINFO_HAS_NETWORK | 
		 INTFINFO_HAS_NETMASK;
}

static void parseReply(struct bootpRequest * breq, struct intfInfo * intf) {
    unsigned int i;
    unsigned char * chptr;
    unsigned char option, length;

    syslog (LOG_DEBUG, "intf: device: %s", intf->device);
    syslog (LOG_DEBUG, "intf: set: %i", intf->set);
    syslog (LOG_DEBUG, "intf: bootServer: %s", inet_ntoa (intf->bootServer));
    syslog (LOG_DEBUG, "intf: reqLease: %i", intf->reqLease);

    i = ~(INTFINFO_HAS_IP | INTFINFO_HAS_NETMASK | INTFINFO_HAS_NETWORK |
	  INTFINFO_HAS_BROADCAST);
    intf->set &= i;

    if (strlen(breq->bootfile)) {
	intf->bootFile = strdup(breq->bootfile);
	intf->set |= INTFINFO_HAS_BOOTFILE;
    } else {
	intf->set &= ~INTFINFO_HAS_BOOTFILE;
    }
    syslog (LOG_DEBUG, "intf: bootFile: %s", intf->bootFile);

    memcpy(&intf->ip, &breq->yiaddr, 4);
    intf->set |= INTFINFO_HAS_IP;
    syslog (LOG_DEBUG, "intf: ip: %s", inet_ntoa (intf->ip));

    chptr = breq->vendor;
    chptr += 4;
    while (*chptr != 0xFF && (void *) chptr < (void *) breq->vendor + DHCP_VENDOR_LENGTH) {
	option = *chptr++;
	if (!option) continue;
	length = *chptr++;

	switch (option) {
	    case BOOTP_OPTION_DNS:
		intf->numDns = 0;
		for (i = 0; i < length; i += 4) {
		    if (intf->numDns < MAX_DNS_SERVERS) {
			memcpy(&intf->dnsServers[intf->numDns++], chptr + i, 4);
			syslog(LOG_DEBUG, "intf: dnsServers[%i]: %s", 
			       i/4, inet_ntoa (intf->dnsServers[i/4]));
		    }
		}
		intf->set |= NETINFO_HAS_DNS;
		syslog (LOG_DEBUG, "intf: numDns: %i", intf->numDns);
		break;

	    case BOOTP_OPTION_NETMASK:
		memcpy(&intf->netmask, chptr, 4);
		intf->set |= INTFINFO_HAS_NETMASK;
		syslog (LOG_DEBUG, "intf: netmask: %s", inet_ntoa (intf->netmask));
		break;

	    case BOOTP_OPTION_DOMAIN:
		if ((intf->domain = malloc(length + 1))) {
		    memcpy(intf->domain, chptr, length);
		    intf->domain[length] = '\0';
		    intf->set |= NETINFO_HAS_DOMAIN;
		    syslog (LOG_DEBUG, "intf: domain: %s", intf->domain);
		}
		break;

	    case BOOTP_OPTION_BROADCAST:
		memcpy(&intf->broadcast, chptr, 4);
		intf->set |= INTFINFO_HAS_BROADCAST;
		syslog (LOG_DEBUG, "intf: broadcast: %s", inet_ntoa (intf->broadcast));
		break;

	    case BOOTP_OPTION_GATEWAY:
		memcpy(&intf->gateway, chptr, 4);
		intf->set |= NETINFO_HAS_GATEWAY;
		syslog (LOG_DEBUG, "intf: gateway: %s", inet_ntoa (intf->gateway));
		break;

	    case BOOTP_OPTION_HOSTNAME:
		if ((intf->hostname = malloc(length + 1))) {
		    memcpy(intf->hostname, chptr, length);
		    intf->hostname[length] = '\0';
		    intf->set |= NETINFO_HAS_HOSTNAME;
		    syslog (LOG_DEBUG, "intf: hostname: %s", intf->hostname);
		}
		break;

	    case BOOTP_OPTION_BOOTFILE:
		/* we ignore this right now */
		break;

	    case DHCP_OPTION_OVERLOAD:
		/* FIXME: we should pay attention to this */
		logMessage("dhcp overload option is currently ignored!");
		break;
	}

	chptr += length;
    }

    setMissingIpInfo(intf);
}

static char * perrorstr(char * msg) {
    static char * err = NULL;
    static int errsize = 0;
    static int newsize;

    newsize = strlen(msg) + strlen(strerror(errno)) + 3;
    if (!errsize) {
	errsize = newsize;
	err = malloc(errsize);
    } else if (errsize < newsize) {
	free(err);
	errsize = newsize;
	err = malloc(errsize);
    } 
 
    if (err)
        sprintf(err, "%s: %s", msg, strerror(errno));
    else
	err = "out of memory!";

    return err;
}

static void initVendorCodes(struct bootpRequest * breq) {
    memcpy(breq->vendor, vendCookie, sizeof(vendCookie));
}

static char * prepareRequest(struct bootpRequest * breq,
			     int sock, char * device, time_t startTime) {
#ifdef NOTHURD
    struct ifreq req;
#endif
    int i;

    memset(breq, 0, sizeof(*breq));

    breq->opcode = BOOTP_OPCODE_REQUEST;

#ifdef NOTHURD
    strcpy(req.ifr_name, device);
    if (ioctl(sock, SIOCGIFHWADDR, &req))
	return perrorstr("SIOCSIFHWADDR");

    breq->hw = 1; 		/* ethernet */
    breq->hwlength = IFHWADDRLEN;	
    memcpy(breq->hwaddr, req.ifr_hwaddr.sa_data, IFHWADDRLEN);
#else
    breq->hw = 1; 		/* ethernet */
    breq->hwlength = IFHWADDRLEN;	
    memcpy(breq->hwaddr, ethernet_address, IFHWADDRLEN);
#endif

    /* we should use something random here, but I don't want to start using
       stuff from the math library */
    breq->id = time(NULL);
    for (i = 0; i < IFHWADDRLEN; i++)
	breq->id ^= breq->hwaddr[i] << (8 * (i % 4));

    breq->hopcount = 0;
    breq->secs = time(NULL) - startTime;

    initVendorCodes(breq);

    return NULL;
}

static unsigned int verifyChecksum(void * buf, int length, void * buf2,
				   int length2) {
    unsigned int csum;
    unsigned short * sp;

    csum = 0;
    for (sp = (unsigned short *) buf; length > 0; (length -= 2), sp++)
	csum += *sp;

    /* this matches rfc 1071, but not Steven's */
    if (length)
	csum += *((unsigned char *) sp);

    for (sp = (unsigned short *) buf2; length2 > 0; (length2 -= 2), sp++)
	csum += *sp;

    /* this matches rfc 1071, but not Steven's */
    if (length)
	csum += *((unsigned char *) sp);

    while (csum >> 16)
	csum = (csum & 0xffff) + (csum >> 16);

    if (csum!=0x0000 && csum != 0xffff) return 0; else return 1;
}

void debugbootpRequest(char *name, struct bootpRequest *breq)  {
    char vendor[28], vendor2[28];
    int i;
    struct in_addr address;
    unsigned char *vndptr;
    unsigned char option, length;
    
    syslog (LOG_DEBUG, "%s: opcode: %i", name, breq->opcode);
    syslog (LOG_DEBUG, "%s: hw: %i", name, breq->hw);
    syslog (LOG_DEBUG, "%s: hwlength: %i", name, breq->hwlength);
    syslog (LOG_DEBUG, "%s: hopcount: %i", name, breq->hopcount);
    syslog (LOG_DEBUG, "%s: id: 0x%8x", name, breq->id);
    syslog (LOG_DEBUG, "%s: secs: %i", name, breq->secs);
    syslog (LOG_DEBUG, "%s: flags: 0x%4x", name, breq->flags);
    
    address.s_addr = breq->ciaddr;
    syslog (LOG_DEBUG, "%s: ciaddr: %s", name, inet_ntoa (address));
    
    address.s_addr = breq->yiaddr;
    syslog (LOG_DEBUG, "%s: yiaddr: %s", name, inet_ntoa (address));
    
    address.s_addr = breq->server_ip;
    syslog (LOG_DEBUG, "%s: server_ip: %s", name, inet_ntoa (address));
    
    address.s_addr = breq->bootp_gw_ip;
    syslog (LOG_DEBUG, "%s: bootp_gw_ip: %s", name, inet_ntoa (address));
    
    syslog (LOG_DEBUG, "%s: hwaddr: %s", name, breq->hwaddr);
    syslog (LOG_DEBUG, "%s: servername: %s", name, breq->servername);
    syslog (LOG_DEBUG, "%s: bootfile: %s", name, breq->bootfile);
    
    vndptr = breq->vendor;
    sprintf (vendor, "0x%2x 0x%2x 0x%2x 0x%2x", *vndptr++, *vndptr++, *vndptr++, *vndptr++);
    syslog (LOG_DEBUG, "%s: vendor: %s", name, vendor);
    
    
    for (; (void *) vndptr < (void *) breq->vendor + DHCP_VENDOR_LENGTH;)
      {
	option = *vndptr++;
	if (option == 0xFF)
	  {
	    sprintf (vendor, "0x%2x", option);
	    vndptr = breq->vendor + DHCP_VENDOR_LENGTH;
	  }
	else if (option == 0x00)
	  {
	    for (i = 1; *vndptr == 0x00; i++, vndptr++);
	    sprintf (vendor, "0x%2x x %i", option, i);
	  }
	else
	  {
	    length = *vndptr++;
	    sprintf (vendor, "%3u %3u", option, length);
	    for (i = 0; i < length; i++)
	      {
		if (strlen (vendor) > 22)
		  {
		    syslog (LOG_DEBUG, "%s: vendor: %s", name, vendor);
		    strcpy (vendor, "++++++");
		  }
		snprintf (vendor2, 27, "%s 0x%2x", vendor, *vndptr++);
		strcpy (vendor, vendor2);
		
	      }
	  }
	
	syslog (LOG_DEBUG, "%s: vendor: %s", name, vendor);
      }

    return;

}

static char * handleTransaction(int s, struct overrideInfo * override, 
				struct bootpRequest * breq,
			        struct bootpRequest * bresp, 
			        struct sockaddr_in * serverAddr,
				struct sockaddr_in * respondant,
				int useBootpPacket, int dhcpResponseType) {
    struct timeval tv;
    fd_set readfs;
    int i, j;
    struct sockaddr_pkt tmpAddress;
    int gotit = 0;
    int tries;
    int nextTimeout = 2;
    time_t timeoutTime;
    int sin;
    int resend = 1;
    struct ethhdr;
    char ethPacket[ETH_FRAME_LEN];
    struct iphdr * ipHdr;
    struct udphdr * udpHdr;
    struct psuedohUdpHeader pHdr;
    time_t start = time(NULL);
    
    debugbootpRequest("breq", breq);

    if (!override) {
	override = alloca(sizeof(*override));
	initOverride(override);
    }

    tries = override->numRetries + 1;

    sin = socket(AF_PACKET, SOCK_DGRAM, ntohs(ETH_P_IP));
    if (sin < 0) {
	return strerror(errno);
    }

    while (!gotit && tries) {
	i = sizeof(*breq);
	if (useBootpPacket)
	    i -= (DHCP_VENDOR_LENGTH - BOOTP_VENDOR_LENGTH);

	if (resend) {
	    if (sendto(s, breq, i, 0, (struct sockaddr *) serverAddr, 
		       sizeof(*serverAddr)) != i) {
		close(sin);
		return perrorstr("sendto");
	    }

	    tries--;
	    nextTimeout *= 2;

	    switch (time(NULL) & 4) {
		case 0:	if (nextTimeout >= 2) nextTimeout--; break;
		case 1:	nextTimeout++; break;
	    }

	    timeoutTime = time(NULL) + nextTimeout;
	    i = override->timeout + start;
	    if (timeoutTime > i) timeoutTime = i;

	    resend = 0;
	}

	if (dhcpResponseType == NORESPONSE) {
	    close(sin);
	    return NULL;
	}

	tv.tv_usec = 0;
 	tv.tv_sec = timeoutTime - time(NULL);
	if (timeoutTime < time(NULL)) {
	    tries = 0;
	    continue;
	}

	FD_ZERO(&readfs);
	FD_SET(sin, &readfs);
	switch ((select(sin + 1, &readfs, NULL, NULL, &tv))) {
	  case 0:
	    resend = 1;
	    break;

	  case 1:
	    i = sizeof(tmpAddress);
	    if ((j = recvfrom(sin, ethPacket, sizeof(ethPacket), 0, 
		     (struct sockaddr *) &tmpAddress, &i)) < 0)
		return perrorstr("recvfrom");

	    /* We need to do some basic sanity checking of the header */
	    if (j < (sizeof(*ipHdr) + sizeof(*udpHdr))) continue;

	    ipHdr = (void *) ethPacket;
	    if (!verifyChecksum(NULL, 0, ipHdr, sizeof(*ipHdr)))
		continue;

	    if (ntohs(ipHdr->tot_len) > j)
		continue;
	    j = ntohs(ipHdr->tot_len);

	    if (ipHdr->protocol != IPPROTO_UDP) continue;

	    udpHdr = (void *) (ethPacket + sizeof(*ipHdr));
	    pHdr.source = ipHdr->saddr;
	    pHdr.dest = ipHdr->daddr;
	    pHdr.zero = 0;
	    pHdr.protocol = ipHdr->protocol;
	    pHdr.len = udpHdr->len;

/*
	    egcs bugs make this problematic

	    if (udpHdr->check && !verifyChecksum(&pHdr, sizeof(pHdr), 
 				udpHdr, j - sizeof(*ipHdr)))
	    continue;
*/

	    if (ntohs(udpHdr->source) != BOOTP_SERVER_PORT)
		continue;
	    if (ntohs(udpHdr->dest) != BOOTP_CLIENT_PORT) 
		continue;
	    /* Go on with this packet; it looks sane */

	  /* Originally copied sizeof (*bresp) - this is a security
	     problem due to a potential underflow of the source
	     buffer.  Also, it trusted that the packet was properly
	     0xFF terminated, which is not true in the case of the
	     DHCP server on Cisco 800 series ISDN router. */

	  memset (bresp, 0xFF, sizeof (*bresp));
	  memcpy (bresp, (char *) udpHdr + sizeof (*udpHdr), j - sizeof (*ipHdr) - sizeof (*udpHdr));

	    /* sanity checks */
	    if (bresp->id != breq->id) continue;
	    if (bresp->opcode != BOOTP_OPCODE_REPLY) continue;
	    if (bresp->hwlength != breq->hwlength) continue;
	    if (memcmp(bresp->hwaddr, breq->hwaddr, bresp->hwlength)) continue;
	    i = dhcpMessageType(bresp);
	    if (!(i == -1 && useBootpPacket) && (i != dhcpResponseType))
		continue;
	    if (memcmp(bresp->vendor, vendCookie, 4)) continue;
	    /* if (respondant) *respondant = tmpAddress; */
	    gotit = 1;

	    break;

	  default:
	    close(sin);
	    return perrorstr("select");
	}
    }

    if (!gotit) {
	close(sin);
	return _("No DHCP reply received");
    }

    close(sin);

    debugbootpRequest("bresp", bresp);

    return NULL;
}

static void addVendorCode(struct bootpRequest * breq, unsigned char option,
			  unsigned char length, void * data) {
    unsigned char * chptr;
    int theOption, theLength;

    chptr = breq->vendor;
    chptr += 4;
    while (*chptr != 0xFF && *chptr != option) {
	theOption = *chptr++;
	if (!theOption) continue;
	theLength = *chptr++;
	chptr += theLength;
    }

    *chptr++ = option;
    *chptr++ = length;
    memcpy(chptr, data, length);
    chptr[length] = 0xff;
}

static int getVendorCode(struct bootpRequest * bresp, unsigned char option,
			  void * data) {
    unsigned char * chptr;
    unsigned int length, theOption;

    chptr = bresp->vendor;
    chptr += 4;
    while (*chptr != 0xFF && *chptr != option) {
	theOption = *chptr++;
	if (!theOption) continue;
	length = *chptr++;
	chptr += length;
    }

    if (*chptr++ == 0xff) return 1;

    length = *chptr++;
    memcpy(data, chptr, length);

    return 0;
}

static int createSocket(void) {
    struct sockaddr_in clientAddr;
    int s;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
	return -1;

    memset(&clientAddr.sin_addr, 0, sizeof(&clientAddr.sin_addr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(BOOTP_CLIENT_PORT);	/* bootp client */

    if (bind(s, (struct sockaddr *) &clientAddr, sizeof(clientAddr))) {
	close(s); 
	return -1;
    }

    return s;
}

static int dhcpRelease(struct intfInfo * intf) {
    struct bootpRequest breq, bresp;
    unsigned char messageType;
    struct sockaddr_in serverAddr;
    char * chptr;
    int s;

    if (!(intf->set & INTFINFO_HAS_LEASE)) {
	disableInterface(intf->device);
	syslog(LOG_INFO, "disabling interface %s", intf->device);

	return 0;
    }

    if ((s = createSocket()) < 0) return 1;

    if ((chptr = prepareRequest(&breq, s, intf->device, time(NULL)))) {
	close(s);
	while (1) {
	    disableInterface(intf->device);
	    return 0;
	}
    }

    messageType = DHCP_TYPE_RELEASE;
    addVendorCode(&breq, DHCP_OPTION_TYPE, 1, &messageType);
    memcpy(&breq.ciaddr, &intf->ip, sizeof(breq.ciaddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(BOOTP_SERVER_PORT);	/* bootp server */
    serverAddr.sin_addr = intf->bootServer;

    if (!handleTransaction(s, NULL, &breq, &bresp, &serverAddr, NULL, 0,
			   NORESPONSE)) {
	disableInterface(intf->device);
	close(s);
	return 0;
    }

    disableInterface(intf->device);
    close(s);

    syslog(LOG_INFO, "disabling interface %s", intf->device);

    return 1;
}
    
/* This is somewhat broken. We try only to renew the lease. If we fail,
   we don't try to completely rebind. This doesn't follow the DHCP spec,
   but for the install it should be a reasonable compromise. */
static int dhcpRenew(struct intfInfo * intf) {
    struct bootpRequest breq, bresp;
    unsigned char messageType;
    struct sockaddr_in serverAddr;
    char * chptr;
    int s;
    int i;

    s = createSocket();

    if ((chptr = prepareRequest(&breq, s, intf->device, time(NULL)))) {
	close(s);
	while (1);	/* problem */
    }

    messageType = DHCP_TYPE_REQUEST;
    addVendorCode(&breq, DHCP_OPTION_TYPE, 1, &messageType);
    memcpy(&breq.ciaddr, &intf->ip, sizeof(breq.ciaddr));

    i = htonl(intf->reqLease);
    addVendorCode(&breq, DHCP_OPTION_LEASE, 4, &i);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(BOOTP_SERVER_PORT);	/* bootp server */
    serverAddr.sin_addr = intf->bootServer;

    if (handleTransaction(s, NULL, &breq, &bresp, &serverAddr, NULL, 0,
			   DHCP_TYPE_ACK)) {
	close(s);
	return 1;
    }

    parseLease(&bresp, intf);

    syslog(LOG_INFO, "renewed lease for interface %s", intf->device);

    close(s);
    return 0;
}

static void parseLease(struct bootpRequest * bresp, struct intfInfo * intf) {
    int lease;
    time_t now;

    intf->set &= INTFINFO_HAS_LEASE;
    if (getVendorCode(bresp, DHCP_OPTION_LEASE, &lease)) 
	return;

    lease = ntohl(lease);

    if (lease && lease != 0xffffffff) {
	now = time(NULL);
	intf->set |= INTFINFO_HAS_LEASE;
	intf->leaseExpiration = now + lease;
	intf->renewAt = now + (7 * lease / 8);
    }
}

char * readSearchPath(void) {
    int fd;
    struct stat sb;
    char * buf;
    char * start;

    fd = open("/etc/resolv.conf", O_RDONLY);
    if (fd < 0) return NULL;

    fstat(fd, &sb);
    buf = alloca(sb.st_size + 2);
    if (read(fd, buf, sb.st_size) != sb.st_size) return NULL;
    buf[sb.st_size] = '\n';
    buf[sb.st_size + 1] = '\0';
    close(fd);

    start = buf;
    while (start && *start) {
	while (isspace(*start) && (*start != '\n')) start++;
	if (*start == '\n') {
	    start++;
	    continue;
	}

	if (!strncmp("search", start, 6) && isspace(start[6])) {
	    start += 6;
	    while (isspace(*start) && *start != '\n') start++;
	    if (*start == '\n') return NULL;

	    buf = strchr(start, '\n');
	    *buf = '\0';
	    return strdup(start);
	}
    }

    return NULL;
}

static void createResolvConf(struct intfInfo * intf, char * domain,
			     int isSearchPath) {
    FILE * f;
    int i;
    char * chptr;

    /* force a reread of /etc/resolv.conf if we need it again */
    res_close();

    if (!domain) {
	domain = readSearchPath();
 	if (domain) {
	    chptr = alloca(strlen(domain) + 1);
	    strcpy(chptr, domain);
	    free(domain);
	    domain = chptr;
	    isSearchPath = 1;
	}
    }

    f = fopen("/etc/resolv.conf", "w");
    if (!f) {
	syslog(LOG_ERR, "cannot create /etc/resolv.conf: %s\n",
	       strerror(errno));
	return;
    }

    if (domain && isSearchPath) {
	fprintf(f, "search %s\n", domain);
    } else if (domain) {
	fprintf(f, "search");
	chptr = domain;
	do {
	    fprintf(f, " %s", chptr);
	    chptr = strchr(chptr, '.');
	    if (chptr) {
		chptr++;
		if (!strchr(chptr, '.'))
		    chptr = NULL;
	    }
	} while (chptr);

	fprintf(f, "\n");
    }

    for (i = 0; i < intf->numDns; i++)
	fprintf(f, "nameserver %s\n", inet_ntoa(intf->dnsServers[i]));

    fclose(f);

    /* force a reread of /etc/resolv.conf */
    endhostent();
}

void setupDns(struct intfInfo * intf, struct overrideInfo * override) {
    char * hn, * dn = NULL;
    struct hostent * he;

    if (override->flags & OVERRIDE_FLAG_NODNS) {
	return;
    }

    if (override->searchPath) {
	createResolvConf(intf, override->searchPath, 1);
	return;
    }

    if (intf->set & NETINFO_HAS_DNS) {
	if (!(intf->set & NETINFO_HAS_DOMAIN))  {
	    if (intf->set & NETINFO_HAS_HOSTNAME) {
		hn = intf->hostname;
	    } else {
		createResolvConf(intf, NULL, 0);

		he = gethostbyaddr((char *) &intf->ip, sizeof(intf->ip),
				   AF_INET);
		if (he) {
		    hn = he->h_name;
		} else {
		    hn = NULL;
		}
	    }

	    if (hn) {
		dn = strchr(hn, '.');
		if (dn)
		    dn++;
	    }
	} else {
	    dn = intf->domain;
	}

	createResolvConf(intf, dn, 0);
    }
}

static char * doDhcp(char * device, int flags, int reqLease,
		     char * reqHostname, struct intfInfo * intf,
		     struct overrideInfo * override) {
    int s, i;
    struct sockaddr_in serverAddr;
    struct sockaddr_in clientAddr;
    struct sockaddr_in broadcastAddr;
    struct bootpRequest breq, bresp;
    struct bootpRequest protoReq;
    unsigned char * chptr;
    unsigned char messageType;
    time_t startTime = time(NULL);
    int true = 1;
    char optionsRequested[50];
    int numOptions;
    short aShort;

    memset(intf, 0, sizeof(*intf));
    strcpy(intf->device, device);
    intf->reqLease = reqLease;
    intf->set |= INTFINFO_HAS_REQLEASE;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
	return perrorstr("socket");
    }

    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &true, sizeof(true))) {
	close(s);
	return perrorstr("setsockopt");
    }

    if (flags & DHCP_FLAG_NOCONFIG) {
	if ((chptr = getInterfaceInfo(intf, s))) {
	    close(s);
 	    return chptr;
	}
    } else if ((chptr = prepareInterface(intf, s))) {
	close(s);
	return chptr;
    }

    if ((chptr = prepareRequest(&breq, s, intf->device, startTime))) {
	close(s);
	disableInterface(intf->device);
	return chptr;
    }

    messageType = DHCP_TYPE_DISCOVER;
    addVendorCode(&breq, DHCP_OPTION_TYPE, 1, &messageType);

    memset(&clientAddr.sin_addr, 0, sizeof(&clientAddr.sin_addr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(BOOTP_CLIENT_PORT);	/* bootp client */

    if (bind(s, (struct sockaddr *) &clientAddr, sizeof(clientAddr))) {
	disableInterface(intf->device);
	close(s);
	return perrorstr("bind");
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(BOOTP_SERVER_PORT);	/* bootp server */

#if 0
    /* seems like a good idea?? */
    if (intf->set & INTFINFO_HAS_BOOTSERVER)
	serverAddr.sin_addr = intf->bootServer;
#endif 

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(BOOTP_SERVER_PORT);

#if 0
    /* this too! */
    if (intf->set & INTFINFO_HAS_BROADCAST)
	broadcastAddr.sin_addr = intf->broadcast;
#endif

    memset(&broadcastAddr.sin_addr, 0xff, 
	   sizeof(broadcastAddr.sin_addr));  /* all 1's broadcast */

    syslog (LOG_DEBUG, "PUMP: sending discover\n");

    if ((chptr = handleTransaction(s, override, &breq, &bresp, &broadcastAddr,
				   NULL, 1, DHCP_TYPE_OFFER))) {
	close(s);
	disableInterface(intf->device);
	return chptr;
    }

    /* Otherwise we're in the land of bootp */
    if (dhcpMessageType(&bresp) == DHCP_TYPE_OFFER) {
	/* Admittedly, this seems a bit odd. If we find a dhcp server, we
	   rerun the dhcp discover broadcast, but with the proper option
	   field this time. This makes me rfc compliant. */
	syslog (LOG_DEBUG, "got dhcp offer\n");

	initVendorCodes(&breq);

	aShort = ntohs(sizeof(struct bootpRequest));
	addVendorCode(&breq, DHCP_OPTION_MAXSIZE, 2, &aShort);

	numOptions = 0;
	optionsRequested[numOptions++] = BOOTP_OPTION_NETMASK;
	optionsRequested[numOptions++] = BOOTP_OPTION_GATEWAY;
	optionsRequested[numOptions++] = BOOTP_OPTION_DNS;
	optionsRequested[numOptions++] = BOOTP_OPTION_DOMAIN;
	optionsRequested[numOptions++] = BOOTP_OPTION_BROADCAST;
	optionsRequested[numOptions++] = BOOTP_OPTION_HOSTNAME;
	addVendorCode(&breq, DHCP_OPTION_OPTIONREQ, numOptions, 
		      optionsRequested);

	if (reqHostname) {
	    syslog(LOG_DEBUG, "HOSTNAME: requesting %s\n", reqHostname);
	    addVendorCode(&breq, BOOTP_OPTION_HOSTNAME, strlen(reqHostname), 
			  reqHostname);
	}

	i = htonl(intf->reqLease);
	addVendorCode(&breq, DHCP_OPTION_LEASE, 4, &i);

	protoReq = breq;

	syslog (LOG_DEBUG, "PUMP: sending second discover");

	messageType = DHCP_TYPE_DISCOVER;
	addVendorCode(&breq, DHCP_OPTION_TYPE, 1, &messageType);

	/* Send another DHCP_REQUEST with the proper option list */
	if ((chptr = handleTransaction(s, override, &breq, &bresp, 
				       &broadcastAddr, NULL, 1, 
				       DHCP_TYPE_OFFER))) {
	    close(s);
	    disableInterface(intf->device);
	    return chptr;
	}


	if (dhcpMessageType(&bresp) != DHCP_TYPE_OFFER) {
	    close(s);
	    disableInterface(intf->device);
	    return "dhcp offer expected";
	}

	syslog (LOG_DEBUG, "PUMP: got an offer");

	if (getVendorCode(&bresp, DHCP_OPTION_SERVER, &serverAddr.sin_addr)) {
	    syslog (LOG_DEBUG, "DHCPOFFER didn't include server address");
	    intf->bootServer = broadcastAddr.sin_addr;
	}

	breq = protoReq;
	messageType = DHCP_TYPE_REQUEST;
	addVendorCode(&breq, DHCP_OPTION_TYPE, 1, &messageType);

	addVendorCode(&breq, DHCP_OPTION_SERVER, 4, &serverAddr.sin_addr);
	addVendorCode(&breq, DHCP_OPTION_REQADDR, 4, &bresp.yiaddr);

	/* why do we need to use the broadcast address here? better reread the
	   spec! */
	if ((chptr = handleTransaction(s, override, &breq, &bresp, 
				       &broadcastAddr, NULL, 0, 
				       DHCP_TYPE_ACK))) {
	    close(s);
	    disableInterface(intf->device);
	    return chptr;
	}

	syslog (LOG_DEBUG, "PUMP: got lease");

	parseLease(&bresp, intf);

	if (getVendorCode(&bresp, DHCP_OPTION_SERVER, &intf->bootServer)) {
	    syslog (LOG_DEBUG, "DHCPACK didn't include server address");
	    intf->bootServer = broadcastAddr.sin_addr;
	}

	intf->set |= INTFINFO_HAS_BOOTSERVER;
    }

    parseReply(&bresp, intf);
    if (flags & DHCP_FLAG_FORCEHNLOOKUP)
	intf->set &= ~(NETINFO_HAS_DOMAIN | NETINFO_HAS_HOSTNAME);

    chptr = setupInterface(intf, s);
    if (chptr) {
	close(s);
	disableInterface(intf->device);
	return chptr;
    }

    syslog(LOG_INFO, "configured interface %s", intf->device);

    if (intf->set & NETINFO_HAS_GATEWAY) {
	chptr = setupDefaultGateway(&intf->gateway, s);
    }

    setupDns(intf, override);

    close(s);

    return NULL;
}

static void runDaemon(int sock, char * configFile) {
    int conn;
    struct sockaddr_un addr;
    int addrLength;
    struct command cmd;
    struct intfInfo intf[20];
    int numInterfaces = 0;
    int i;
    int closest;
    struct timeval tv;
    fd_set fds;
    struct overrideInfo * overrides = NULL;
    struct overrideInfo emptyOverride, * o;

    readPumpConfig(configFile, &overrides);
    if (!overrides) {
	overrides = &emptyOverride;
	overrides->intf.device[0] = '\0';
    }

    while (1) {
	FD_ZERO(&fds);
	FD_SET(sock, &fds);

	tv.tv_sec = tv.tv_usec = 0;
	closest = -1;
	if (numInterfaces) {
	    for (i = 0; i < numInterfaces; i++)
		if ((intf[i].set & INTFINFO_HAS_LEASE) && 
			(closest == -1 || 
			       (intf[closest].renewAt > intf[i].renewAt)))
		    closest = i;
	    if (closest != -1) {
		tv.tv_sec = intf[closest].renewAt - time(NULL);
		if (tv.tv_sec <= 0) {
		    dhcpRenew(intf + closest);
		    continue;
		}
	    }
	}

	if (select(sock + 1, &fds, NULL, NULL, 
		   closest != -1 ? &tv : NULL) > 0) {
	    conn = accept(sock, &addr, &addrLength);

	    if (read(conn, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		close(conn);
		continue;
	    }

	    switch (cmd.type) {
	      case CMD_DIE:
		for (i = 0; i < numInterfaces; i++)
		    dhcpRelease(intf + i);

		syslog(LOG_INFO, "terminating at root's request");

		cmd.type = CMD_RESULT;
		cmd.u.result = 0;
		write(conn, &cmd, sizeof(cmd));
		exit(0);

	      case CMD_STARTIFACE:
		o = overrides; 
		while (*o->intf.device && 
			strcmp(o->intf.device, cmd.u.start.device)) {
		    o++;
		}
		if (!*o->intf.device) o = overrides;

		if (doDhcp(cmd.u.start.device,
			   cmd.u.start.flags, cmd.u.start.reqLease, 
			   cmd.u.start.reqHostname[0] ? 
			       cmd.u.start.reqHostname : NULL,
			   intf + numInterfaces, o)) {
		    cmd.u.result = 1;
		} else {
		    cmd.u.result = 0;
		    numInterfaces++;
		}
		break;

	      case CMD_FORCERENEW:
		for (i = 0; i < numInterfaces; i++)
		    if (!strcmp(intf[i].device, cmd.u.renew.device)) break;
		if (i == numInterfaces)
		    cmd.u.result = RESULT_UNKNOWNIFACE;
		else
		    cmd.u.result = dhcpRenew(intf + i);
		break;

	      case CMD_STOPIFACE:
		for (i = 0; i < numInterfaces; i++)
		    if (!strcmp(intf[i].device, cmd.u.stop.device)) break;
		if (i == numInterfaces)
		    cmd.u.result = RESULT_UNKNOWNIFACE;
		else {
		    cmd.u.result = dhcpRelease(intf + i);
		    if (numInterfaces == 1) {
			cmd.type = CMD_RESULT;
			write(conn, &cmd, sizeof(cmd));

			syslog(LOG_INFO, "terminating as there are no "
				"more devices under management");

			exit(0);
		    }

		    intf[i] = intf[numInterfaces - 1];
		    numInterfaces--;
		}
		break;

	      case CMD_REQSTATUS:
		for (i = 0; i < numInterfaces; i++)
		    if (!strcmp(intf[i].device, cmd.u.stop.device)) break;
		if (i == numInterfaces) {
		    cmd.u.result = RESULT_UNKNOWNIFACE;
		} else {
		    cmd.type = CMD_STATUS;
		    cmd.u.status.intf = intf[i];
		    if (intf[i].set & NETINFO_HAS_HOSTNAME)
			strncpy(cmd.u.status.hostname,
			    intf->hostname, sizeof(cmd.u.status.hostname));
		    cmd.u.status.hostname[sizeof(cmd.u.status.hostname)] = '\0';

		    if (intf[i].set & NETINFO_HAS_DOMAIN)
			strncpy(cmd.u.status.domain,
			    intf->domain, sizeof(cmd.u.status.domain));
		    cmd.u.status.domain[sizeof(cmd.u.status.domain) - 1] = '\0';

		    if (intf[i].set & INTFINFO_HAS_BOOTFILE)
			strncpy(cmd.u.status.bootFile,
			    intf->bootFile, sizeof(cmd.u.status.bootFile));
		    cmd.u.status.bootFile[sizeof(cmd.u.status.bootFile) - 1] = 
		    							'\0';
		}

	      case CMD_STATUS:
	      case CMD_RESULT:
		/* can't happen */
		break;
	    }

	    if (cmd.type != CMD_STATUS) cmd.type = CMD_RESULT;
	    write(conn, &cmd, sizeof(cmd));

	    close(conn);
	}
    }

    exit(0);
}

static int openControlSocket(char * configFile) {
    struct sockaddr_un addr;
    int sock;
    size_t addrLength;
    pid_t child;
    int status;

    if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	return -1;

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROLSOCKET);
    addrLength = sizeof(addr.sun_family) + strlen(addr.sun_path);

    if (!connect(sock, (struct sockaddr *) &addr, addrLength)) 
	return sock;

    if (errno != ENOENT && errno != ECONNREFUSED) {
	fprintf(stderr, "failed to connect to %s: %s\n", CONTROLSOCKET,
		strerror(errno));
	close(sock);
	return -1;
    }

    if (!(child = fork())) {
	close(sock);

	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
	    fprintf(stderr, "failed to create socket: %s\n", strerror(errno));
	    exit(1);
	}

	unlink(CONTROLSOCKET);
	umask(077);
	if (bind(sock, (struct sockaddr *) &addr, addrLength)) {
	    fprintf(stderr, "bind to %s failed: %s\n", CONTROLSOCKET,
		    strerror(errno));
	    exit(1);
	}
	umask(033);

	listen(sock, 5);

	if (fork()) _exit(0);

	close(0);
	close(1);
	close(2);

	openlog("pumpd", LOG_PID, LOG_DAEMON);
	{
	    time_t t;

	    t = time(NULL);
	    syslog(LOG_INFO, "starting at %s\n", ctime(&t));
	}

	runDaemon(sock, configFile);
    }

    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status))
	return -1;

    if (!connect(sock, (struct sockaddr *) &addr, addrLength)) 
	return sock;

    fprintf(stderr, "failed to connect to %s: %s\n", CONTROLSOCKET,
	    strerror(errno));

    return 0;
}

void printStatus(struct intfInfo i, char * hostname, char * domain,
		 char * bootFile) {
    int j;

    printf("Device %s\n", i.device);
    printf("\tIP: %s\n", inet_ntoa(i.ip));
    printf("\tNetmask: %s\n", inet_ntoa(i.netmask));
    printf("\tBroadcast: %s\n", inet_ntoa(i.broadcast));
    printf("\tNetwork: %s\n", inet_ntoa(i.network));
    printf("\tBoot server %s\n", inet_ntoa(i.bootServer));

    if (i.set & NETINFO_HAS_GATEWAY)
	printf("\tGateway: %s\n", inet_ntoa(i.gateway));

    if (i.set & INTFINFO_HAS_BOOTFILE)
	printf("\tBoot file: %s\n", bootFile);

    if (i.set & NETINFO_HAS_HOSTNAME)
	printf("\tHostname: %s\n", hostname);

    if (i.set & NETINFO_HAS_DOMAIN)
	printf("\tDomain: %s\n", domain);

    if (i.numDns) {
	printf("\tNameservers:");
	for (j = 0; j < i.numDns; j++)
	    printf(" %s", inet_ntoa(i.dnsServers[j]));
	printf("\n");
    }

    if (i.set & INTFINFO_HAS_LEASE) {
	printf("\tRenewal time: %s", ctime(&i.renewAt)); 
	printf("\tExpiration time: %s", ctime(&i.leaseExpiration)); 
    }
}

int main (int argc, char ** argv) {
    char * device = "eth0";
    char * hostname = "";
    poptContext optCon;
    int rc;
    int test = 0;
    int flags = 0;
    int lease = 6;
    int killDaemon = 0;
    int release = 0, renew = 0, status = 0, lookupHostname = 0;
    struct command cmd, response;
    char * configFile = "/etc/pump.conf";
    struct overrideInfo * overrides;
    int cont;
    struct poptOption options[] = {
	    { "config-file", 'c', POPT_ARG_STRING, &configFile, 0,
			N_("Configuration file to use instead of "
			   "/etc/pump.conf") },
            { "hostname", 'h', POPT_ARG_STRING, &hostname, 0, 
			N_("Hostname to request"), N_("hostname") },
            { "interface", 'i', POPT_ARG_STRING, &device, 0, 
			N_("Interface to configure (normally eth0)"), 
			N_("iface") },
	    { "kill", 'k', POPT_ARG_NONE, &killDaemon, 0,
			N_("Kill daemon (and disable all interfaces)"), NULL },
	    { "lease", 'l', POPT_ARG_INT, &lease, 0,
			N_("Lease time to request (in hours)"), N_("hours") },
	    { "lookup-hostname", '\0', POPT_ARG_NONE, &lookupHostname, 0,
			N_("Force lookup of hostname") },
	    { "release", 'r', POPT_ARG_NONE, &release, 0,
			N_("Release interface"), NULL },
	    { "renew", 'R', POPT_ARG_NONE, &renew, 0,
			N_("Force immediate lease renewal"), NULL },
	    { "status", 's', POPT_ARG_NONE, &status, 0,
			N_("Display interface status"), NULL },
	    /*{ "test", 't', POPT_ARG_NONE, &test, 0,
			N_("Don't change the interface configuration or "
			   "run as a deamon.") },*/
	    POPT_AUTOHELP
	    { NULL, '\0', 0, NULL, 0 }
        };

    optCon = poptGetContext(PROGNAME, argc, argv, options,0);
    poptReadDefaultConfig(optCon, 1);

    if ((rc = poptGetNextOpt(optCon)) < -1) {
	fprintf(stderr, _("%s: bad argument %s: %s\n"), PROGNAME,
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS), 
		poptStrerror(rc));
	return 1;
    }

    if (poptGetArg(optCon)) {
	fprintf(stderr, _("%s: no extra parameters are expected\n"), PROGNAME);
	return 1;
    }

    /* make sure the config file is parseable before going on any further */
    if (readPumpConfig(configFile, &overrides)) return 1;

    if (geteuid()) {
	fprintf(stderr, _("%s: must be run as root\n"), PROGNAME);
	exit(1);
    }

    if (test)
	flags = DHCP_FLAG_NODAEMON | DHCP_FLAG_NOCONFIG;
    if (lookupHostname)
	flags |= DHCP_FLAG_FORCEHNLOOKUP;

    cont = openControlSocket(configFile);
    if (cont < 0) 
	exit(1);

    if (killDaemon) {
	cmd.type = CMD_DIE;
    } else if (status) {
	cmd.type = CMD_REQSTATUS;
	strcpy(cmd.u.reqstatus.device, device);
    } else if (renew) {
	cmd.type = CMD_FORCERENEW;
	strcpy(cmd.u.renew.device, device);
    } else if (release) {
	cmd.type = CMD_STOPIFACE;
	strcpy(cmd.u.stop.device, device);
    } else {
	cmd.type = CMD_STARTIFACE;
	strcpy(cmd.u.start.device, device);
	cmd.u.start.flags = flags;
	cmd.u.start.reqLease = lease * 60 * 60;
	strcpy(cmd.u.start.reqHostname, hostname);
    }

    write(cont, &cmd, sizeof(cmd));
    read(cont, &response, sizeof(response));

    if (response.type == CMD_RESULT && response.u.result &&
	    cmd.type == CMD_STARTIFACE) {
	cont = openControlSocket(configFile);
	if (cont < 0) 
	    exit(1);
	write(cont, &cmd, sizeof(cmd));
	read(cont, &response, sizeof(response));
    }

    if (response.type == CMD_RESULT) {
	if (response.u.result) {
	    fprintf(stderr, "Operation failed.\n");
	    return 1;
	}
    } else if (response.type == CMD_STATUS) {
	printStatus(response.u.status.intf, response.u.status.hostname, 
		    response.u.status.domain, response.u.status.bootFile);
    }

    return 0;
}
