#ifndef	_ps_msg_user_
#define	_ps_msg_user_

/* Module msg */

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

/* Routine msg_sig_post */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_sig_post
#if	defined(LINTLIBRARY)
    (process, signal, refport)
	mach_port_t process;
	int signal;
	mach_port_t refport;
{ return ps_msg_sig_post(process, signal, refport); }
#else
(
	mach_port_t process,
	int signal,
	mach_port_t refport
);
#endif

/* Routine msg_proc_newids */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_proc_newids
#if	defined(LINTLIBRARY)
    (process, task, ppid, pgrp, orphaned)
	mach_port_t process;
	mach_port_t task;
	pid_t ppid;
	pid_t pgrp;
	int orphaned;
{ return ps_msg_proc_newids(process, task, ppid, pgrp, orphaned); }
#else
(
	mach_port_t process,
	mach_port_t task,
	pid_t ppid,
	pid_t pgrp,
	int orphaned
);
#endif

/* Routine msg_add_auth */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_add_auth
#if	defined(LINTLIBRARY)
    (process, auth)
	mach_port_t process;
	auth_t auth;
{ return ps_msg_add_auth(process, auth); }
#else
(
	mach_port_t process,
	auth_t auth
);
#endif

/* Routine msg_del_auth */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_del_auth
#if	defined(LINTLIBRARY)
    (process, task, uids, uidsCnt, gids, gidsCnt)
	mach_port_t process;
	mach_port_t task;
	intarray_t uids;
	mach_msg_type_number_t uidsCnt;
	intarray_t gids;
	mach_msg_type_number_t gidsCnt;
{ return ps_msg_del_auth(process, task, uids, uidsCnt, gids, gidsCnt); }
#else
(
	mach_port_t process,
	mach_port_t task,
	intarray_t uids,
	mach_msg_type_number_t uidsCnt,
	intarray_t gids,
	mach_msg_type_number_t gidsCnt
);
#endif

/* Routine msg_get_init_port */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_init_port
#if	defined(LINTLIBRARY)
    (process, refport, which, port)
	mach_port_t process;
	mach_port_t refport;
	int which;
	mach_port_t *port;
{ return ps_msg_get_init_port(process, refport, which, port); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int which,
	mach_port_t *port
);
#endif

/* Routine msg_set_init_port */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_init_port
#if	defined(LINTLIBRARY)
    (process, refport, which, port, portPoly)
	mach_port_t process;
	mach_port_t refport;
	int which;
	mach_port_t port;
	mach_msg_type_name_t portPoly;
{ return ps_msg_set_init_port(process, refport, which, port, portPoly); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int which,
	mach_port_t port,
	mach_msg_type_name_t portPoly
);
#endif

/* Routine msg_get_init_ports */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_init_ports
#if	defined(LINTLIBRARY)
    (process, refport, ports, portsCnt)
	mach_port_t process;
	mach_port_t refport;
	portarray_t *ports;
	mach_msg_type_number_t *portsCnt;
{ return ps_msg_get_init_ports(process, refport, ports, portsCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	portarray_t *ports,
	mach_msg_type_number_t *portsCnt
);
#endif

/* Routine msg_set_init_ports */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_init_ports
#if	defined(LINTLIBRARY)
    (process, refport, ports, portsPoly, portsCnt)
	mach_port_t process;
	mach_port_t refport;
	portarray_t ports;
	mach_msg_type_name_t portsPoly;
	mach_msg_type_number_t portsCnt;
{ return ps_msg_set_init_ports(process, refport, ports, portsPoly, portsCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	portarray_t ports,
	mach_msg_type_name_t portsPoly,
	mach_msg_type_number_t portsCnt
);
#endif

/* Routine msg_get_init_int */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_init_int
#if	defined(LINTLIBRARY)
    (process, refport, which, value)
	mach_port_t process;
	mach_port_t refport;
	int which;
	int *value;
{ return ps_msg_get_init_int(process, refport, which, value); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int which,
	int *value
);
#endif

/* Routine msg_set_init_int */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_init_int
#if	defined(LINTLIBRARY)
    (process, refport, which, value)
	mach_port_t process;
	mach_port_t refport;
	int which;
	int value;
{ return ps_msg_set_init_int(process, refport, which, value); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int which,
	int value
);
#endif

/* Routine msg_get_init_ints */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_init_ints
#if	defined(LINTLIBRARY)
    (process, refport, values, valuesCnt)
	mach_port_t process;
	mach_port_t refport;
	intarray_t *values;
	mach_msg_type_number_t *valuesCnt;
{ return ps_msg_get_init_ints(process, refport, values, valuesCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	intarray_t *values,
	mach_msg_type_number_t *valuesCnt
);
#endif

/* Routine msg_set_init_ints */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_init_ints
#if	defined(LINTLIBRARY)
    (process, refport, values, valuesCnt)
	mach_port_t process;
	mach_port_t refport;
	intarray_t values;
	mach_msg_type_number_t valuesCnt;
{ return ps_msg_set_init_ints(process, refport, values, valuesCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	intarray_t values,
	mach_msg_type_number_t valuesCnt
);
#endif

/* Routine msg_get_dtable */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_dtable
#if	defined(LINTLIBRARY)
    (process, refport, dtable, dtableCnt)
	mach_port_t process;
	mach_port_t refport;
	portarray_t *dtable;
	mach_msg_type_number_t *dtableCnt;
{ return ps_msg_get_dtable(process, refport, dtable, dtableCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	portarray_t *dtable,
	mach_msg_type_number_t *dtableCnt
);
#endif

/* Routine msg_set_dtable */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_dtable
#if	defined(LINTLIBRARY)
    (process, refport, dtable, dtablePoly, dtableCnt)
	mach_port_t process;
	mach_port_t refport;
	portarray_t dtable;
	mach_msg_type_name_t dtablePoly;
	mach_msg_type_number_t dtableCnt;
{ return ps_msg_set_dtable(process, refport, dtable, dtablePoly, dtableCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	portarray_t dtable,
	mach_msg_type_name_t dtablePoly,
	mach_msg_type_number_t dtableCnt
);
#endif

/* Routine msg_get_fd */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_fd
#if	defined(LINTLIBRARY)
    (process, refport, fd, port)
	mach_port_t process;
	mach_port_t refport;
	int fd;
	mach_port_t *port;
{ return ps_msg_get_fd(process, refport, fd, port); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int fd,
	mach_port_t *port
);
#endif

/* Routine msg_set_fd */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_fd
#if	defined(LINTLIBRARY)
    (process, refport, fd, port, portPoly)
	mach_port_t process;
	mach_port_t refport;
	int fd;
	mach_port_t port;
	mach_msg_type_name_t portPoly;
{ return ps_msg_set_fd(process, refport, fd, port, portPoly); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int fd,
	mach_port_t port,
	mach_msg_type_name_t portPoly
);
#endif

/* Routine msg_get_environment */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_environment
#if	defined(LINTLIBRARY)
    (process, value, valueCnt)
	mach_port_t process;
	data_t *value;
	mach_msg_type_number_t *valueCnt;
{ return ps_msg_get_environment(process, value, valueCnt); }
#else
(
	mach_port_t process,
	data_t *value,
	mach_msg_type_number_t *valueCnt
);
#endif

/* Routine msg_set_environment */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_environment
#if	defined(LINTLIBRARY)
    (process, refport, value, valueCnt)
	mach_port_t process;
	mach_port_t refport;
	data_t value;
	mach_msg_type_number_t valueCnt;
{ return ps_msg_set_environment(process, refport, value, valueCnt); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	data_t value,
	mach_msg_type_number_t valueCnt
);
#endif

/* Routine msg_get_env_variable */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_env_variable
#if	defined(LINTLIBRARY)
    (process, variable, value, valueCnt)
	mach_port_t process;
	string_t variable;
	data_t *value;
	mach_msg_type_number_t *valueCnt;
{ return ps_msg_get_env_variable(process, variable, value, valueCnt); }
#else
(
	mach_port_t process,
	string_t variable,
	data_t *value,
	mach_msg_type_number_t *valueCnt
);
#endif

/* Routine msg_set_env_variable */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_env_variable
#if	defined(LINTLIBRARY)
    (process, refport, variable, value, replace)
	mach_port_t process;
	mach_port_t refport;
	string_t variable;
	string_t value;
	boolean_t replace;
{ return ps_msg_set_env_variable(process, refport, variable, value, replace); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	string_t variable,
	string_t value,
	boolean_t replace
);
#endif

/* Routine msg_startup_dosync */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_startup_dosync
#if	defined(LINTLIBRARY)
    (process)
	mach_port_t process;
{ return ps_msg_startup_dosync(process); }
#else
(
	mach_port_t process
);
#endif

/* Routine msg_sig_post_untraced */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_sig_post_untraced
#if	defined(LINTLIBRARY)
    (process, signal, refport)
	mach_port_t process;
	int signal;
	mach_port_t refport;
{ return ps_msg_sig_post_untraced(process, signal, refport); }
#else
(
	mach_port_t process,
	int signal,
	mach_port_t refport
);
#endif

/* Routine msg_get_exec_flags */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_get_exec_flags
#if	defined(LINTLIBRARY)
    (process, refport, flags)
	mach_port_t process;
	mach_port_t refport;
	int *flags;
{ return ps_msg_get_exec_flags(process, refport, flags); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int *flags
);
#endif

/* Routine msg_set_all_exec_flags */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_all_exec_flags
#if	defined(LINTLIBRARY)
    (process, refport, flags)
	mach_port_t process;
	mach_port_t refport;
	int flags;
{ return ps_msg_set_all_exec_flags(process, refport, flags); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int flags
);
#endif

/* Routine msg_set_some_exec_flags */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_set_some_exec_flags
#if	defined(LINTLIBRARY)
    (process, refport, flags)
	mach_port_t process;
	mach_port_t refport;
	int flags;
{ return ps_msg_set_some_exec_flags(process, refport, flags); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int flags
);
#endif

/* Routine msg_clear_some_exec_flags */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_clear_some_exec_flags
#if	defined(LINTLIBRARY)
    (process, refport, flags)
	mach_port_t process;
	mach_port_t refport;
	int flags;
{ return ps_msg_clear_some_exec_flags(process, refport, flags); }
#else
(
	mach_port_t process,
	mach_port_t refport,
	int flags
);
#endif

/* Routine msg_report_wait */
#ifdef	mig_external
mig_external
#else
extern
#endif
kern_return_t ps_msg_report_wait
#if	defined(LINTLIBRARY)
    (process, thread, wait_desc, wait_rpc)
	mach_port_t process;
	mach_port_t thread;
	string_t wait_desc;
	int *wait_rpc;
{ return ps_msg_report_wait(process, thread, wait_desc, wait_rpc); }
#else
(
	mach_port_t process,
	mach_port_t thread,
	string_t wait_desc,
	int *wait_rpc
);
#endif

#endif	/* not defined(_ps_msg_user_) */
