# Execute the user's rootrc file if it exists instead of this one.

if [ $USER && $USER != root ]; then
  . ~$USER/.root_bashrc
else
# define some handy aliases
  alias j jobs -l
  alias c clear
  alias z suspend
  alias d dirs
fi
  