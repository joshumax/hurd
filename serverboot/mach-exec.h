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
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS 
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
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef	_MACH_EXEC_H_
#define	_MACH_EXEC_H_

#include <mach/machine/vm_types.h>
#include <mach/vm_prot.h>

/* XXX */
typedef enum
{
	EXEC_ELF	= 1,
	EXEC_AOUT	= 2,
} exec_format_t;

typedef struct exec_info
{
	/* Format of executable loaded - see above.  */
	exec_format_t format;

	/* Program entrypoint.  */
	vm_offset_t entry;

	/* Initial data pointer - only some architectures use this.  */
	vm_offset_t init_dp;

	/* (ELF) Address of interpreter string for loading shared libraries, null if none.  */
	vm_offset_t interp;

} exec_info_t;

typedef int exec_sectype_t;
#define EXEC_SECTYPE_READ		VM_PROT_READ
#define EXEC_SECTYPE_WRITE		VM_PROT_WRITE
#define EXEC_SECTYPE_EXECUTE		VM_PROT_EXECUTE
#define EXEC_SECTYPE_PROT_MASK		VM_PROT_ALL
#define EXEC_SECTYPE_ALLOC		((exec_sectype_t)0x000100)
#define EXEC_SECTYPE_LOAD		((exec_sectype_t)0x000200)
#define EXEC_SECTYPE_DEBUG		((exec_sectype_t)0x010000)
#define EXEC_SECTYPE_AOUT_SYMTAB	((exec_sectype_t)0x020000)
#define EXEC_SECTYPE_AOUT_STRTAB	((exec_sectype_t)0x040000)

typedef int exec_read_func_t(void *handle, vm_offset_t file_ofs,
			     void *buf, vm_size_t size,
			     vm_size_t *out_actual);

typedef int exec_read_exec_func_t(void *handle,
				  vm_offset_t file_ofs, vm_size_t file_size,
			          vm_offset_t mem_addr, vm_size_t mem_size,
				  exec_sectype_t section_type);

/*
 * Routines exported from libmach_exec.a
 */

/* Generic function to interpret an executable "file"
   and "load" it into "memory".
   Doesn't really know about files, loading, or memory;
   all file I/O and destination memory accesses
   go through provided functions.
   Thus, this is a very generic loading mechanism.

   The read() function is used to read metadata from the file
   into the local address space.

   The read_exec() function is used to load the actual sections.
   It is used for all kinds of sections - code, data, bss, debugging data.
   The 'section_type' parameter specifies what type of section is being loaded.

   For code, data, and bss, the EXEC_SECTYPE_ALLOC flag will be set.
   For code and data (i.e. stuff that's actually loaded from the file),
   EXEC_SECTYPE_LOAD will also be set.
   The EXEC_SECTYPE_PROT_MASK contains the intended access permissions
   for the section.
   'file_size' may be less than 'mem_size';
   the remaining data must be zero-filled.
   'mem_size' is always greater than zero, but 'file_size' may be zero
   (e.g. in the case of a bss section).
   No two read_exec() calls for one executable
   will load data into the same virtual memory page,
   although they may load from arbitrary (possibly overlapping) file positions.

   For sections that aren't normally loaded into the process image
   (e.g. debug sections), EXEC_SECTYPE_ALLOC isn't set,
   but some other appropriate flag is set to indicate the type of section.

   The 'handle' is an opaque pointer which is simply passed on
   to the read() and read_exec() functions.

   On return, the specified info structure is filled in
   with information about the loaded executable.
*/
int exec_load(exec_read_func_t *read, exec_read_exec_func_t *read_exec,
	      void *handle, exec_info_t *out_info);

/*
 * Error codes
 */

#define	EX_NOT_EXECUTABLE	6000	/* not a recognized executable format */
#define	EX_WRONG_ARCH		6001	/* valid executable, but wrong arch. */
#define EX_CORRUPT		6002	/* recognized executable, but mangled */
#define EX_BAD_LAYOUT		6003	/* something wrong with the memory or file image layout */


#endif	/* _MACH_EXEC_H_ */
