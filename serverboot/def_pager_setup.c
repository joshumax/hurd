/*
 * Mach Operating System
 * Copyright (c) 1992-1989 Carnegie Mellon University
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
#include <mach.h>

#include <file_io.h>

extern void *kalloc();

/*
 * Create a paging partition given a file name
 */
extern void	create_paging_partition();

kern_return_t
add_paging_file(master_device_port, file_name)
	mach_port_t		master_device_port;
	char			*file_name;
{
	register struct file_direct *fdp;
	register kern_return_t	result;
	struct file     	pfile;
	boolean_t		isa_file;

	bzero((char *) &pfile, sizeof(struct file));

	result = open_file(master_device_port, file_name, &pfile);
	if (result != KERN_SUCCESS)
		return result;

	fdp = (struct file_direct *) kalloc(sizeof *fdp);
	bzero((char *) fdp, sizeof *fdp);

	isa_file = file_is_structured(&pfile);

	result = open_file_direct(pfile.f_dev, fdp, isa_file);
	if (result)
		panic("Can't open paging file %s\n", file_name);

	result = add_file_direct(fdp, &pfile);
	if (result)
		panic("Can't read disk addresses: %d\n", result);

	close_file(&pfile);

	/*
	 * Set up the default paging partition
	 */
	create_paging_partition(file_name, fdp, isa_file);

	return result;
}

/*
 * Destroy a paging_partition given a file name
 */
kern_return_t
remove_paging_file(file_name)
	char			*file_name;
{
	struct file_direct	*fdp = 0;
	kern_return_t		kr;

	kr = destroy_paging_partition(file_name, &fdp);
	if (kr == KERN_SUCCESS) {
		remove_file_direct(fdp);
		kfree(fdp, sizeof(*fdp));
	}
	return kr;
}

/*
 * Set up default pager
 */
extern char *strbuild();

boolean_t
default_pager_setup(master_device_port, server_dir_name)
	mach_port_t master_device_port;
	char	*server_dir_name;
{
	register kern_return_t	result;

	char	paging_file_name[MAXPATHLEN+1];

	(void) strbuild(paging_file_name,
			server_dir_name,
			"/paging_file",
			(char *)0);

	while (TRUE) {
	    result = add_paging_file(master_device_port, paging_file_name);
	    if (result == KERN_SUCCESS)
		break;
	    printf("Can't open paging file %s: %d\n",
		   paging_file_name,
		   result);

	    bzero(paging_file_name, sizeof(paging_file_name));
	    printf("Paging file name ? ");
	    safe_gets(paging_file_name, sizeof(paging_file_name));

	    if (paging_file_name[0] == 0) {
		printf("*** WARNING: running without paging area!\n");
		return FALSE;
	    }
	}

	/*
	 * Our caller will become the default pager - later
	 */

	return TRUE;
}
