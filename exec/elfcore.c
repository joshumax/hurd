{
  processor_set_name_t pset;
  host_t host;
  processor_set_basic_info_data_t pinfo;

  thread_t *threads;
  size_t nthreads;

  vm_address_t addr;
  vm_size_t size;
  vm_prot_t prot, maxprot;
  vm_inherit_t inherit;
  boolean_t shared;
  memory_object_name_t objname;
  vm_offset_t offset;

  /* Figure out what flavor of machine the task is on.  */
  if (err = task_get_assignment (task, &pset))
    goto lose;
  err = processor_set_info (pset, PROCESSOR_SET_BASIC_INFO, &host,
			    &pinfo, PROCESSOR_SET_BASIC_INFO_COUNT);
  mach_port_deallocate (mach_task_self (), pset);
  if (err)
    goto lose;
  err = bfd_mach_host_arch_mach (host, &arch, &machine, &e_machine);
  mach_port_deallocate (mach_task_self (), host);
  if (err)
    goto lose;

  if (err = task_threads (task, &threads, &nthreads))
    goto lose;

  /* Create a BFD section to describe each contiguous chunk
     of the task's address space with the same stats.  */
  sec = NULL;
  addr = 0;
  while (!vm_region (task, &addr, &size, &prot, &maxprot,
		     &inherit, &shared, &objname, &offset))
    {
      mach_port_deallocate (mach_task_self (), objname);

      if (prot != VM_PROT_NONE)
	{
	  flagword flags = SEC_NO_FLAGS;

	  if (!(prot & VM_PROT_WRITE))
	    flags |= SEC_READONLY;
	  if (!(prot & VM_PROT_EXECUTE))
	    flags |= SEC_DATA;

	  if (sec != NULL &&
	      (vm_address_t) (bfd_section_vma (bfd, sec) +
			      bfd_section_size (bfd, sec)) == addr &&
	      flags == (bfd_get_section_flags (bfd, sec) &
			(SEC_READONLY|SEC_DATA)))
	    /* Coalesce with the previous section.  */
	    bfd_set_section_size (bfd, sec,
				  bfd_section_size (bfd, sec) + size);
	  else
	    {
	      /* Make a new section (which might grow by
		 the next region being coalesced onto it). */
	      char *name = bfd_intuit_section_name (addr, size, &flags);
	      if (name == NULL)
		{
		  /* No guess from BFD.  */
		  if (asprintf (&name, "[%p,%p) %c%c%c",
				(void *) addr, (void *) (addr + size),
				(prot & VM_PROT_READ) ? 'r' : '-',
				(prot & VM_PROT_WRITE) ? 'w' : '-',
				(prot & VM_PROT_EXECUTE) ? 'x' : '-') == -1)
		    goto lose;
		}
	      sec = bfd_make_section (name);
	      bfd_set_section_flags (bfd, sec, flags);
	      bfd_set_section_vma (bfd, sec, addr);
	      bfd_set_section_size (bfd, sec, size);
	    }
	}
    }

  /* Write all the sections' data.  */
  for (sec = bfd->sections; sec != NULL; sec = sec->next)
    {
      void *data;
      err = vm_read (task, bfd_section_vma (bfd, sec),
		     bfd_section_size (bfd, sec), &data);
      if (err)
	/* XXX What to do?
	  1. lose
	  2. remove this section
	  3. mark this section as having ungettable contents (how?)
	    */
	goto lose;
      err = bfd_set_section_contents (bfd, sec, data, 0,
				      bfd_section_size (bfd, sec));
      vm_deallocate (mach_task_self (), data, bfd_section_size (bfd, sec));
      if (err)
	goto bfdlose;
    }
