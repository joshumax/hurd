#!/bin/sh
#  A unix-like su (one which invokes a sub-shell).
/bin/login --program-name="$0" -pzxSLf -aHOME -aMOTD -aUMASK -aBACKUP_SHELL "$@"
