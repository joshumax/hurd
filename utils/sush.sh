#!/bin/sh
#  A unix-like su (one which invokes a sub-shell).
exec /bin/login --program-name="$0" -pxSLf -aHOME -aMOTD -aUMASK -aBACKUP_SHELLS "$@"
