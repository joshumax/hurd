#if !_LIBC
/* This code is written for inclusion in gnu-libc, and uses names in the
   namespace reserved for libc.  If we're not compiling in libc, define those
   names to be the normal ones instead.  */

/* argp-parse functions */
#undef __argp_parse
#define __argp_parse argp_parse
#undef __option_is_end
#define __option_is_end _option_is_end
#undef __option_is_short
#define __option_is_short _option_is_short

/* argp-help functions */
#undef __argp_help
#define __argp_help argp_help
#undef __argp_error
#define __argp_error argp_error
#undef __argp_failure
#define __argp_failure argp_failure
#undef __argp_state_help
#define __argp_state_help argp_state_help
#undef __argp_usage
#define __argp_usage argp_usage

/* argp-fmtstream functions */
#undef __argp_make_fmtstream
#define __argp_make_fmtstream argp_make_fmtstream
#undef __argp_fmtstream_free
#define __argp_fmtstream_free argp_fmtstream_free
#undef __argp_fmtstream_putc
#define __argp_fmtstream_putc argp_fmtstream_putc 
#undef __argp_fmtstream_puts
#define __argp_fmtstream_puts argp_fmtstream_puts
#undef __argp_fmtstream_write
#define __argp_fmtstream_write argp_fmtstream_write
#undef __argp_fmtstream_printf
#define __argp_fmtstream_printf argp_fmtstream_printf
#undef __argp_fmtstream_set_lmargin
#define __argp_fmtstream_set_lmargin argp_fmtstream_set_lmargin
#undef __argp_fmtstream_set_rmargin
#define __argp_fmtstream_set_rmargin argp_fmtstream_set_rmargin
#undef __argp_fmtstream_set_wmargin
#define __argp_fmtstream_set_wmargin argp_fmtstream_set_wmargin
#undef __argp_fmtstream_point
#define __argp_fmtstream_point argp_fmtstream_point
#undef __argp_fmtstream_update
#define __argp_fmtstream_update _argp_fmtstream_update
#undef __argp_fmtstream_ensure
#define __argp_fmtstream_ensure _argp_fmtstream_ensure
#undef __argp_fmtstream_lmargin
#define __argp_fmtstream_lmargin argp_fmtstream_lmargin
#undef __argp_fmtstream_rmargin
#define __argp_fmtstream_rmargin argp_fmtstream_rmargin
#undef __argp_fmtstream_wmargin
#define __argp_fmtstream_wmargin argp_fmtstream_wmargin

/* normal libc functions we call */
#undef __getopt_long
#define __getopt_long getopt_long
#undef __getopt_long_only
#define __getopt_long_only getopt_long_only
#undef __optarg
#define __optarg optarg
#undef __opterr
#define __opterr opterr
#undef __optind
#define __optind optind
#undef __optopt
#define __optopt optopt
#undef __sleep
#define __sleep sleep
#undef __strcasecmp
#define __strcasecmp strcasecmp
#undef __vsnprintf
#define __vsnprintf vsnprintf

#endif /* !_LIBC */

#ifndef __set_errno
#define __set_errno(e) (errno = (e))
#endif
