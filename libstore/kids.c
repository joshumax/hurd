/* Managing sub-stores

   Copyright (C) 1995,96,97,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "store.h"

/* Set STORE's current children list to (a copy of) CHILDREN and NUM_CHILDREN.  */
error_t
store_set_children (struct store *store,
		    struct store *const *children, size_t num_children)
{
  unsigned size = num_children * sizeof (struct store *);
  struct store **copy = malloc (size);

  if (!copy)
    return ENOMEM;

  if (store->children)
    free (store->children);

  memcpy (copy, children, size);
  store->children = copy;
  store->num_children = num_children;

  return 0;
}

/* Calls the allocate_encoding method in each child store of STORE,
   propagating any errors.  If any child does not hae such a method,
   EOPNOTSUPP is returned.  */
error_t
store_allocate_child_encodings (const struct store *store,
				struct store_enc *enc)
{
  int i;
  error_t err = 0;
  for (i = 0; i < store->num_children && !err; i++)
    {
      struct store *k = store->children[i];
      if (k->class->allocate_encoding)
	(*k->class->allocate_encoding) (k, enc);
      else
	err = EOPNOTSUPP;
    }
  return err;
}

/* Calls the encode method in each child store of STORE, propagating any
   errors.  If any child does not hae such a method, EOPNOTSUPP is returned. */
error_t
store_encode_children (const struct store *store, struct store_enc *enc)
{
  int i;
  error_t err = 0;
  for (i = 0; i < store->num_children && !err; i++)
    {
      struct store *k = store->children[i];
      if (k->class->encode)
	(*k->class->encode) (k, enc);
      else
	err = EOPNOTSUPP;
    }
  return err;
}

/* Decodes NUM_CHILDREN from ENC, storing the results into successive
   positions in CHILDREN.  */
error_t
store_decode_children (struct store_enc *enc, int num_children,
		       const struct store_class *const *classes,
		       struct store **children)
{
  int i;
  error_t err = 0;
  for (i = 0; i < num_children && !err; i++)
    err = store_decode (enc, classes, &children[i]);
  if (err)
    /* Deallocate anything we've already created.  */
    while (--i >= 0)
      store_free (children[i]);
  return err;
}

/* Set FLAGS in all children of STORE, and if successful, add FLAGS to
   STORE's flags.  */
error_t
store_set_child_flags (struct store *store, int flags)
{
  int i;
  error_t err = 0;
  int old_child_flags[store->num_children];

  for (i = 0; i < store->num_children && !err; i++)
    {
      old_child_flags[i] = store->children[i]->flags;
      err = store_set_flags (store->children[i], flags);
    }

  if (err)
    while (i-- > 0)
      store_clear_flags (store->children[i], flags & ~old_child_flags[i]);
  else
    store->flags |= flags;

  return err;
}

/* Clear FLAGS in all children of STORE, and if successful, remove FLAGS from
   STORE's flags.  */
error_t
store_clear_child_flags (struct store *store, int flags)
{
  int i;
  error_t err = 0;
  int old_child_flags[store->num_children];

  for (i = 0; i < store->num_children && !err; i++)
    {
      old_child_flags[i] = store->children[i]->flags;
      err = store_clear_flags (store->children[i], flags);
    }

  if (err)
    while (i-- > 0)
      store_set_flags (store->children[i], flags & ~old_child_flags[i]);
  else
    store->flags &= ~flags;

  return err;
}

/* Parse multiple store names in NAME, and open each individually, returning
   all in the vector STORES, and the number in NUM_STORES.  The syntax of
   NAME is a single non-alpha-numeric separator character, followed by each
   child store name separated by the same separator; each child name is
   TYPE:NAME notation as parsed by store_typed_open.  If every child uses the
   same TYPE: prefix, then it may be factored out and put before the child
   list instead (the two types of notation are differentiated by whether the
   first character of name is alpha-numeric or not).  */
error_t
store_open_children (const char *name, int flags,
		     const struct store_class *const *classes,
		     struct store ***stores, size_t *num_stores)
{
  char *pfx = 0;		/* Prefix applied to each part name.  */
  size_t pfx_len = 0;		/* Space PFX + separator takes up.  */
  char sep = *name;		/* Character separating individual names.  */

  if (sep && isalnum (sep))
    /* If the first character is a `name' character, it's likely to be either
       a type prefix (e.g, TYPE:@NAME1@NAME2@), so we distribute the type
       prefix among the elements (@TYPE:NAME1@TYPE:NAME2@).  */
    {
      const char *pfx_end = name;

      while (isalnum (*pfx_end))
	pfx_end++;

      if (*pfx_end++ != ':')
	return EINVAL;

      /* Make a copy of the prefix.  */
      pfx = strndupa (name, pfx_end - name);
      pfx_len = pfx_end - name;

      sep = *pfx_end;
    }

  if (sep)
    /* Parse a list of store specs separated by SEP.  */
    {
      int k;
      const char *p, *end;
      error_t err = 0;
      size_t count = 0;

      /* First, see how many there are.  */
      for (p = name; p && p[1]; p = strchr (p + 1, sep))
	count++;

      /* Make a vector to hold them.  */
      *stores = malloc (count * sizeof (struct store *));
      *num_stores = count;
      if (! *stores)
	return ENOMEM;

      memset (*stores, 0, count * sizeof(struct store *));

      /* Open each child store.  */
      for (p = name, k = 0; !err && p && p[1]; p = end, k++)
	{
	  size_t kname_len;

	  end = strchr (p + 1, sep);
	  kname_len = (end ? end - p - 1 : strlen (p + 1));

	  {
	    /* Allocate temporary child name on the stack.  */
	    char kname[pfx_len + kname_len + 1];

	    if (pfx)
	      /* Add type prefix to child name.  */
	      memcpy (kname, pfx, pfx_len);

	    memcpy (kname + pfx_len, p + 1, kname_len);
	    kname[pfx_len + kname_len] = '\0';

	    err = store_typed_open (kname, flags, classes, &(*stores)[k]);
	  }
	}

      if (err)
	/* Failure opening some child, deallocate what we've done so far.  */
	{
	  while (--k >= 0)
	    store_free ((*stores)[k]);
	  free (*stores);
	}

      return err;
    }
  else
    /* Empty list.  */
    {
      *stores = 0;
      *num_stores = 0;
      return 0;
    }
}

/* Try to come up with a name for the children in STORE, combining the names
   of each child in a way that could be used to parse them with
   store_open_children.  This is done heuristically, and so may not succeed.
   If a child doesn't have a  name, EINVAL is returned.  */
error_t
store_children_name (const struct store *store, char **name)
{
  static char try_seps[] = "@+=,._%|;^!~'&";
  struct store **kids = store->children;
  size_t num_kids = store->num_children;

  if (num_kids == 0)
    {
      *name = strdup ("");
      return *name ? 0 : ENOMEM;
    }
  else
    {
      int k;
      char *s;			/* Current separator in search for one.  */
      int fail;			/* If we couldn't use *S as as sep. */
      size_t total_len = 0;	/* Length of name we will return.  */

      /* Detect children without names, and calculate the total length of the
	 name we will return (which is the sum of the lengths of the child
	 names plus room for the types and separator characters.  */
      for (k = 0; k < num_kids; k++)
	if (!kids[k] || !kids[k]->name)
	  return EINVAL;
	else
	  total_len +=
	    /* separator + type name + type separator + child name */
	    1 + strlen (kids[k]->class->name) + 1 + strlen (kids[k]->name);

      /* Look for a separator character from those in TRY_SEPS that doesn't
	 occur in any of the the child names.  */
      for (s = try_seps, fail = 1; *s && fail; s++)
	for (k = 0, fail = 0; k < num_kids && !fail; k++)
	  if (strchr (kids[k]->name, *s))
	    fail = 1;

      if (*s)
	/* We found a usable separator!  */
	{
	  char *p = malloc (total_len + 1);

	  if (! p)
	    return ENOMEM;
	  *name = p;

	  for (k = 0; k < num_kids; k++)
	    p +=
	      sprintf (p, "%c%s:%s", *s, kids[k]->class->name, kids[k]->name);

	  return 0;
	}
      else
	return EGRATUITOUS;
    }
}
