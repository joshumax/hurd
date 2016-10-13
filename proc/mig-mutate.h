/*
   Copyright (C) 2014 Free Software Foundation, Inc.
   Written by Justus Winter.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */


#define PROCESS_INTRAN						\
  pstruct_t begin_using_proc_port (process_t)
#define PROCESS_INTRAN_PAYLOAD					\
  pstruct_t begin_using_proc_payload
#define PROCESS_DESTRUCTOR					\
  end_using_proc (pstruct_t)
#define PROCESS_IMPORTS						\
  import "mig-decls.h";

#define NOTIFY_INTRAN						\
  port_info_t begin_using_port_info_port (mach_port_t)
#define NOTIFY_INTRAN_PAYLOAD					\
  port_info_t begin_using_port_info_payload
#define NOTIFY_DESTRUCTOR					\
  end_using_port_info (port_info_t)
#define NOTIFY_IMPORTS						\
  import "libports/mig-decls.h";

#define TASK_NOTIFY_INTRAN					\
  port_info_t begin_using_port_info_port (mach_port_t)
#define TASK_NOTIFY_INTRAN_PAYLOAD				\
  port_info_t begin_using_port_info_payload
#define TASK_NOTIFY_DESTRUCTOR					\
  end_using_port_info (port_info_t)
#define TASK_NOTIFY_IMPORTS					\
  import "libports/mig-decls.h";
