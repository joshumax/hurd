#include <hurd.h>
#include <elf.h>
#include <link.h>
#include <string.h>
#include <argz.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/procfs.h>
#include <stddef.h>


#define ELF_MACHINE	EM_386	/* XXX */

#define ELF_CLASS	PASTE (ELFCLASS, __ELF_NATIVE_CLASS)
#define PASTE(a, b)	PASTE_1 (a, b)
#define PASTE_1(a, b)	a##b

#include <endian.h>
#if BYTE_ORDER == BIG_ENDIAN
#define ELF_DATA ELFDATA2MSB
#elif BYTE_ORDER == LITTLE_ENDIAN
#define ELF_DATA ELFDATA2LSB
#endif


#define TIME_VALUE_TO_TIMESPEC(tv, ts) \
  TIMEVAL_TO_TIMESPEC ((struct timeval *) (tv), (ts))

#define PAGES_TO_KB(x)	((x) * (vm_page_size / 1024))
#define ENCODE_PCT(f)	((uint16_t) ((f) * 32768.0))

extern process_t procserver;

error_t
dump_core (task_t task, file_t file, off_t corelimit,
	   int signo, long int sigcode, int sigerror)
{
  static float host_memory_size = -1.0;
  error_t err;
  ElfW(Phdr) *phdrs, *ph;
  ElfW(Ehdr) hdr =		/* ELF header for the core file.  */
  {
    e_ident:
    {
      [EI_MAG0] = ELFMAG0,
      [EI_MAG1] = ELFMAG1,
      [EI_MAG2] = ELFMAG2,
      [EI_MAG3] = ELFMAG3,
      [EI_CLASS] = ELF_CLASS,
      [EI_DATA] = ELF_DATA,
      [EI_VERSION] = EV_CURRENT,
      [EI_OSABI] = ELFOSABI_SYSV,
      [EI_ABIVERSION] = 0
    },
    e_type: ET_CORE,
    e_version: EV_CURRENT,
    e_machine: ELF_MACHINE,
    e_ehsize: sizeof hdr,
    e_phentsize: sizeof phdrs[0],
    e_phoff: sizeof hdr,	/* Fill in e_phnum later.  */
  };
  off_t offset;
  size_t wrote;

  pid_t pid;
  thread_t *threads;
  size_t nthreads, i;
  off_t notestart;

  /* Helper macros for writing notes.  */
#define DEFINE_NOTE(typename) struct { struct note_header hdr; typename data; }
#define WRITE_NOTE(type, var) ({ 					      \
  (var).hdr = NOTE_HEADER ((type), sizeof (var).data);			      \
  write_note (&(var).hdr);						      \
})
  struct note_header
  {
    ElfW(Nhdr) note;
    char name[4];
  };
#define NOTE_HEADER(type, size) \
  ((struct note_header) { { 4, (size), (type) }, "CORE" })
  inline error_t write_note (struct note_header *hdr)
    {
      error_t err = 0;
      char *data = (char *) hdr;
      size_t size = sizeof *hdr + hdr->note.n_descsz;
      if (corelimit >= 0 && offset + size > corelimit)
	size = corelimit - offset;
      while (size > 0)
	{
	  err = io_write (file, data, size, offset, &wrote);
	  if (err)
	    return err;
	  offset = (offset + wrote + 3) &~ 3; /* Pad it to word alignment.  */
	  if (wrote > size)
	    break;
	  data += wrote;
	  size -= wrote;
	}
      return err;
    }

  struct vm_region_list
  {
    struct vm_region_list *next;
    vm_prot_t protection;
    vm_address_t start;
    vm_size_t length;
  };
  struct vm_region_list *regions = NULL, **tailp = &regions, *r;
  unsigned int nregions = 0;

  if (corelimit >= 0 && corelimit < sizeof hdr)
    return EFBIG;

  {
    /* Examine the task and record the locations of contiguous memory
       segments that we will dump into the core file.  */

    vm_address_t region_address, last_region_address, last_region_end;
    vm_prot_t last_protection;
#define RECORD_LAST_REGION do {						      \
    if (last_region_end > last_region_address				      \
	&& last_protection != VM_PROT_NONE)				      \
      record_last_region (alloca (sizeof (struct vm_region_list))); } while (0)
    inline void record_last_region (struct vm_region_list *region)
      {
	*tailp = region;
	tailp = &region->next;
	region->next = NULL;
	region->start = last_region_address;
	region->length = last_region_end - last_region_address;
	region->protection = last_protection;
	++nregions;
      }

    region_address = last_region_address = last_region_end = VM_MIN_ADDRESS;
    last_protection = VM_PROT_NONE;
    while (region_address < VM_MAX_ADDRESS)
      {
	vm_prot_t protection;
	vm_prot_t max_protection;
	vm_inherit_t inheritance;
	boolean_t shared;
	mach_port_t object_name;
	vm_offset_t offset;
	vm_size_t region_length = VM_MAX_ADDRESS - region_address;

	err = vm_region (task,
			 &region_address,
			 &region_length,
			 &protection,
			 &max_protection,
			 &inheritance,
			 &shared,
			 &object_name,
			 &offset);
	if (err == KERN_NO_SPACE)
	  break;
	if (err != KERN_SUCCESS)
	  return err;

	if (protection == last_protection && region_address == last_region_end)
	  /* This region is contiguous with and indistinguishable from
	     the previous one, so we just extend that one.  */
	  last_region_end = region_address += region_length;
	else
	  {
	    /* This region is distinct from the last one we saw,
	       so record that previous one.  */
	    RECORD_LAST_REGION;
	    last_region_address = region_address;
	    last_region_end = region_address += region_length;
	    last_protection = protection;
	  }
      }
    /* Record the final region.  */
    RECORD_LAST_REGION;
  }

  /* Now we start laying out the file.  */
  offset = round_page (sizeof hdr + ((nregions + 1) * sizeof *phdrs));

  /* Final check for tiny core limit.  From now on, we will simply truncate
     the file at CORELIMIT but not change the contents of what we write.  */
  if (corelimit >= 0 && corelimit < offset)
    return EFBIG;

  /* Now we can complete the file header and write it.  */
  hdr.e_phnum = nregions + 1;
  err = io_write (file, (char *) &hdr, sizeof hdr, 0, &wrote);
  if (err)
    return err;
  if (wrote < sizeof hdr)
    return EGRATUITOUS;		/* XXX */

  /* Now we can write the various notes.  */
  notestart = offset;

  /* First a dull note containing the results of `uname', a la Solaris.  */
  {
    DEFINE_NOTE (struct utsname) note;
    if (uname (&note.data) == 0) /* XXX Use proc_uname on task's proc port?  */
      err = WRITE_NOTE (NT_UTSNAME, note);
  }
  if (err || (corelimit >= 0 && corelimit <= offset))
    return err;

  err = proc_task2pid (procserver, task, &pid);
  if (err)
    return err;

  /* Make sure we have the total RAM size of the host.
     We only do this once, assuming that it won't change.
     XXX this could use the task's host-self port instead. */
  if (host_memory_size <= 0.0)
    {
      host_basic_info_data_t hostinfo;
      mach_msg_type_number_t size = sizeof hostinfo;
      error_t err = host_info (mach_host_self (), HOST_BASIC_INFO,
			       (host_info_t) &hostinfo, &size);
      if (err == 0)
	host_memory_size = hostinfo.memory_size;
    }

  /* The psinfo_t note contains some process-global info we should get from
     the proc server, but no thread-specific info like register state.  */
  {
    DEFINE_NOTE (psinfo_t) note;
    int flags = PI_FETCH_TASKINFO | PI_FETCH_THREADS | PI_FETCH_THREAD_BASIC;
    char *waits = 0;
    mach_msg_type_number_t num_waits = 0;
    char pibuf[offsetof (struct procinfo, threadinfos[2])];
    struct procinfo *pi = (void *) &pibuf;
    mach_msg_type_number_t pi_size = sizeof pibuf;
    err = proc_getprocinfo (procserver, pid, &flags,
			    (procinfo_t *) &pi, &pi_size,
			    &waits, &num_waits);
    if (err == 0)
      {
	if (num_waits != 0)
	  munmap (waits, num_waits);
	memset (&note.data, 0, sizeof note.data);
	note.data.pr_flag = pi->state;
	note.data.pr_nlwp = pi->nthreads;
	note.data.pr_pid = pid;
	note.data.pr_ppid = pi->ppid;
	note.data.pr_pgid = pi->pgrp;
	note.data.pr_sid = pi->session;
	note.data.pr_euid = pi->owner;
	/* XXX struct procinfo should have these */
	note.data.pr_egid = note.data.pr_gid = note.data.pr_uid = -1;
	note.data.pr_size = PAGES_TO_KB (pi->taskinfo.virtual_size);
	note.data.pr_rssize = PAGES_TO_KB (pi->taskinfo.resident_size);
	{
	  /* Sum all the threads' cpu_usage fields.  */
	  integer_t cpu_usage = 0;
	  for (i = 0; i < pi->nthreads; ++i)
	    cpu_usage += pi->threadinfos[i].pis_bi.cpu_usage;
	  note.data.pr_pctcpu = ENCODE_PCT ((float) cpu_usage
					    / (float) TH_USAGE_SCALE);
	}
	if (host_memory_size > 0.0)
	  note.data.pr_pctmem = ENCODE_PCT ((float) pi->taskinfo.resident_size
					    / host_memory_size);
	TIME_VALUE_TO_TIMESPEC (&pi->taskinfo.creation_time,
				&note.data.pr_start);
	timeradd ((const struct timeval *) &pi->taskinfo.user_time,
		  (const struct timeval *) &pi->taskinfo.system_time,
		  (struct timeval *) &pi->taskinfo.user_time);
	TIME_VALUE_TO_TIMESPEC (&pi->taskinfo.user_time, &note.data.pr_time);
	/* XXX struct procinfo should have dead child info for pr_ctime */
	note.data.pr_wstat = pi->exitstatus;

	if ((void *) pi != &pibuf)
	  munmap (pi, pi_size);

	{
	  /* We have to nab the process's own proc port to get the
	     proc server to tell us its registered arg locations.  */
	  process_t proc;
	  err = proc_task2proc (procserver, task, &proc);
	  if (err == 0)
	    {
	      err = proc_get_arg_locations (proc,
					    &note.data.pr_argv,
					    &note.data.pr_envp);
	      mach_port_deallocate (mach_task_self (), proc);
	    }
	  err = 0;
	}
	{
	  /* Now fetch the arguments.  We could do this directly from the
	     task given the locations we now have.  But we are lazy and have
	     the proc server do it for us.  */
	  char *data = note.data.pr_psargs;
	  size_t datalen = sizeof note.data.pr_psargs;
	  err = proc_getprocargs (procserver, pid, &data, &datalen);
	  if (err == 0)
	    {
	      note.data.pr_argc = argz_count (data, datalen);
	      argz_stringify (data, datalen, ' ');
	      if (data != note.data.pr_psargs)
		{
		  memcpy (note.data.pr_psargs, data,
			  sizeof note.data.pr_psargs);
		  munmap (data, datalen);
		}
	    }
	}
	err = WRITE_NOTE (NT_PSTATUS, note);
      }

#if 0
    note.data.pr_info.si_signo = signo;
    note.data.pr_info.si_code = sigcode;
    note.data.pr_info.si_errno = sigerror;
#endif
  }
  if (err || (corelimit >= 0 && corelimit <= offset))
    return err;

  /* Now examine all the threads in the task.
     For each thread we produce one or more notes.  */
  err = task_threads (task, &threads, &nthreads);
  if (err)
    return err;
  for (i = 0; i < nthreads; ++i)
    {
      {
	/* lwpinfo_t a la Solaris gives thread's CPU time and such.  */
	DEFINE_NOTE (struct thread_basic_info) note;
	mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
	err = thread_info (threads[i], THREAD_BASIC_INFO,
			   (thread_info_t)&note.data, &count);
	if (err == 0)
	  err = WRITE_NOTE (NT_LWPSINFO, note);
	else			/* Just skip it if we can't get the info.  */
	  err = 0;
      }
      if (err || (corelimit >= 0 && corelimit <= offset))
	break;

#ifdef WRITE_THREAD_NOTES
      /* XXX Here would go the note flavors for machine thread states.  */
      err = WRITE_THREAD_NOTES (i, threads[i]);
#endif
      if (err || (corelimit >= 0 && corelimit <= offset))
	break;
      mach_port_deallocate (mach_task_self (), threads[i]);
    }
  while (i < nthreads)
    mach_port_deallocate (mach_task_self (), threads[i++]);
  munmap (threads, nthreads * sizeof *threads);
  if (err || (corelimit >= 0 && corelimit <= offset))
    return err;

  /* Make an array of program headers and fill them in.
     The first one describes the note segment.  */
  ph = phdrs = alloca ((nregions + 1) * sizeof *phdrs);

  memset (ph, 0, sizeof *ph);
  ph->p_type = PT_NOTE;
  ph->p_offset = notestart;
  ph->p_filesz = offset - notestart;
  ++ph;

  /* Now make ELF program headers for each of the record memory regions.
     Consistent with the Linux kernel, we create PT_LOAD headers with
     p_filesz = 0 for the read-only segments that we are not dumping
     into the file.  */
  for (r = regions; r != NULL; r = r->next)
    {
      memset (ph, 0, sizeof *ph);
      ph->p_type = PT_LOAD;
      ph->p_align = vm_page_size;
      ph->p_flags = (((r->protection & VM_PROT_READ) ? PF_R : 0)
		     | ((r->protection & VM_PROT_WRITE) ? PF_W : 0)
		     | ((r->protection & VM_PROT_EXECUTE) ? PF_X : 0));
      ph->p_vaddr = r->start;
      ph->p_memsz = r->length;
      ph->p_filesz = (r->protection & VM_PROT_WRITE) ? ph->p_memsz : 0;
      ph->p_offset = offset;
      offset += ph->p_filesz;
      ++ph;
    }

  /* Now write the memory segment data.  */
  for (ph = &phdrs[1]; ph < &phdrs[nregions + 1]; ++ph)
    if (ph->p_filesz > 0)
      {
	vm_address_t va = ph->p_vaddr;
	vm_size_t sz = ph->p_memsz;
	off_t ofs = ph->p_offset;
	int wrote_any = 0;
	do
	  {
	    pointer_t copied;
	    int copy_count;
	    err = vm_read (task, va, sz, &copied, &copy_count);
	    if (err == 0)
	      {
		char *data = (void *) copied;
		size_t left = copy_count, wrote;

		va += copy_count;
		sz -= copy_count;

		do
		  {
		    if (corelimit >= 0 && ofs + left > corelimit)
		      left = corelimit - ofs;
		    err = io_write (file, data, left, ofs, &wrote);
		    if (err)
		      break;
		    ofs += wrote;
		    data += wrote;
		    left -= wrote;
		    if (ofs >= corelimit)
		      break;
		  } while (left > 0);

		munmap ((void *) copied, copy_count);

		if (left < copy_count)
		  wrote_any = 1;
	      }
	    else
	      {
		/* Leave a hole in the file for pages we can't read.  */
		va += vm_page_size;
		sz -= vm_page_size;
		ofs += vm_page_size;
	      }
	  } while (sz > 0 && (corelimit < 0 || ofs < corelimit));

	if (! wrote_any)
	  /* If we failed to write any contents at all,
	     don't claim the big hole as the contents.  */
	  ph->p_filesz = 0;
      }

  /* Finally, we go back and write the program headers.  */
  err = io_write (file, (char *) phdrs, (nregions + 1) * sizeof phdrs[0],
		  sizeof hdr, &wrote);

  return err;
}
