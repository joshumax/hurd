#!/bin/bash -noprofile
#
# Traditional prompting login program
#
# This can be made the system default by making it the shell for the
# pseudo-user `login'.
#

prompt='
login: '
user=''

while [ ! "$user" ]; do
  echo -n "$prompt"
  read user args || exit 0
done

exec login "$@" --paranoid --retry="$0" "$user" $args
