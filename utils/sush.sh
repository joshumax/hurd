#!/bin/sh
#  A unix-like su (one which invokes a sub-shell).
/bin/login --program-name="$0" -pzxSLfk -aHOME -aMOTD -aUMASK -aBACKUP_SHELL "$@"