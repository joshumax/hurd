/* Recording errors for pager library
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


/* Some error has happened indicating that the page cannot be written. 
   (Usually this is ENOSPC or EDQOUT.)  On the next pagein which
   requests write access, return the error to the kernel.  (This is 
   screwy because of the rules associated with m_o_lock_request.) */
void
mark_next_request_error(struct pager *p,
			int offset,
			int length,
			error_t error)
{
  int page_error;
  char *p;

  offset /= __vm_page_size;
  length /= __vm_page_size;
  
  switch (error)
    {
    case 0:
      page_error = PAGE_NOERR;
      break;
    case ENOSPC:
      page_error = PAGE_ENOSPC;
      break;
    case EIO:
      page_error = PAGE_EIO;
      break;
    case EDQUOT:
      page_error = PAGE_EDQUOT;
      break;
    default:
      panic ("mark_object_error");
      break;
    }
  
  for (p = p->pagemap; p < p->pagemap + length; p++)
    *p = SET_PM_NEXTERROR (*p, page_error);
}

/* We are returning a pager error to the kernel.  Write down
   in the pager what that error was so that the exception handling
   routines can find out.  (This is only necessary because the
   XP interface is not completely implemented in the kernel.) */
void
mark_object_error(struct pager *p,
		  int offset,
		  int length,
		  error_t error)
{
  int page_error = 0;
  char *p;

  offset /= __vm_page_size;
  length /= __vm_page_size;
  
  switch (error)
    {
    case 0:
      page_error = PAGE_NOERR;
      break;
    case ENOSPC:
      page_error = PAGE_ENOSPC;
      break;
    case EIO:
      page_error = PAGE_EIO;
      break;
    case EDQUOT:
      page_error = PAGE_EDQUOT;
      break;
    default:
      panic ("mark_object_error");
      break;
    }
  
  for (p = p->pagemap; p < p->pagemap + length; p++)
    *p = SET_PM_ERROR (*p, page_error);
}

