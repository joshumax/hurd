#ifndef _WRAPPER_H_
#define _WRAPPER_H_
#define wait_handle struct wait_queue
#define file_handle struct file
#define inode_handle struct inode
#define select_table_handle select_table
#define vm_area_handle struct vm_area_struct
#define file_operation_handle file_operations

#define connect_wrapper(x) 0
#define current_got_fatal_signal() (signal_pending(current))
#define current_set_timeout(val) current->timeout = val

#define module_interruptible_sleep_on interruptible_sleep_on
#define module_wake_up wake_up
#define module_select_wait select_wait
#define module_register_chrdev register_chrdev
#define module_unregister_chrdev unregister_chrdev
#define module_register_blkdev register_blkdev
#define module_unregister_blkdev unregister_blkdev

#define inode_get_rdev(i) i->i_rdev
#define inode_get_count(i) i->i_count
#define inode_inc_count(i) i->i_count++
#define inode_dec_count(i) i->i_count--

#define file_get_flags(f) f->f_flags

#define vma_set_inode(v,i) v->vm_inode = i
#define vma_get_flags(v) v->vm_flags
#define vma_get_offset(v) v->vm_offset
#define vma_get_start(v) v->vm_start
#define vma_get_end(v) v->vm_end
#define vma_get_page_prot(v) v->vm_page_prot

#define mem_map_reserve(p) set_bit(PG_reserved, &mem_map[p].flags)
#define mem_map_unreserve(p) clear_bit(PG_reserved, &mem_map[p].flags)
#define mem_map_inc_count(p) atomic_inc(&(mem_map[p].count))
#define mem_map_dec_count(p) atomic_dec(&(mem_map[p].count))
#endif
