PS1='login> '
test "$_LOGIN_RETRY" || echo "Use \`login USER' to login, or \`help' for more information."
unset _LOGIN_RETRY
