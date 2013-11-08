/*
	kmod header
*/

#include <linux/config.h>

#ifdef CONFIG_KMOD
extern int request_module(const char * name);
#else
#define request_module(x) do {} while(0)
#endif
