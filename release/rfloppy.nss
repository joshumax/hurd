# /etc/nsswitch.conf
#
# Don't use name services that we can't provide (specifically, nis & db)
#

# defaults for hosts & networks are ok

passwd:		files
group:		files
shadow:		files
aliases:	files

protocols:	files
services:	files
ethers:		files
rpc:		files
publickey:	files

netgroup:	files
