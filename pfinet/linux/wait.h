#ifndef _HACK_WAIT_H_
#define _HACK_WAIT_H_

#include <cthreads.h>

struct wait_queue
{
  struct condition c;
};

struct select_table_elt
{
  struct condition *dependent_condition;
  struct select_table_elt *next;
};

typedef struct select_table_struct
{
  struct condition master_condition;
  struct select_table_elt *head;
} select_table;

#endif
