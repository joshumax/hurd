/* FS helper library definitions
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

/* This library implements various things that are generic to
   all or most implementors of the filesystem protocol.  It 
   presumes that you are using the iohelp library as well.  It
   is divided into separate facilities which may be used independently.  */


/* Translator linkage.  These routines only work for multi-threaded
   servers, and assume you are using the ports library.  */

/* Define one of these structures as part of every disk node.  */
struct trans_link
{
  /* control port for the child filesystem */
  fsys_t control;

  /* this is woken up when fsys_startup is receieved 
     from the child filesystem. */
  struct condition initwait;

  /* This indicates that someone has already started up the translator */
  int starting;
};

/* The user must define this variable.  This is the libports type for
   bootstrap ports given to newly started translators. */
extern int fshelp_transboot_port_type;

/* Call this before calling any of the other translator linkage routines,
   normally from your main node initialization routine. */
void fshelp_init_trans_link (struct trans_link *LINK);

/* Call this when the CONTROL field of a translator is null and 
   you want to have the translator started so you can talk to it.
   LINK is the trans_link structure for this node; NAME is the file
   to execute as the translator.  
*/
void fshelp_start_translator
