# Execute the user's .root_profile file if it exists rather than this one.

case "$USER" in
  "" | root)
    UHOME="";;
  *)
    UHOME="`eval echo ~$USER`";;
esac

if [ "$UHOME" -a -r "$UHOME/.root_profile" ]; then
  . "$UHOME/.root_profile"
else
  PATH=/sbin:/bin:/local/bin
  export PATH
  echo "Don't login as root; use \`addauth root' or \`su'."
fi
