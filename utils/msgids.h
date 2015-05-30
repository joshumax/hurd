/* Translate message ids to symbolic names.

   Copyright (C) 1998-2015 Free Software Foundation, Inc.

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

#ifndef _HURD_MSGIDS_H_
#define _HURD_MSGIDS_H_

/* We map message ids to procedure and subsystem names.  */
struct msgid_info
{
  char *name;
  char *subsystem;
};

const struct msgid_info *msgid_info (mach_msg_id_t msgid);
const struct argp msgid_argp;

#endif	/* _HURD_MSGIDS_H_ */
