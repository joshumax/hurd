#ifndef _HACK_KERNEL_H
#define _HACK_KERNEL_H

#include <stdio.h>
#include <linux/sched.h>

#define printk printf

int getname (const char *, char **);
void putname (char *);

extern inline int suser ()
{
  return current->isroot;
};


int kill_proc (int, int, int);
int kill_pg (int, int, int);


#endif
