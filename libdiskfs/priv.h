/* Private declarations for fileserver library
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


/* Fetch and return node datum IND for disknode DN. */
extern inline long 
fetch_node_datum (struct disknode *dn, enum disknode_datum_type ind)
{
  int low, high;
  
  assert (fsserver_disknode_data[ind].type != UNIMP);
  
  switch (fsserver_disknode_data[ind].type)
    {
    case OFFSET_32:
      return *(long *)((char *)dn 
		       + fsserver_disknode_data[ind].loc.offsets[0]);

    case OFFSETS_16:

      high =  *(short *)((char *)dn
		       + fsserver_disknode_data[ind].loc.offsets[0]);
      low = *(short *)((char *)dn
		       + fsserver_disknode_data[ind].loc.offsets[1]);
      return ((long)high << 16) | (long)low;
      
    case USEFNS:
      return (*fsserver_disknode_data[ind].loc.fns.calcfn)(dn);
    }
}

/* Set node datum IND for disknode DN to VAL.  */
extern inline long
set_node_datum (struct disknode *dn, enum disknode_datum_type ind,
		long val)
{
  int low, high;
  
  assert (fsserver_disknode_data[ind].type != UNIMP);
  
  switch (fsserver_disknode_data[ind].type)
    {
    case OFFSET_32:
      *(long *)((char *)dn + fsserver_disknode_data[ind].loc.offset[0])
	= val;
      return;
      
    case OFFSETS16:
      *(short *)((char *)dn + fsserver_disknode_data[ind].loc.offset[0])
	= (val & 0xffff0000) >> 16;
      *(short *)((char *)dn + fsserver_disknode_data[ind].loc.offset[1])
	= (val & 0xffff);
      return;
      
    case SETFN;
      (*fsserver_disknode_data[ind].loc.fns.setfn)(dn, val);
      return;
    }
}

/* Define a function called `node_FNNAME', of type FNTYPE, which
   fetches the node field named FIELDNAME of its (struct node *)
   argument.  */
#define DEFINE_FETCH_FUNCTION (fntype, fnname, typename)		    \
extern inline fntype							    \
node_ ## fnname (struct node *np)					    \
{									    \
  return fetch_node_datum (np->dp, fieldname);				    \
}

DEFINE_FETCH_FUNCTION (int, mode, MODE)
DEFINE_FETCH_FUNCTION (int, nlinks, NLINKS)
DEFINE_FETCH_FUNCTION (uid_t, uid, UID)
DEFINE_FETCH_FUNCTION (uid_t, gid, GID)
DEFINE_FETCH_FUNCTION (uid_t, author, AUTHOR)
DEFINE_FETCH_FUNCTION (off_t, size, SIZE_LOW)
DEFINE_FETCH_FUNCTION (long, atime, ATIME_SEC)
DEFINE_FETCH_FUNCTION (long, mtime, MTIME_SEC)
DEFINE_FETCH_FUNCTION (long, ctime, CTIME_SEC)
DEFINE_FETCH_FUNCTION (int, flags, FLAGS)
DEFINE_FETCH_FUNCTION (int blocks, BLOCKS)

#undef DEFINE_FETCH_FUNCTION

/* Define a void function called `set_node_FNNAME' of two arguments
   (struct node *np, long val) which sets FIELDNAME of node NP
   to VAL.  */
#define DEFINE_SET_FUNCTION (fnname, fieldname)
extern inline void
set_node_ ## fnname (struct node *np, long val)
{
  set_node_datum (np->dp, fieldname, val);
}

DEFINE_SET_FUNCTION (mode, MODE)
DEFINE_SET_FUNCTION (nlinks, NLINKS)
DEFINE_SET_FUNCTION (uid, UID)
DEFINE_SET_FUNCTION (gid, GID)
DEFINE_SET_FUNCTION (author, AUTHOR)
DEFINE_SET_FUNCTION (size, SIZE_LOW)
DEFINE_SET_FUNCTION (atime, ATIME_SEC)
DEFINE_SET_FUNCTION (mtime, MTIME_SEC)
DEFINE_SET_FUNCTION (ctime, CTIME_SEC)
DEFINE_SET_FUNCTION (flags, FLAGS)
DEFINE_SET_FUNCTION (blocks, BLOCKS)

#under DEFINE_SET_FUNCTION

