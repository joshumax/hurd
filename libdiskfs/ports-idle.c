/* 
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* Called by the ports library when we have been idle for
   ten minutes. */
void
ports_notice_idle (int nhard, int nsoft)
{
  spin_lock (&_diskfs_control_lock);
  if (nhard > _diskfs_ncontrol_ports)
    {
      spin_unlock (&diskfs_control_lock);
      return;
    }
  spin_unlock (&_diskfs_control_lock);

  /* XXX
     Here should actually drop control ports and exit. */
  return;
}
