#ifndef _HACK_WAIT_H_
#define _HACK_WAIT_H_

#include <cthreads.h>

struct wait_queue
{
  struct condition c;
};

typedef struct select_table_struct
{
} select_table;

#endif
