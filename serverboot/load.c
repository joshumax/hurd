/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <stddef.h>
#include <assert.h>
#include <mach/mach_interface.h>
#include "mach-exec.h"
#include "../boot/boot_script.h"

#include <file_io.h>


boolean_t	load_protect_text = TRUE;


struct stuff
{
	struct file *fp;
	task_t user_task;

	/* uncompressed image */
	vm_offset_t image_addr;
	vm_size_t image_size;

	vm_offset_t aout_symtab_ofs;
	vm_size_t aout_symtab_size;
	vm_offset_t aout_strtab_ofs;
	vm_size_t aout_strtab_size;
};

char *set_regs(
	mach_port_t	user_task,
	mach_port_t	user_thread,
	struct exec_info *info,
	int		arg_size);

static void read_symtab_from_file(
	struct file	*fp,
	mach_port_t	host_port,
	task_t		task,
	char *		symtab_name,
	struct stuff	*st);

/* Callback functions for reading the executable file.  */
static int prog_read(void *handle, vm_offset_t file_ofs, void *buf, vm_size_t size,
		     vm_size_t *out_actual)
{
	struct stuff *st = handle;
	vm_size_t resid;
	int result;

	result = read_file(st->fp, file_ofs, buf, size, &resid);
	if (result)
		return result;
	*out_actual = size - resid;
	return 0;
}

static int prog_read_exec(void *handle, vm_offset_t file_ofs, vm_size_t file_size,
		   	  vm_offset_t mem_addr, vm_size_t mem_size,
			  exec_sectype_t sec_type)
{
	struct stuff *st = handle;
	vm_offset_t page_start = trunc_page(mem_addr);
	vm_offset_t page_end = round_page(mem_addr + mem_size);
	vm_prot_t mem_prot = sec_type & EXEC_SECTYPE_PROT_MASK;
	vm_offset_t area_start;
	int result;

	if (sec_type & EXEC_SECTYPE_AOUT_SYMTAB)
	{
		st->aout_symtab_ofs = file_ofs;
		st->aout_symtab_size = file_size;
	}
	if (sec_type & EXEC_SECTYPE_AOUT_STRTAB)
	{
		st->aout_strtab_ofs = file_ofs;
		st->aout_strtab_size = file_size;
	}

	if (!(sec_type & EXEC_SECTYPE_ALLOC))
		return 0;

	assert(mem_size > 0);
	assert(mem_size > file_size);

	/*
	printf("section %08x-%08x-%08x prot %08x (%08x-%08x)\n",
		mem_addr, mem_addr+file_size, mem_addr+mem_size, mem_prot, page_start, page_end);
	*/

	result = vm_allocate(mach_task_self(), &area_start, page_end - page_start, TRUE);
	if (result) return (result);

	if (file_size > 0)
	{
		vm_size_t resid;

		result = read_file(st->fp, file_ofs, area_start + (mem_addr - page_start),
				   file_size, &resid);
		if (result) return result;
		if (resid) return EX_CORRUPT;
	}

	if (mem_size > file_size)
	{
		bzero((void*)area_start + (mem_addr + file_size - page_start),
			mem_size - file_size);
	}

	result = vm_allocate(st->user_task, &page_start, page_end - page_start, FALSE);
	if (result) return (result);
	assert(page_start == trunc_page(mem_addr));

	result = vm_write(st->user_task, page_start, area_start, page_end - page_start);
	if (result) return (result);

	result = vm_deallocate(mach_task_self(), area_start, page_end - page_start);
	if (result) return (result);

	/*
	 * Protect the segment.
	 */
	if (load_protect_text && (mem_prot != VM_PROT_ALL)) {
		result = vm_protect(st->user_task, page_start, page_end - page_start,
				    FALSE, mem_prot);
		if (result) return (result);
	}

	return 0;
}

/* Callback functions for reading the uncompressed image. */
static int image_read(void *handle, vm_offset_t file_ofs, void *buf,
	 	      vm_size_t size, vm_size_t *out_actual)
{
	struct stuff *st = handle;
	bcopy(st->image_addr + file_ofs, buf, size);
	*out_actual = size;
	return 0;
}

static int image_read_exec(void *handle, vm_offset_t file_ofs,
			   vm_size_t file_size, vm_offset_t mem_addr,
			   vm_size_t mem_size, exec_sectype_t sec_type)
{
	struct stuff *st = handle;
	vm_offset_t page_start = trunc_page(mem_addr);
	vm_offset_t page_end = round_page(mem_addr + mem_size);
	vm_prot_t mem_prot = sec_type & EXEC_SECTYPE_PROT_MASK;
	vm_offset_t area_start;
	int result;

	if (sec_type & EXEC_SECTYPE_AOUT_SYMTAB)
	{
		st->aout_symtab_ofs = file_ofs;
		st->aout_symtab_size = file_size;
	}
	if (sec_type & EXEC_SECTYPE_AOUT_STRTAB)
	{
		st->aout_strtab_ofs = file_ofs;
		st->aout_strtab_size = file_size;
	}

	if (!(sec_type & EXEC_SECTYPE_ALLOC))
		return 0;

	assert(mem_size > 0);
	assert(mem_size > file_size);

	/*
	printf("section %08x-%08x-%08x prot %08x (%08x-%08x)\n",
		mem_addr, mem_addr+file_size, mem_addr+mem_size, mem_prot, page_start, page_end);
	*/

	result = vm_allocate(mach_task_self(), &area_start, page_end - page_start, TRUE);
	if (result) return (result);

	if (file_size > 0)
	{
		bcopy(st->image_addr + file_ofs, area_start + (mem_addr - page_start),
		      file_size);
	}

	if (mem_size > file_size)
	{
		bzero((void*)area_start + (mem_addr + file_size - page_start),
			mem_size - file_size);
	}

	result = vm_allocate(st->user_task, &page_start, page_end - page_start, FALSE);
	if (result) return (result);
	assert(page_start == trunc_page(mem_addr));

	result = vm_write(st->user_task, page_start, area_start, page_end - page_start);
	if (result) return (result);

	result = vm_deallocate(mach_task_self(), area_start, page_end - page_start);
	if (result) return (result);

	/*
	 * Protect the segment.
	 */
	if (load_protect_text && (mem_prot != VM_PROT_ALL)) {
		result = vm_protect(st->user_task, page_start, page_end - page_start,
				    FALSE, mem_prot);
		if (result) return (result);
	}

	return 0;
}

mach_port_t boot_script_read_file (const char *file)
{ return MACH_PORT_NULL; }	/* XXX */

int
boot_script_exec_cmd (void *hook,
		      task_t user_task,
		      char *file_name,
		      int arg_count, char **argv,
		      char *argstrings, int argslen)
{
  extern mach_port_t bootstrap_master_device_port, bootstrap_master_host_port;
  extern char *root_name;
  extern char **environ;
  int envc, env_len;

	int			arg_len = argslen;
	char *arg_pos;

	kern_return_t		result;
	thread_t		user_thread;
	struct file		file;
	char			namebuf[MAXPATHLEN+1];

	struct stuff		st;
	struct exec_info	info;

	extern char *	strbuild();

  	if (strcmp (file_name, "/dev/"))
	  (void) strbuild(namebuf, "/dev/", root_name, "/", file_name,
			  (char *)0);
	else
	  strcpy (namebuf, file_name);

	/*
	 * Open the file
	 */
	bzero((char *)&file, sizeof(file));

	result = open_file(bootstrap_master_device_port, namebuf, &file);
	if (result != 0) {
	  panic ("%s: %s", namebuf, strerror (result));
	}

	env_len = 0;
	for (envc = 0; environ[envc]; ++envc)
	  env_len += strlen (environ[envc]) + 1;

	/*
	 * Add space for:
	 *    arg_count
	 *    pointers to arguments
	 *    trailing 0 pointer
	 *    environment variables
	 *    trailing 0 pointer
	 *    and align to integer boundary
	 */
	arg_len += sizeof(integer_t) + (envc + 2 + arg_count) * sizeof(char *);
	arg_len += env_len;
	arg_len = (arg_len + (sizeof(integer_t) - 1)) & ~(sizeof(integer_t)-1);

	/*
	 * We refrain from checking IEXEC bits to make
	 * things a little easier when things went bad.
	 * Say you have ftp(1) but chmod(1) is gone.
	 */
	if (!file_is_regular(&file))
	  panic("boot_load_program: %s is not a regular file", namebuf);

	/*
	 * Load the executable file.
	 */
	st.fp = &file;
	st.user_task = user_task;
	st.aout_symtab_size = 0;
	st.aout_strtab_size = 0;
	result = exec_load(prog_read, prog_read_exec, &st, &info);
#ifdef GZIP
	if (result)
	{
		/*
		 * It might be gzip file.
		 */
		int err;
		extern int serverboot_gunzip(struct file *,
					     vm_offset_t *, size_t *);

		err = serverboot_gunzip(st.fp,
					&(st.image_addr),
					&(st.image_size));
		if (!err)
		{
			result = exec_load(image_read,
					   image_read_exec,
					   &st,
					   &info);
			vm_deallocate(mach_task_self(),
				      st.image_addr,
				      st.image_size);
		}
	}
#endif  /* GZIP */
#ifdef BZIP2
	if (result)
	{
		/*
		 * It might be bzip2 file.
		 */
		int err;
		extern int serverboot_bunzip2(struct file *,
					      vm_offset_t *, size_t *);

		err = serverboot_bunzip2(st.fp,
					 &(st.image_addr),
					 &(st.image_size));
		if (!err)
		{
			result = exec_load(image_read,
					   image_read_exec,
					   &st,
					   &info);
			vm_deallocate(mach_task_self(),
				      st.image_addr,
				      st.image_size);
		}
	}
#endif  /* BZIP2 */
	if (result)
	  panic ("cannot load %s: %s", namebuf, strerror (result));
#if 0
	printf("(serverboot): loaded %s; entrypoint %08x\n", namebuf, info.entry);
#endif

	/*
	 * Set up the stack and user registers.
	 */
	result = thread_create (user_task, &user_thread);
	if (result)
	  panic ("can't create user thread for %s: %s", namebuf,
		 strerror (result));
	arg_pos = set_regs(user_task, user_thread, &info, arg_len);

	/*
	 * Read symbols from the executable file.
	 */
#if 0
	printf("(serverboot): loading symbols from %s\n", namebuf);
	read_symtab_from_file(&file, bootstrap_master_host_port, user_task, namebuf, &st);
#endif

	/*
	 * Copy out the arguments.
	 */
	{
	    vm_offset_t	u_arg_start;
				/* user start of argument list block */
	    vm_offset_t	k_arg_start;
				/* kernel start of argument list block */
	    vm_offset_t u_arg_page_start;
				/* user start of args, page-aligned */
	    vm_size_t	arg_page_size;
				/* page_aligned size of args */
	    vm_offset_t	k_arg_page_start;
				/* kernel start of args, page-aligned */

	    register
	    char **	k_ap;	/* kernel arglist address */
	    char *	u_cp;	/* user argument string address */
	    register
	    char *	k_cp;	/* kernel argument string address */
	    register
	    int		i;

	    /*
	     * Get address of argument list in user space
	     */
	    u_arg_start = (vm_offset_t)arg_pos;

	    /*
	     * Round to page boundaries, and allocate kernel copy
	     */
	    u_arg_page_start = trunc_page(u_arg_start);
	    arg_page_size = (vm_size_t)(round_page(u_arg_start + arg_len)
					- u_arg_page_start);

	    result = vm_allocate(mach_task_self(),
				 &k_arg_page_start,
				 (vm_size_t)arg_page_size,
				 TRUE);
	    if (result)
		panic("boot_load_program: arg size");

	    /*
	     * Set up addresses corresponding to user pointers
	     * in the kernel block
	     */
	    k_arg_start = k_arg_page_start + (u_arg_start - u_arg_page_start);

	    k_ap = (char **)k_arg_start;

	    /*
	     * Start the strings after the arg-count and pointers
	     */
	    u_cp = (char *)u_arg_start + arg_count * sizeof(char *)
					+ envc * sizeof(char *)
					+ 2 * sizeof(char *)
					+ sizeof(integer_t);
	    k_cp = (char *)k_arg_start + arg_count * sizeof(char *)
					+ envc * sizeof(char *)
					+ 2 * sizeof(char *)
					+ sizeof(integer_t);

	    /*
	     * first the argument count
	     */
	    *k_ap++ = (char *)(intptr_t)arg_count;

	    /*
	     * Then the strings and string pointers for each argument
	     */
	    for (i = 0; i < arg_count; i++)
	      *k_ap++ = argv[i] - argstrings + u_cp;
	    *k_ap++ = (char *)0;
	    bcopy (argstrings, k_cp, argslen);
	    k_cp += argslen;
	    u_cp += argslen;

	    for (i = 0; i < envc; i++)
	      *k_ap++ = environ[i] - environ[0] + u_cp;
	    *k_ap   = (char *)0;
	    bcopy (environ[0], k_cp, env_len);

	    /*
	     * Now write all of this to user space.
	     */
	    (void) vm_write(user_task,
			    u_arg_page_start,
			    k_arg_page_start,
			    arg_page_size);

	    (void) vm_deallocate(mach_task_self(),
				 k_arg_page_start,
				 arg_page_size);
	}

	/*
	 * Close the file.
	 */
	close_file(&file);

  	/* Resume the thread.  */
  	thread_resume (user_thread);
  	mach_port_deallocate (mach_task_self (), user_thread);

	return (0);
}

/*
 * Load symbols from file into kernel debugger.
 */
static void read_symtab_from_file(
	struct file	*fp,
	mach_port_t	host_port,
	task_t		task,
	char *		symtab_name,
	struct stuff	*st)
{
	vm_size_t	resid;
	kern_return_t	result;
	vm_size_t	table_size;
	vm_offset_t	symtab;

#if 0

	if (!st->aout_symtab_size || !st->aout_strtab_size)
		return;

	/*
	 * Allocate space for the symbol table.
	 */
	table_size = sizeof(vm_size_t)
		     + st->aout_symtab_size
		     + st->aout_strtab_size;
	result= vm_allocate(mach_task_self(),
			    &symtab,
			    table_size,
			    TRUE);
	if (result) {
	    printf("[ error %d allocating space for %s symbol table ]\n",
		result, symtab_name);
	    return;
	}

	/*
	 * Set the symbol table length word,
	 * then read in the symbol table and string table.
	 */
	*(vm_size_t*)symtab = st->aout_symtab_size;
	result = read_file(fp, st->aout_symtab_ofs,
			   symtab + sizeof(vm_size_t),
			   st->aout_symtab_size + st->aout_strtab_size,
			   &resid);
	if (result || resid) {
	    printf("[ no valid symbol table present for %s ]\n",
		symtab_name);
	}
	else {
	    /*
	     * Load the symbols into the kernel.
	     */
	    result = host_load_symbol_table(
			host_port,
			task,
			symtab_name,
			symtab,
			table_size);
	}
	(void) vm_deallocate(mach_task_self(), symtab, table_size);
#endif
}
