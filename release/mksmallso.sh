# Usage:
#  $1 : Destination merged, stripped, small shared library
#  $2 : lib*_pic.a files from which to produce the final small library
#  $3 : .so files that this library should depend on
#  ${4:$} : executables and shared libraries whos dependencies we care about

while :; do
  case "$1" in
    -*) LDARGS="$1"; shift;;
    *)  break;;
  esac
done

MERGED_SO="$1"; shift
PIC_LIBS="$1"; shift
DEPS="$1"; shift

GCC=${GCC-gcc}
LD=${LD-ld}
OBJDUMP=${OBJDUMP-objdump}
OBJCOPY=${OBJCOPY-objcopy}

DEP_FLAGS_FILE=/tmp/,depflags.$$
NEED_DSYMS_FILE=/tmp/,need.dyn.syms.$$
HAVE_DSYMS_FILE=/tmp/,have.dyn.syms.$$
MERGED_PIC_LIB=/tmp/,libmerged_pic.a.$$

#trap "rm -f $DEP_FLAGS_FILE $MERGED_PIC_LIB $NEED_DSYMS_FILE $HAVE_DSYMS_FILE" 0


$OBJDUMP --dynamic-syms "$@" 2>/dev/null \
  | sed -n 's/^.*\*UND\*.* \([^ ]*\)$/\1/p' \
  | sort -u > $NEED_DSYMS_FILE

#	       00000000  w    F .text	00000000 syscall_device_write_request
#              00000000 g     F .text	0000056c __strtoq_internal
$OBJDUMP --syms $PIC_LIBS 2>/dev/null \
  | sed -n 's/^........ \(g \| w\)   .. .*	[0-9a-f]....... \([^ ]*\)$/\2/p' \
  | sort -u > $HAVE_DSYMS_FILE

# This had better be gnu diff...
diff --unchanged-l='%L' --old-l= --new-l= $NEED_DSYMS_FILE $HAVE_DSYMS_FILE \
  | sed 's/^/-u/' > $DEP_FLAGS_FILE

$GCC $LDARGS -nostdlib -nostartfiles -shared -Wl,-soname=`basename $MERGED_SO` `cat $DEP_FLAGS_FILE` \
    -o $MERGED_SO.uns $PIC_LIBS $DEPS \
&& $OBJCOPY --strip-debug $MERGED_SO.uns $MERGED_SO \
&& rm -f $MERGED_SO.uns
