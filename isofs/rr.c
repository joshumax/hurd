/*
   Copyright (C) 1997,99,2002 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

/* Parse Rock-Ridge and related SUSP conformant extensions. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/sysmacros.h>
#include "isofs.h"

/* These tell whether the specified extensions are on or not. */
int susp_live = 0;
int rock_live = 0;
int gnuext_live = 0;

/* How far to skip when reading SUSP fields. */
int susp_skip = 0;

void
release_rrip (struct rrip_lookup *rr)
{
  if ((rr->valid & VALID_NM) && rr->name)
    free (rr->name);
  if ((rr->valid & VALID_SL) && rr->target)
    free (rr->target);
  if ((rr->valid & VALID_TR) && rr->trans)
    free (rr->trans);
}


/* Work function combining the three interfaces below. */
static int
rrip_work (struct dirrect *dr, struct rrip_lookup *rr,
	   const char *match_name, size_t match_name_len,
	   int initializing, int ignorenm)
{
  void *bp, *terminus;
  void *slbuf, *nmbuf;
  size_t slbufsize, nmbufsize;
  int nomorenm, nomoresl;

  /* Initialize RR */
  rr->valid = 0;
  rr->target = rr->name = 0;

  if (!susp_live && !initializing)
    return 0;

  /* The only extensions we currently support are rock-ridge, so
     this test will cut a lot of useless work. */
  if (!rock_live && !initializing)
    return 0;

  /* Initialized the name buffers */
  nmbuf = slbuf = 0;
  nmbufsize = slbufsize = 0;
  nomorenm = nomoresl = 0;

  /* Find the beginning and end of the SUSP area */

  /* If this is the root node, from the root directory, then
     we look in a special place.  This only happens during the two
     calls in fetch_root, and we know exactly what the value passed
     is there.  */

  if (dr == (struct dirrect *)sblock->root)
    {
      struct dirrect *p;
      off_t filestart;
      unsigned char *c;
      error_t err;

      /* Look at the first directory entry in root. */
      err = calculate_file_start (dr, &filestart, 0);
      if (err)
	return 0;		/* give up */
      p = disk_image + (filestart << store->log2_block_size);

      /* Set C to the system use area. */
      c = p->name + p->namelen;
      if ((uintptr_t)c & 1)
	c++;

      /* There needs to be an SUSP SP field right here; make sure there is */
      if (!bcmp (c, "SP\7\1\276\357", 6))
	bp = c;
      else if (!bcmp (c + 15, "SP\7\1\276\357", 6))
	/* Detect CD-ROM XA disk */
	bp = c + 15;
      else
	/* No SUSP, give up. */
	return 0;

      terminus = (char *) p + p->len;
    }
  else
    {
      /* It's in the normal place. */
      bp = dr->name + dr->namelen;
      if ((uintptr_t) bp & 1)
	bp++;			/* must be even */
      bp += susp_skip;		/* skip to start of susp area */
      terminus = (char *) dr + dr->len;
    }


  /* Loop across all the fields, processing them one at a time. */

  while (bp < terminus)
    {
      struct su_header *susp = bp;
      void *body;

      /* Make sure the whole thing fits */
      if (bp + sizeof (struct su_header) > terminus
	  || bp + susp->len > terminus)
	break;

      body = (char *) susp + sizeof (struct su_header);

      /* CE means that further extension fields are elsewhere on
	 the disk.  We just reset the pointers and keep going. */
      if (susp->sig[0] == 'C'
	  && susp->sig[1] == 'E'
	  && susp->version == 1)
	{
	  int offset;
	  int location;
	  int size;
	  struct su_ce *ce = body;

	  offset = isonum_733 (ce->offset);
	  location = isonum_733 (ce->continuation);
	  size = isonum_733 (ce->size);

	  /* Reset pointers */
	  bp = disk_image + (location * logical_block_size) + offset;
	  terminus = bp + size;

	  /* NOT goto next_field */
	  continue;
	}

      /* Only on the root node; SP signals that the sharing protocol
	 is in use. */
      if (initializing
	       && susp->sig[0] == 'S'
	       && susp->sig[1] == 'P'
	       && susp->version == 1)
	{
	  /* Sharing Protocol */
	  struct su_sp *sp = body;

	  /* Verify magic numbers */
	  if (sp->check[0] == SU_SP_CHECK_0
	      && sp->check[1] == SU_SP_CHECK_1)
	    susp_live = 1;

	  susp_skip = sp->skip;

	  goto next_field;
	}

      /* Only on the root node; ER signals that a specified extension
	 is present.  We implement and check for only the Rock Ridge
	 extension. */
      if (initializing
	  && susp->sig[0] == 'E'
	  && susp->sig[1] == 'R'
	  && susp->version == 1)
	{
	  /* Extension Reference */
	  struct su_er *er = body;

	  /* Make sure the ER field is valid */
	  if ((void *) er->more + er->len_id + er->len_des + er->len_src
	      < terminus)
	    goto next_field;

	  /* Check for rock-ridge */
	  if (er->ext_ver == ROCK_VERS
	      && !memcmp (ROCK_ID, er->more, er->len_id))
	    rock_live = 1;

	  /* Check for Gnuext */
	  else if (er->ext_ver == GNUEXT_VERS
		   && !memcmp (GNUEXT_ID, er->more, er->len_id))
	    gnuext_live = 1;
	}

      /* PD fields are padding and just get ignored. */
      if (susp->sig[0] == 'P'
	  && susp->sig[1] == 'D'
	  && susp->version == 1)
	goto next_field;

      /* ST fields mean that there are no more SUSP fields to be processed. */
      if (susp->sig[0] == 'S'
	  && susp->sig[1] == 'T'
	  && susp->version == 1)
	/* All done */
	break;

      /* The rest are Rock-Ridge, and are not interesting if we are doing
	 setup. */

      if (initializing || !rock_live)
	goto next_field;

      /* RE is present in a directory entry to mean that the node
	 is specified by a CL field elsewhere.  So this entry needs
	 to be ignored by anyone who understands CL fields. */
      if (susp->sig[0] == 'R'
	  && susp->sig[1] == 'E'
	  && susp->version == 1)
	{
	  rr->valid |= VALID_RE;

	  /* No point in parsing anything else now. */
	  break;
	}

      /* NM identifies the real name of the file; it overrides
	 the name in the directory. */
      if (susp->sig[0] == 'N'
	  && susp->sig[1] == 'M'
	  && susp->version == 1
	  && !ignorenm)
	{
	  struct rr_nm *nm = body;
	  size_t nmlen = susp->len - 5;
	  char *name;
	  size_t namelen;

	  if (nomorenm)
	    goto next_field;

	  if (nm->flags & NAME_DOT)
	    {
	      name = ".";
	      namelen = 1;
	      goto finalize_nm;
	    }
	  else if (nm->flags & NAME_DOTDOT)
	    {
	      name = "..";
	      namelen = 2;
	      goto finalize_nm;
	    }
	  else if (nm->flags & NAME_HOST)
	    {
	      name = host_name;
	      namelen = strlen (host_name);
	      goto finalize_nm;
	    }

	  /* Add this component to the list. */

	  /* We don't store a trailing null here, but we always leave
	     room for it.  The null gets stored in the finalization
	     code below. */
	  if (!nmbuf)
	    nmbuf = malloc ((nmbufsize = nmlen) + 1);
	  else
	    nmbuf = realloc (nmbuf, (nmbufsize += nmlen) + 1);
	  assert_backtrace (nmbuf);

	  memcpy (nmbuf + nmbufsize - nmlen, nm->name, nmlen);

	  if (nm->flags & NAME_CONTINUE)
	    goto next_field;

	  name = nmbuf;
	  namelen = nmbufsize;

	finalize_nm:
	  nomorenm = 1;

	  /* Is this a failed match? */
	  if (match_name && (match_name_len != namelen
	      || memcmp (match_name, name, match_name_len)))
	    {
	      if (nmbuf)
		free (nmbuf);
	      return 0;
	    }

	  /* Store the name */
	  rr->valid |= VALID_NM;
	  if (name != nmbuf)
	    {
	      rr->name = strdup (name);
	      assert_backtrace (rr->name);
	    }
	  else
	    {
	      rr->name = name;
	      name[namelen] = '\0';
	    }

	  if (rr->valid & VALID_CL)
	    /* Finalize CL processing. */
	    goto clrecurse;

	  goto next_field;
	}

      /* PX gives mode, nlink, uid, and gid posix-style attributes. */
      if (susp->sig[0] == 'P'
	  && susp->sig[1] == 'X'
	  && susp->version == 1)
	{
	  struct rr_px *px = body;

	  rr->valid |= VALID_PX;

	  rr->mode = isonum_733 (px->mode);
	  rr->nlink = isonum_733 (px->nlink);
	  rr->uid = isonum_733 (px->uid);
	  rr->gid = isonum_733 (px->gid);

	  goto next_field;
	}

      /* PN, for S_ISCHR and S_ISDEV devices gives the magic numbers */
      if (susp->sig[0] == 'P'
	  && susp->sig[1] == 'N'
	  && susp->version == 1)
	{
	  struct rr_pn *pn = body;

	  rr->valid |= VALID_PN;
	  rr->rdev = gnu_dev_makedev (isonum_733 (pn->high), isonum_733 (pn->low));

	  goto next_field;
	}

      /* SL tells, for a symlink, what the target of the link is */
      if (susp->sig[0] == 'S'
	  && susp->sig[1] == 'L'
	  && susp->version == 1)
	{
	  struct rr_sl *sl = body;
	  size_t crlen = susp->len - 5;
	  struct rr_sl_comp *comp;
	  void *cp;
	  size_t targalloced, targused;

	  void add_comp (char *cname, size_t cnamelen)
	    {
	      if (rr->target == 0)
		{
		  rr->target = malloc (cnamelen * 2);
		  targused = 0;
		  targalloced = cnamelen * 2;
		}
	      else while (targused + cnamelen > targalloced)
		rr->target = realloc (rr->target, targalloced *= 2);
	      assert_backtrace (rr->target);

	      memcpy (rr->target + targused, cname, cnamelen);
	      targused += cnamelen;
	    }

	  if (nomoresl)
	    goto next_field;

	  /* Append the component use fields to the records we are saving
	     up */

	  if (!slbuf)
	    slbuf = malloc (slbufsize = crlen);
	  else
	    slbuf = realloc (slbuf, slbufsize += crlen);
	  assert_backtrace (slbuf);

	  memcpy (slbuf + slbufsize - crlen, sl->data, crlen);

	  if (sl->flags & 1)
	    /* We'll finish later. */
	    goto next_field;

	  /* Do the symlink translation */
	  for (cp = slbuf; cp < slbuf + slbufsize; cp += comp->len + 2)
	    {
	      comp = (struct rr_sl_comp *)cp;
	      nomoresl = 1;

	      /* Put in a slash after each component as we go,
		 unless it's a "continuation" component. */

	      if (comp->flags & NAME_DOT)
		add_comp ("./", 2);
	      else if (comp->flags & NAME_DOTDOT)
		add_comp ("../", 3);
	      else if (comp->flags & NAME_ROOT)
		{
		  targused = 0;
		  add_comp ("/", 1);
		}
	      else if (comp->flags & NAME_VOLROOT)
		{
		  targused = 0;
		  add_comp (mounted_on, strlen (mounted_on));
		}
	      else if (comp->flags & NAME_HOST)
		{
		  add_comp (host_name, strlen (host_name));
		  add_comp ("/", 1);
		}
	      else
		{
		  add_comp (comp->name, comp->len);
		  if (!(comp->flags & NAME_CONTINUE))
		    add_comp ("/", 1);
		}
	    }

	  /* And turn the final character, if it's a slash, into a null.
	     Otherwise, add a null. */
	  if (rr->target[targused - 1] == '/')
	    rr->target[targused - 1] = '\0';
	  else
	    add_comp ("", 1);

	  rr->valid |= VALID_SL;

	  free (slbuf);
	  goto next_field;
	}

      /* TF gives atime, mtime, ctime (and others we don't care about);
	 this overrides the time specified in the directory. */
      if (susp->sig[0] == 'T'
	  && susp->sig[1] == 'F'
	  && susp->version == 1)
	{
	  char *(*convert)(char *, struct timespec *);
	  struct rr_tf *tf = body;
	  char *c;

	  if (tf->flags & TF_LONG_FORM)
	    convert = isodate_84261;
	  else
	    convert = isodate_915;

	  rr->valid |= VALID_TF;
	  rr->tfflags = tf->flags;
	  c = tf->data;

	  if (rr->tfflags & TF_CREATION)
	    c = (*convert) (c, &rr->ctime);
	  if (rr->tfflags & TF_MODIFY)
	    c = (*convert) (c, &rr->mtime);
	  if (rr->tfflags & TF_ACCESS)
	    c = (*convert) (c, &rr->atime);

	  goto next_field;
	}

      /* CL means that this entry is a relocated directory.  We ignore
	 the attributes in this directory entry (except for NM); they
	 are fetched from the "." entry of the directory itself.  The
	 CL field identifies the location of the directory, overriding
	 the location given in the present directory.  This directory
	 is listed somewhere else too (to keep the format ISO 9660 compliant),
	 but there's an RE entry on that one so that we ignore it. */
      if (susp->sig[0] == 'C'
	  && susp->sig[1] == 'L'
	  && susp->version == 1)
	{
	  struct rr_cl *cl = body;

	  rr->realdirent
	    = disk_image + (isonum_733 (cl->loc) * logical_block_size);
	  rr->valid |= VALID_CL;

	  if (rr->valid & VALID_NM)
	    {
	      /* We've gotten all we care about from this node.
		 Remember the NM name, and load all the contents
		 from the new location. */
	      char *savename;
	      struct dirrect *realdir;

	    clrecurse:
	      /* It might look like VALID_NM is alway set here, but if
		 we got here from the exit point of the function, then
		 VALID_NM is actually clear. */

	      /* Save these, because rrip_work will clear them. */
	      savename = (rr->valid & VALID_NM) ? rr->name : 0;
	      realdir = rr->realdirent;

	      rrip_work (realdir, rr, 0, 0, 0, 1);

	      rr->valid |= VALID_CL;
	      rr->realdirent = realdir;
	      if (savename)
		{
		  rr->valid |= VALID_NM;
		  rr->name = savename;
		}

	      /* If there's an NM field, then we must have matched
		 if we got here. */
	      return (rr->valid & VALID_NM) ? 1 : 0;
	    }

	  /* We must keep looking for an NM record.  When we find one,
	     the NM code will goto the above piece of code. */
	  goto next_field;
	}

      /* PL is found in the ".." entry of a relocated directory.
	 The present directory entry points to the fictitious parent
	 (the one that holds the fictitious RE link here); the PL
	 field identifies the real parent (the one that has the CL
	 entry). */
      if (susp->sig[0] == 'P'
	  && susp->sig[1] == 'L'
	  && susp->version == 1)
	{
	  struct rr_pl *pl = body;

	  rr->realfilestart = (isonum_733 (pl->loc)
			       * (logical_block_size
				  >> store->log2_block_size));
	  rr->valid |= VALID_PL;
	  goto next_field;
	}

      /* The rest are GNU ext. */
      if (!gnuext_live)
	goto next_field;

      /* Author */
      if (susp->sig[0] == 'A'
	  && susp->sig[1] == 'U'
	  && susp->version == 1)
	{
	  struct gn_au *au = body;

	  rr->author = isonum_733 (au->author);
	  rr->valid |= VALID_AU;

	  goto next_field;
	}

      if (susp->sig[0] == 'T'
	  && susp->sig[1] == 'R'
	  && susp->version == 1)
	{
	  struct gn_tr *tr = body;

	  rr->translen = tr->len;
	  rr->trans = malloc (rr->translen);
	  assert_backtrace (rr->trans);
	  memcpy (tr->data, rr->trans, rr->translen);
	  rr->valid |= VALID_TR;

	  goto next_field;
	}

      if (susp->sig[0] == 'M'
	  && susp->sig[1] == 'D'
	  && susp->version == 1)
	{
	  struct gn_md *md = body;

	  rr->allmode = isonum_733 (md->mode);
	  rr->valid |= VALID_MD;

	  goto next_field;
	}

      if (susp->sig[0] == 'F'
	  && susp->sig[1] == 'L'
	  && susp->version == 1)
	{
	  struct gn_fl *fl = body;

	  rr->flags = isonum_733 (fl->flags);
	  rr->valid |= VALID_FL;

	  goto next_field;
	}

    next_field:
      bp = bp + susp->len;
    }

  if (rr->valid & VALID_CL)
    goto clrecurse;

  /* If we saw an NM field, then it matched; otherwise we
     didn't see one. */
  return rr->valid & VALID_NM ? 1 : 0;
}

/* Parse extensions for directory entry DR.  If we encounter an NM
   record, and it does not match NAME (length NAMELEN), then stop
   immediately (but do note the NM file in RR->valid) and return zero.
   If we encounter no NM record at all, then process all the fields
   normally and return zero.  If we encounter an NM field which matches
   the provided name, then process all the fields and return 1.  In any
   case, fill RR with information corresponding to the fields we do
   encounter. */
int
rrip_match_lookup (struct dirrect *dr, const char *name, size_t namelen,
		   struct rrip_lookup *rr)
{
  return rrip_work (dr, rr, name, namelen, 0, 0);
}

/* Parse extensions for dirrect DR and store the results in RR.
   If IGNORENM, then do not bother with NM records. */
void
rrip_lookup (struct dirrect *dr, struct rrip_lookup *rr, int ignorenm)
{
  rrip_work (dr, rr, 0, 0, 0, ignorenm);
}

/* Scan extensions on dirrect DR looking for the tags that are supposed
   to be on the root directory. */
void
rrip_initialize (struct dirrect *dr)
{
  struct rrip_lookup rr;
  rrip_work (dr, &rr, 0, 0, 1, 1);
  release_rrip (&rr);
}
