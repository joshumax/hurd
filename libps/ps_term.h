#ifndef	_ps_term_user_
#define	_ps_term_user_

/* Module term */

#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/message.h>

#include <mach/std_types.h>
#include <mach/mach_types.h>
#include <device/device_types.h>
#include <device/net_status.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <hurd/hurd_types.h>

/* Routine term_getctty */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_getctty
#if	defined(LINTLIBRARY)
    (terminal, ctty)
	io_t terminal;
	mach_port_t *ctty;
{ return ps_term_getctty(terminal, ctty); }
#else
(
	io_t terminal,
	mach_port_t *ctty
);
#endif

/* Routine term_open_ctty */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_open_ctty
#if	defined(LINTLIBRARY)
    (terminal, pid, pgrp, newtty)
	io_t terminal;
	pid_t pid;
	pid_t pgrp;
	mach_port_t *newtty;
{ return ps_term_open_ctty(terminal, pid, pgrp, newtty); }
#else
(
	io_t terminal,
	pid_t pid,
	pid_t pgrp,
	mach_port_t *newtty
);
#endif

/* Routine term_set_nodename */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_set_nodename
#if	defined(LINTLIBRARY)
    (terminal, name)
	io_t terminal;
	string_t name;
{ return ps_term_set_nodename(terminal, name); }
#else
(
	io_t terminal,
	string_t name
);
#endif

/* Routine term_get_nodename */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_get_nodename
#if	defined(LINTLIBRARY)
    (terminal, name)
	io_t terminal;
	string_t name;
{ return ps_term_get_nodename(terminal, name); }
#else
(
	io_t terminal,
	string_t name
);
#endif

/* Routine term_set_filenode */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_set_filenode
#if	defined(LINTLIBRARY)
    (terminal, filenode)
	io_t terminal;
	file_t filenode;
{ return ps_term_set_filenode(terminal, filenode); }
#else
(
	io_t terminal,
	file_t filenode
);
#endif

/* Routine term_get_bottom_type */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_get_bottom_type
#if	defined(LINTLIBRARY)
    (terminal, ttype)
	io_t terminal;
	int *ttype;
{ return ps_term_get_bottom_type(terminal, ttype); }
#else
(
	io_t terminal,
	int *ttype
);
#endif

/* Routine term_on_machdev */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_on_machdev
#if	defined(LINTLIBRARY)
    (terminal, machdev)
	io_t terminal;
	mach_port_t machdev;
{ return ps_term_on_machdev(terminal, machdev); }
#else
(
	io_t terminal,
	mach_port_t machdev
);
#endif

/* Routine term_on_hurddev */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_on_hurddev
#if	defined(LINTLIBRARY)
    (terminal, hurddev)
	io_t terminal;
	io_t hurddev;
{ return ps_term_on_hurddev(terminal, hurddev); }
#else
(
	io_t terminal,
	io_t hurddev
);
#endif

/* Routine term_on_pty */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_term_on_pty
#if	defined(LINTLIBRARY)
    (terminal, ptymaster)
	io_t terminal;
	io_t *ptymaster;
{ return ps_term_on_pty(terminal, ptymaster); }
#else
(
	io_t terminal,
	io_t *ptymaster
);
#endif

/* Routine termctty_open_terminal */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_termctty_open_terminal
#if	defined(LINTLIBRARY)
    (ctty, flags, terminal)
	mach_port_t ctty;
	int flags;
	mach_port_t *terminal;
{ return ps_termctty_open_terminal(ctty, flags, terminal); }
#else
(
	mach_port_t ctty,
	int flags,
	mach_port_t *terminal
);
#endif

#endif	/* not defined(_ps_term_user_) */
