OUTPUT_FORMAT("elf32-i386", "elf32-i386",
	      "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
 SEARCH_DIR(/usr/local/i386-gnuelf/lib);
/* Do we need any of these for elf?
   __DYNAMIC = 0;    */
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  . = 0x10020;
  .text      :
  {
    *(.text)
  *(.interp)
  *(.hash)
  *(.dynsym)
  *(.dynstr)
  *(.rel.text)
  *(.rela.text)
  *(.rel.data)
  *(.rela.data)
  *(.rel.rodata)
  *(.rela.rodata)
  *(.rel.got)
  *(.rela.got)
  *(.rel.ctors)
  *(.rela.ctors)
  *(.rel.dtors)
  *(.rela.dtors)
  *(.rel.init)
  *(.rela.init)
  *(.rel.fini)
  *(.rela.fini)
  *(.rel.bss)
  *(.rela.bss)
  *(.rel.plt)
  *(.rela.plt)
  *(.init)
  *(.plt)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
  *(.fini)
  *(.rodata)
  *(.rodata1)
*(_hurd_ioctl_handler_lists)
*(_hurd_pgrp_changed_hook)
*(_hurd_fork_locks)
*(_hurd_subinit)
*(__libc_atexit)
*(_hurd_fd_subinit)
*(_hurd_preinit_hook)
*(_hurd_fork_child_hook)
*(_hurd_fork_parent_hook)
*(_hurd_fork_prepare_hook)
*(_hurd_reauth_hook)
*(_hurd_proc_subinit)
*(__libc_subinit)
  _etext = .;
  PROVIDE (etext = .);
  . = ALIGN(0x1000);
  } =0x9090
  . =  ALIGN(0x1000);
  .data    :
  {
    *(.data)
    CONSTRUCTORS

  *(.data1) 
  *(.ctors)
  *(.dtors)
  *(.got.plt) *(.got) 
  *(.dynamic)
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  *(.sdata)
  _edata  =  .;
  PROVIDE (edata = .);
  . = ALIGN(0x10);
}
  __bss_start = .;
  .bss       :
  {
 *(.sbss) *(.scommon) 
   *(.dynbss)
   *(.bss)
   *(COMMON)
  _end = ALIGN(4) ;
  PROVIDE (end = ALIGN(4));
  }
  /* These are needed for ELF backends which have not yet been
     converted to the new style linker.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  /* DWARF debug sections.
     Symbols in the .debug DWARF section are relative to the beginning of the
     section so we begin .debug at 0.  It's not clear yet what needs to happen
     for the others.   */
  .debug          0 : { *(.debug) }
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  .line           0 : { *(.line) }
  /* These must appear regardless of  .  */
}
