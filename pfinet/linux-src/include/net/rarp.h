/* linux/net/inet/rarp.h */
#ifndef _RARP_H
#define _RARP_H

extern int rarp_ioctl(unsigned int cmd, void *arg);
extern int rarp_get_info(char *buffer, 
			 char **start, 
			 off_t offset, 
			 int length,
			 int dummy);
#endif	/* _RARP_H */

