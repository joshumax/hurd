# Execute the user's rootrc file if it exists instead of this one.

case "$USER" in
  "" | root)
    UHOME="";;
  *)
    UHOME="`eval echo ~$USER`";;
esac

if [ "$UHOME" -a -r "$UHOME/.root_bashrc" ]; then
  . "$UHOME/.root_bashrc"
else
  # define some handy aliases
  alias j jobs -l
  alias c clear
  alias z suspend
  alias d dirs
fi
