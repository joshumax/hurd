
#ifndef get_char
#  define get_char() get_byte()
#endif

#ifndef put_char
#  define put_char(c) put_byte(c)
#endif

#include <stdio.h>
#define fprintf(stream, fmt...) /* ignore useless error msgs */ ((void)0)

void (*unzip_error) (const char *msg);
#define error(msg) (*unzip_error) (msg)
