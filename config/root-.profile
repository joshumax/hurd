# Execute the user's .root_profile file if it exists rather than this one.

if [ $USER && $USER != root ]; then
  . ~USER/.root_profile
else
  export PATH=/sbin:/bin:/local/bin
fi
