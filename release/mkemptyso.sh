#!/bin/sh
# args: $1 -- destination .so file
SO=$1
GCC=${GCC-i386-gnu-gcc}
$GCC -nostdlib -shared -fPIC -x c /dev/null -Wl,-soname=`basename $SO` -o $SO
