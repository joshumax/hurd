#ifndef _HACK_KERNEL_H
#define _HACK_KERNEL_H

#include <stdio.h>

#define printk printf

int getname (const char *, char **);
void putname (char *);

int suser (void);


#endif
