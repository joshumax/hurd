/*  xkbdata.c -- Manage XKB datastructures.

    Copyright (C) 2003  Marco Gerards
   
    Written by Marco Gerards <marco@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  */

/* Generate a key for the string S.  XXX: There are many more efficient
   algorithms, this one should be replaced by one of those.  */

#include <stdlib.h>
#include <string.h>
#include <hurd/ihash.h>
#include "xkb.h"

static int
name_hash (char *s)
{
  int i = 0;
  while (*s)
      i += *(s++);
  return i;
}


/* A keyname with a keycode and realmodifier bound to it.  */
struct keyname
{
  int keycode;
  int rmods;
};

static struct hurd_ihash kn_mapping;

/* Initialize the keyname hashtable.  */
static void
keyname_init ()
{
  hurd_ihash_init (&kn_mapping, HURD_IHASH_NO_LOCP);
  debug_printf ("Kn_mapping init");
  /* XXX: error.  */
}

static inline int
keyname_hash(char *keyname)
{
  char tmp[4] = { 0 };
  strncpy(tmp, keyname, sizeof tmp);
  return tmp[0] + (tmp[1] << 8) + (tmp[2] << 16) + (tmp[3] << 24);
}

/* Assign the name KEYNAME to the keycode KEYCODE.  */
error_t
keyname_add (char *keyname, int keycode)
{
  struct keyname *kn;
  int kn_int;

  kn = malloc (sizeof (struct keyname));
  if (!kn)
    return ENOMEM;

  /* XXX: 4 characters can be mapped into a int, it is safe to assume
     this will not be changed.  */
  if (strlen (keyname) > 4)
    {
      debug_printf ("The keyname `%s' consist of more than 4 characters;"
		    " 4 characters is the maximum.\n", keyname);
      /* XXX: Abort?  */
      return 0;
    }
  
  kn->keycode = keycode;
  kn->rmods = 0;

  kn_int = keyname_hash(keyname);
  debug_printf ("add key %s(%d) hash: %d\n", keyname, keycode, kn_int);
  hurd_ihash_add (&kn_mapping, kn_int, kn);

  return 0;
}

/* Find the numberic representation of the keycode with the name
   KEYNAME.  */
int
keyname_find (char *keyname)
{
  struct keyname *kn;
  int kn_int;

  /* XXX: 4 characters can be mapped into a int, it is safe to assume
     this will not be changed.  */
  if (strlen (keyname) > 4)
    {
      debug_printf ("The keyname `%s' consist of more than 4 characters;"
		    " 4 characters is the maximum.\n", keyname);
      /* XXX: Abort?  */
      return 0;
    } 
  kn_int = keyname_hash(keyname);

  kn = hurd_ihash_find (&kn_mapping, kn_int);
  if (kn)
    return kn->keycode;
/*   int h = name_hash (keyname); */
/*   struct keyname *kn; */
/*   for (kn = knhash[KNHASH(h)]; kn; kn = kn->hnext) */
/*     { */
/*       if (strcmp (kn->keyname, keyname)) */
/* 	continue; */
      
/*       return kn->keycode; */
/*     } */

  /* XXX: Is 0 an invalid keycode?  */
  return 0;
}


/* Keytypes and keytype maps.  */

/* The dummy gets used when the original may not be overwritten.  */
static struct keytype dummy_keytype;

#define	KTHSZ	16
#if	((KTHSZ&(KTHSZ-1)) == 0)
#define	KTHASH(ktttl)	((ktttl)&(KTHSZ-1))
#else
#define	KTHASH(ktttl)	(((unsigned)(kt))%KTHSZ)
#endif

/* All keytypes.  */
struct keytype *kthash[KTHSZ];

/* Initialize the keytypes hashtable.  */
static void
keytype_init ()
{
  int n;
  for (n = 0; n < KTHSZ; n++)
    kthash[n] = 0;
}

/* Search the keytype with the name NAME.  */
struct keytype *
keytype_find (char *name)
{
  int nhash = name_hash (name);
  struct keytype *kt;

  for (kt = kthash[KTHASH(nhash)]; kt; kt = kt->hnext)
    if (!strcmp (name, kt->name))
      return kt;
  return NULL;
}

/* Remove the keytype KT.  */
void
keytype_delete (struct keytype *kt)
{
  struct typemap *map;


  *kt->prevp = kt->hnext;
  if (kt->hnext)
    kt->hnext->prevp = kt->prevp;
      
  map = kt->maps;
  while (map)
    {
      struct typemap *nextmap = map->next;
      free (map);
      map = nextmap;
    }
  
}

/* Create a new keytype with the name NAME.  */
error_t
keytype_new (char *name, struct keytype **new_kt)
{
  struct keytype *kt;
  struct keytype *ktlist;
  int nhash;
  
  nhash = name_hash (name);
  debug_printf ("New: %s\n", name);

  kt = keytype_find (name);

  if (kt)
    {
      /* If the merge mode is augement don't replace it.  */
      if (merge_mode == augment)
	{
	  *new_kt = &dummy_keytype;
	  return 0;
	}
      else /* This keytype should replace the old one, remove the old one.  */
	keytype_delete (kt);
    }

  ktlist = kthash[KTHASH(nhash)];
  kt = calloc (1, sizeof (struct keytype));
  if (kt == NULL)
    return ENOMEM;

  kt->hnext = ktlist;
  kt->name = strdup (name);
  kt->prevp = &kthash[KTHASH(nhash)];
  kt->maps = NULL;
  if (kthash[KTHASH(nhash)])
    kthash[KTHASH(nhash)]->prevp = &(kt->hnext);
  kthash[KTHASH(nhash)] = kt;

  *new_kt = kt;
  return 0;
}

/* Add a level (LEVEL) to modifiers (MODS) mapping to the current
   keytype.  */
error_t
keytype_mapadd (struct keytype *kt, modmap_t mods, int level)
{
  struct typemap *map;
  modmap_t nulmap = {0, 0};

  map = malloc (sizeof (struct typemap));
  if (!map)
    return ENOMEM;

  map->level = level;
  map->mods = mods;
  map->preserve = nulmap;
  /* By default modifiers shouldn't be preserved.  */
  map->next = kt->maps;
  
  kt->maps = map;

  return 0;
}

/* For the current keytype the modifiers PRESERVE should be preserved
   when the modifiers MODS are pressed.  */
error_t
keytype_preserve_add (struct keytype *kt, modmap_t mods, modmap_t preserve)
{
  error_t err;
  struct typemap *map;

  map = kt->maps;
  while (map)
    {
      if (mods.rmods == map->mods.rmods && mods.vmods == map->mods.vmods)
	{
	  map->preserve = preserve;
	  return 0;
	}
      map = map->next;
    }

  /* No map has been found, add the default map.  */
  err = keytype_mapadd (kt, mods, 0);
  if (err)
    return err;

  keytype_preserve_add (kt, mods, preserve);

  return 0;
}


/* Interpretations.  */

struct xkb_interpret *last_interp;
struct xkb_interpret default_interpretation;


/* Add a new interpretation.  */
error_t
interpret_new (xkb_interpret_t **new_interpret, symbol ks)
{
  struct xkb_interpret *new_interp;

  new_interp = malloc (sizeof (struct xkb_interpret));
  if (!new_interp)
    return ENOMEM;

  memcpy (new_interp, &default_interpretation, sizeof (struct xkb_interpret));
  new_interp->symbol = ks;

  if (ks)
    {
      new_interp->next = interpretations;
      interpretations = new_interp;

      if (!last_interp)
	last_interp = new_interp;
    }
  else
    {
      if (last_interp)
	last_interp->next = new_interp;
      
      last_interp = new_interp;
      
      if (!interpretations)
	interpretations = new_interp; 
    }

  *new_interpret = new_interp;

  return 0;
}


/* XXX: Dead code!?  */
/* Virtual modifiers name to number mapping.  */
/* Last number assigned to a virtual modifier.  */
static int lastvmod = 0;

/* One virtual modifiername -> vmod number mapping.  */
struct vmodname
{
  char *name;
  struct vmodname *next;
};

/* A list of virtualmodifier names and its numberic representation.  */
static struct vmodname *vmodnamel;

/* Get the number assigned to the virtualmodifier with the name
   VMODNAME.  */
int
vmod_find (char *vmodname)
{
  int i = 0;
  struct vmodname *vmn = vmodnamel;

  while (vmn)
    {
      if (!strcmp (vmn->name, vmodname))
	return (lastvmod - i);
      vmn = vmn->next;
      i++;
    }

  return 0;
}

/* Give the virtualmodifier VMODNAME a number and add it to the
   hashtable.  */
error_t
vmod_add (char *vmodname)
{
  struct vmodname *vmn;

  if (vmod_find (vmodname))
    return 0;

  vmn = malloc (sizeof (struct vmodname));
  if (vmn == NULL)
    return ENOMEM;

  vmn->name = vmodname;
  vmn->next = vmodnamel;
  vmodnamel = vmn;

  lastvmod++;
  if (lastvmod > 16)
	  debug_printf("warning: only sixteen virtual modifiers are supported, %s will not be functional.\n", vmodname);

  return 0;
}


/* XXX: Use this, no pointers.  */
struct ksrm
{
  symbol ks;

  int rmods;
};
static struct hurd_ihash ksrm_mapping;

/* Initialize the list for keysyms to realmodifiers mappings.  */
void
ksrm_init ()
{
  hurd_ihash_init (&ksrm_mapping, HURD_IHASH_NO_LOCP);
  debug_printf ("KSRM MAP IHASH CREATED \n");
}

/* Add keysym to realmodifier mapping.  */
error_t
ksrm_add (symbol ks, int rmod)
{
  hurd_ihash_add (&ksrm_mapping, ks, (void *) rmod);

  return 0;
}

/* Apply the rkms (realmods to keysyms) table to all keysyms.  */
void
ksrm_apply (void)
{
  keycode_t kc;
  for (kc = 0; kc < max_keys; kc++)
    {
      int group;
      for (group = 0; group < 4; group++)
	{
	  int cursym;
	  for (cursym = 0; cursym < keys[kc].groups[group].width; cursym++)
	    {
	      symbol ks = keys[kc].groups[group].symbols[cursym];
	      int rmods = (int) hurd_ihash_find (&ksrm_mapping, ks);

		if (rmods)
		  {
		    keys[kc].mods.rmods = rmods;
		  }
	    }
	}
    }
}


/* void */
/* indicator_new (xkb_indicator_t **,  */


/* Keycode to realmodifier mapping.  */

/* Set the current rmod for the key with keyname KEYNAME.  */
/* XXX: It shouldn't be applied immediately because the key can be
   replaced.  */
void
set_rmod_keycode (char *keyname, int rmod)
{
  keycode_t kc = keyname_find (keyname);
  keys[kc].mods.rmods = rmod;
  debug_printf ("%s (kc %d) rmod: %d\n", keyname, kc, rmod);
}

/* Initialize XKB data structures.  */
error_t
xkb_data_init (void)
{
  keyname_init ();
  keytype_init ();
  ksrm_init ();

  return 0;
}
