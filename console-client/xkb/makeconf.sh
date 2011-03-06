#!/bin/sh
# Update the default configuration with default.xkb

# As input the file "default.xkb" is used.
# You can generate this file with "xkbcomp":
# "xkbcomp :0 -o default.xkb"
# This extract the configuration out of the ":0" X server.
# After that add the Hurd features to switch VCs, scroll, etc!!!!
#
# Hurd feature checklist:
# Alt + F1...F12: Switch VC.
# Alt + Left, Right: Switch to the console left or right of the current
# 		     console.
# Alt + Up, Down: Scroll the console by one line up or down.
# Ctrl + PgUp, PgDown: Scroll the console up or down with 1/2 screen.
# Alt + Home: Scroll the console to the 0% of the scrollback buffer.
# Ctrl + Home: Scroll the console to the 25% of the scrollback buffer.
# Alt + End: Scroll the console to the 100% of the scrollback buffer.
# Alt + End: Scroll the console to the 75% of the scrollback buffer.


# Really ugly code, but it works :)
echo "char *default_xkb_keymap = \"\\" > xkbdefaults.c
# Escape " and \
sed "s/\([\\\"]\)/\\\\\1/g" < default.xkb|awk ' { print $0 "\\n\\" } ' >> xkbdefaults.c
echo "\\0\";" >> xkbdefaults.c
