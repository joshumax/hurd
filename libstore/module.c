/* Dynamic loading of store class modules
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.

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

#include "store.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>		/* XXX */

static error_t
open_class (int need_open,
	    const char *name, const char *clname_end,
	    const struct store_class **classp)
{
  char *modname, *clsym;
  void *mod;

  /* Construct the name of the shared object for this module.  */
  if (asprintf (&modname,
		"libstore_%.*s%s", (int) (clname_end - name), name,
		STORE_SONAME_SUFFIX) < 0)
    return ENOMEM;

  /* Now try to load the module.

     Note we never dlclose the module, and add a ref every time we open it
     anew.  We can't dlclose it until no stores of this class exist, so
     we'd need a creation/deletion hook for that.  */

  errno = 0;
  mod = dlopen (modname, RTLD_LAZY);
  if (mod == NULL)
    {
      const char *errstring = dlerror (); /* Must always call or it leaks! */
      if (errno != ENOENT)
	/* XXX not good, but how else to report the error? */
	error (0, 0, "cannot load %s: %s", modname, errstring);
    }
  free (modname);
  if (mod == NULL)
    return errno ?: ENOENT;

  if (asprintf (&clsym, "store_%.*s_class",
		(int) (clname_end - name), name) < 0)
    {
      dlclose (mod);
      return ENOMEM;
    }

  *classp = dlsym (mod, clsym);
  free (clsym);
  if (*classp == NULL)
    {
      error (0, 0, "invalid store module %.*s: %s",
	     (int) (clname_end - name), name, dlerror ());
      dlclose (mod);
      return EGRATUITOUS;
    }

  if (need_open && ! (*classp)->open)
    {
      /* This class cannot be opened as needed.  */
      dlclose (mod);
      return EOPNOTSUPP;
    }

  return 0;
}

error_t
store_module_find_class (const char *name, const char *clname_end,
			 const struct store_class **classp)
{
  return open_class (0, name, clname_end, classp);
}

error_t
store_module_open (const char *name, int flags,
		   const struct store_class *const *classes,
		   struct store **store)
{
  const struct store_class *cl;
  const char *clname_end = strchrnul (name, ':');
  error_t err;

  err = open_class (1, name, clname_end, &cl);
  if (err)
    return err;

  if (*clname_end)
    /* Skip the ':' separating the class-name from the device name.  */
    clname_end++;

  if (! *clname_end)
    /* The class-specific portion of the name is empty, so make it *really*
       empty.  */
    clname_end = 0;

  return (*cl->open) (clname_end, flags, classes, store);
}

const struct store_class store_module_open_class =
{ -1, "module", open: store_module_open };
STORE_STD_CLASS (module_open);

error_t
store_module_decode (struct store_enc *enc,
		     const struct store_class *const *classes,
		     struct store **store)
{
  char *modname;
  void *mod;
  const struct store_class *const *cl, *const *clend;
  enum file_storage_class id;

  if (enc->cur_int >= enc->num_ints)
    /* The first int should always be the type.  */
    return EINVAL;

  id = enc->ints[enc->cur_int];

  /* Construct the name of the shared object for this module.  */
  if (asprintf (&modname, "libstore_type-%d%s", id, STORE_SONAME_SUFFIX) < 0)
    return ENOMEM;

  /* Try to open the module.  */
  mod = dlopen (modname, RTLD_LAZY);
  free (modname);
  if (mod == NULL)
    {
      (void) dlerror ();	/* otherwise it leaks */
      return ENOENT;
    }

  /* Now find its "store_std_classes" section, which points to each
     `struct store_class' defined in this module.  */
  cl = dlsym (mod, "__start_store_std_classes");
  if (cl == NULL)
    {
      error (0, 0, "invalid store module type-%d: %s", id, dlerror ());
      dlclose (mod);
      return EGRATUITOUS;
    }
  clend = dlsym (mod, "__stop_store_std_classes");
  if (clend == NULL)
    {
      error (0, 0, "invalid store module type-%d: %s", id, dlerror ());
      dlclose (mod);
      return EGRATUITOUS;
    }

  while (cl < clend)
    if ((*cl)->decode && (*cl)->id == id)
      return (*(*cl)->decode) (enc, classes, store);
    else
      ++cl;

  /* This class cannot be opened via store_decode.  */
  dlclose (mod);
  return EOPNOTSUPP;
}
