/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

extern void __fput(struct file *);

/*
 * Check whether the specified task has the fd open. Since the task
 * may not have a files_struct, we must test for p->files != NULL.
 */
extern inline struct file * fcheck_task(struct task_struct *p, unsigned int fd)
{
	struct file * file = NULL;

	if (p->files && fd < p->files->max_fds)
		file = p->files->fd[fd];
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
extern inline struct file * fcheck(unsigned int fd)
{
	struct file * file = NULL;

	if (fd < current->files->max_fds)
		file = current->files->fd[fd];
	return file;
}

extern inline struct file * fget(unsigned int fd)
{
	struct file * file = fcheck(fd);

	if (file)
		file->f_count++;
	return file;
}

/*
 * Install a file pointer in the fd array.
 */
extern inline void fd_install(unsigned int fd, struct file *file)
{
	current->files->fd[fd] = file;
}

/*
 * 23/12/1998 Marcin Dalecki <dalecki@cs.net.pl>: 
 * 
 * Since those functions where calling other functions, it was compleatly 
 * bogous to make them all "extern inline".
 *
 * The removal of this pseudo optimization saved me scandaleous:
 *
 * 		3756 (i386 arch) 
 *
 * precious bytes from my kernel, even without counting all the code compiled
 * as module!
 *
 * I suspect there are many other similar "optimizations" across the
 * kernel...
 */
extern void fput(struct file *file); 
extern void put_filp(struct file *file);

#endif
