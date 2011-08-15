#!/bin/sh
# Make a filesystem image
#
# Copyright (C) 1997 Free Software Foundation, Inc.
# Written by Miles Bader <miles@gnu.ai.mit.edu>
# This file is part of the GNU Hurd.
#
# The GNU Hurd is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2, or (at
# your option) any later version.
#
# The GNU Hurd is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
#  

USAGE="\
Usage: $0 [OPTION...] IMAGE-FILE SRC..."
TRY="Try "\`"$0 --help' for more information"

MAX_SIZE=1440	# size of floppy
MIN_SIZE=500    # avoid lossage for compressed filesystems

OWNER="`id -un`.`id -gn`"

unset IMAGE SRCS COMPRESS SCRIPTS QUIET GEN_DEPS
declare -a SRCS SCRIPTS

NUM_SRCS=0
NUM_SCRIPTS=0

while :; do
  case "$1" in
    --compress)     COMPRESS=yes; shift 1;;
    --fstype=*)     FSTYPE="`echo "$1" | sed 's/^--fstype=//'`"; shift 1;;
    --fstype)       FSTYPE="$2"; shift 2;;
    --mkfs=*)       MKFS="`echo "$1" | sed 's/^--mkfs=//'`"; shift 1;;
    --mkfs)         MKFS="$2"; shift 2;;
    --fstrans=*)    FSTRANS="`echo "$1" | sed 's/^--fstrans=//'`"; shift 1;;
    --fstrans)      FSTRANS="$2"; shift 2;;
    --owner=*)      OWNER="`echo "$1" | sed 's/^--owner=//'`"; shift 1;;
    --owner)        OWNER="$2"; shift 2;;
    --max-size=*)   MAX_SIZE="`echo "$1" | sed 's/^--max-size=//'`"; shift 1;;
    --max-size)     MAX_SIZE="$2"; shift 2;;
    --quiet|-q|-s)  QUIET=yes; shift 1;;
    --copy=*|--copy-rules=*)      SCRIPTS[NUM_SCRIPTS]="`echo "$1" | sed 's/^--[-a-z]*=//'`"; let NUM_SCRIPTS+=1; shift 1;;
    --copy|--copy-rules)        SCRIPTS[NUM_SCRIPTS]="$2"; let NUM_SCRIPTS+=1; shift 2;;
    --dependencies=*) GEN_DEPS="`echo "$1" | sed 's/^--[-a-z]*=//'`"; shift 1;;
    --dependencies)   GEN_DEPS="$2"; shift 2;;

    --version)
      echo "STANDARD_HURD_VERSION_mkfsimage_"; exit 0;;
    --help)
      echo "$USAGE"
      echo "Make a file-system image IMAGE-FILE from the files in SRC..."
      echo ''
      echo "\
      --copy-rules=FILE      Copy files in a manner described by FILE

      --compress             Compress the final image
      --owner=USER[.GROUP]   Make files owned by USER & GROUP (default "\`"$OWNER')
      --max-size=KBYTES      Maximum size of final image (default $MAX_SIZE)
      --dependencies=DEPS    Generate a make dependency rule into DEPS and exit

      --fstype=TYPE          Type of filesystem (TYPE may be "\`"ext2' or "\`"ufs')
      --mkfs=PROGRAM         Program to make an empty filesystem image
      --fstrans=PROGRAM      File system translator program

      --help                 Display this help and exit
      --version              Output version information and exit

If multiple SRCs are specified, then each occurrence of --files pertains only to
the corresponding SRC.

Each FILE named in a --copy-rules option contains lines of the form:

  [gzip] [rename TARGET] COPY-OP NAME

and says to copy NAME from the source tree to the destination, using the
method specified by COPY-OP.  A preceding "\`"rename TARGET"\'" says to give
NAME a different name in the target tree, and a preceding "\`"gzip"\'" says
to compress the result (appending .gz to the name).  COPY-OP may be one of the
following:

  copy	   -- A plain copy, preserving symlinks
  objcopy  -- Copy using objcopy to strip any unneeded symbols
  copytrans -- Copy a translator

  touch    -- Create an empty file in the destination, ignoring the source
  mkdir    -- Create an empty directory in the destination, ignoring the source
  makedev  -- Create the given device in the destination, ignoring the source
  settrans -- Set a translator with the given arguments

If both --mkfs and --fstrans are specified, no filesystem type need be given.
If --fstype is not specified, an attempt is made to guess it based on the
extension of IMAGE-FILE."
      exit 0;;
    -*)
      echo 1>&2 "$0: $1: Unknown option"
      echo 1>&2 "$TRY"
      exit 64;;
    '')
      break;;
    *)
      case "${IMAGE+set}" in
        set) SRCS[NUM_SRCS]="$1"; let 'NUM_SRCS += 1';;
	*)   IMAGE="$1";;
      esac
      shift
  esac
done

case "${IMAGE+set}${SRCS[*]+set}" in
  setset) ;;
  *)
    echo 1>&2 "$USAGE"
    echo 1>&2 "$TRY"
    exit 64;
esac

# Choose format
if [ "${MKFS+set}" != set -o "${MKFS+set}" != set ]; then
  if [ "${FSTYPE+set}" != set ]; then
    case "$IMAGE" in
      *.ext2|*.ext2.gz) FSTYPE=ext2;;
      *.ufs|*.ext2.gz)  FSTYPE=ufs;;
      *)                echo 1>&2 "$0: $IMAGE: Unknown filesystem type"; exit 1;;
    esac
  fi

  case "$FSTYPE" in
    ext2)  MKFS="/sbin/mkfs.ext2 -ohurd"; MKFS_Q="-q"; FSTRANS=/hurd/ext2fs;;
    ufs)   MKFS="/sbin/mkfs.ufs --tracks=1 --sectors=80"; FSTRANS=/hurd/ufs;;
    *)     echo 1>&2 "$0: $IMAGE: Unknown filesystem type"; exit 1;;
  esac
fi

case "$QUIET" in
  yes)  MKFS_Q="${MKFS_Q} >/dev/null"; ECHO=:;;
  *)    MKFS_Q=''; ECHO=echo;;
esac
export ECHO MKFS_Q

case "$IMAGE" in *.gz) COMPRESS=yes;; esac

IMAGE_TMP="${IMAGE}.new"
IMAGE_GZIP_TMP="${IMAGE}.new.gz"
MNT="/tmp/,mkfsimage-$$.mnt"
ERROUT="/tmp/,mkfsimage-$$.errout"
STAGE="/tmp/,mkfsimage-$$.stage"
TRANS_LIST="/tmp/,mkfsimage-$$.trans"

# Extra blocks that will be used by translators
TRANS_BLOCKS=0

if [ "$GEN_DEPS" ]; then
  GEN_DEPS_TMP="$GEN_DEPS.new"
  echo "$GEN_DEPS: ${SCRIPTS[*]}" >> "$GEN_DEPS_TMP"
  echo "$IMAGE: \\" >> "$GEN_DEPS_TMP"
fi

trap "settrans 2>/dev/null -a $MNT; rm -rf $MNT $IMAGE_TMP $IMAGE_GZIP_TMP $ERROUT $STAGE $TRANS_LIST $GEN_DEPS_TMP" 0 1 2 3 15

if [ ${#SRCS[@]} = 1 -a ${#SCRIPTS[@]} = 0 ]; then
  # No staging directory
  TREE="$1"
else
  # Multiple source trees, or selective copying -- copy using a staging directory.
  # We record any translators in a file ($TRANS_LIST) for later, since we copy
  # the staging dir to the final dir using tar, and it can't handle translators.

  mkdir $STAGE || exit $?

  SRC_NUM=0
  while [ $SRC_NUM -lt ${#SRCS[@]} ]; do
    SRC="${SRCS[SRC_NUM]}"
    SCRIPT="${SCRIPTS[SRC_NUM]}"
    let SRC_NUM+=1

    if [ ! -d "$SRC" ]; then
      echo 1>&2 "$0: $SRC: No such directory"
      exit 24
    fi
    case "$SRC" in
      /)  PFX="/";;
      *)  PFX="$SRC/";;
    esac

    if [ ! "$GEN_DEPS" ]; then
      eval $ECHO "'# Copying files from $SRC into staging directory $STAGE...'"
    fi

    if [ x"${SCRIPT}" != x ]; then
      eval $ECHO "'# Using copy script $SCRIPT'"
      (
	if [ x"$SCRIPT" != x- ]; then
	  if [ ! -r "$SCRIPT" ]; then
	    echo 1>&2 "$0: $SCRIPT: No such file"
	    exit 25
	  fi
	  exec <"$SCRIPT"
	fi

        test "$GEN_DEPS" && echo "  $SCRIPT \\" >> "$GEN_DEPS_TMP"

	while read -a args; do
	  case $args in
	    gzip) gzip=yes; unset args[0]; args=(${args[@]});;
	    *)    gzip=no;;
	  esac
	  case $args in
	    rename) dst="${args[1]}"; unset args[0] args[1]; args=(${args[@]});;
	    *)      unset dst;;
	  esac

	  op="${args[0]}"
	  src="${args[1]}"

	  if echo "$src" | grep -q "[?*[]"; then
	    # For wildcards, use the most recent file matching that pattern
	    src="`(cd $SRC; ls -t1 $src | head  -1)`"
	  fi

	  case ${dst+set} in
	    set)
	      # Destination patterns match files in the source tree (What
	      # else could we use?  This may be useful for files that exist
	      # in the source, but which we want to use a different version
	      # of for this filesystem).
	      if echo "$dst" | grep -q "[?*[]"; then
	        dst="`(cd $SRC; ls -t1 $dst | head  -1)`"
	      fi;;
	    *)
	      dst="$src";;
	  esac

	  # Pop op & src off of args
	  set -- "${args[@]}"; shift 2; unset args; args=("$@")

	  if [ "$GEN_DEPS" ]; then
	    case $op in
	      copy|objcopy)
	        echo "  ${PFX}$src \\" >> "$GEN_DEPS_TMP";;
            esac
            continue
          fi

	  case $op in
	    copy)
	      eval $ECHO "'cp -d ${PFX}$src $STAGE/$dst'"
	      cp -d ${PFX}$src $STAGE/$dst
	      ;;
	    objcopy)
	      eval $ECHO "'objcopy --strip-unneeded ${PFX}$src $STAGE/$dst'"
	      objcopy --strip-unneeded "${PFX}$src" "$STAGE/$dst"
	      ;;
	    symlink)
	      if echo "$args" | grep -q "[?*[]"; then
	        # symlink expansion is in the source tree, in the same
		# directory as the file itself.
	        args="`(cd $PFX`dirname $dst`; ls -t1 $args | head  -1)`"
	      fi	      
	      eval $ECHO "'ln -s $args $STAGE/$dst'"
	      ln -s $args $STAGE/$dst
	      ;;
	    mkdir)
	      eval $ECHO "'mkdir $STAGE/$dst'"
	      mkdir "$STAGE/$dst"
	      ;;
	    touch)
	      eval $ECHO "'touch $STAGE/$dst'"
	      touch "$STAGE/$dst"
	      ;;

	    makedev|settrans|copytrans)
	      # delay translators until later, as tar can't copy them.

	      case $op in
	        settrans|copytrans)
		  # We create the node on which translators will be put so
		  # that the owner gets set correctly; this isn't necessary for
		  # device because MAKEDEV does all the work needed, and doing so
		  # would cause problems with device names that are really
		  # categories.
	          touch "$STAGE/$dst";;
	      esac

	      # Accunt for space used by the translator block
	      TRANS_BLOCKS=$(($TRANS_BLOCKS + 1))

	      # Record the desired operation for a later pass
	      echo "$op $dst $src ${args[*]}" >> $TRANS_LIST
	      ;;

	    ''|'#')
	      ;;
	    *)
	      echo 1>&2 "$0: $op: Unknown operation"
	      ;;
	  esac

	  case $gzip in yes)
	    eval $ECHO "'gzip -v9 $STAGE/$dst'"
	    gzip -v9 "$STAGE/$dst";;
	  esac
	done
      ) || exit $?
    else
      eval $ECHO "'# Copying all files using tar'"
      (cd $SRC; tar cf - .) | (cd $STAGE; tar -x --same-owner -p -f -)
    fi
  done
  TREE="$STAGE"
fi

if [ "$GEN_DEPS" ]; then
  echo "" >> "$GEN_DEPS_TMP" && mv "$GEN_DEPS_TMP" "$GEN_DEPS"
  exit 0
fi

eval $ECHO "'# Changing file owners to $OWNER'"
chown -R "$OWNER" $TREE

# Size of source tree, plus 5% for overhead
TREE_SIZE=$((($TRANS_BLOCKS + `du -s "$TREE" | sed 's/^\([0-9]*\).*/\1/'`) * 105 / 100))

if [ "${COMPRESS-no}" = yes ]; then
  # Add 10% to the filesystem size to leave some breathing room.  
  # Since unused filesystem space compresses very well, this shouldn't add
  # much to the final size.
  SIZE=$(($TREE_SIZE * 110 / 100))
  test $SIZE -lt $MIN_SIZE && SIZE=$MIN_SIZE
else
  if [ $TREE_SIZE -gt $MAX_SIZE ]; then
    echo 1>&2 "$0: $TREE: Too big (${TREE_SIZE}k) to fit in ${MAX_SIZE}k"
    exit 10
  fi
  SIZE=$(($TREE_SIZE * 110 / 100))
  test $SIZE -lt $MIN_SIZE && SIZE=$MIN_SIZE
  test $SIZE -gt $MAX_SIZE && SIZE=$MAX_SIZE
fi

eval $ECHO "'# Zeroing disk image...'"
rm -f $IMAGE_TMP
if ! dd if=/dev/zero of=$IMAGE_TMP bs=${SIZE}k count=1 2>$ERROUT; then
  sed -n "s@^dd:@$0@p" < $ERROUT 1>&2
  exit 11
fi

eval $ECHO "'# Making filesystem...'"
eval "$MKFS $MKFS_Q '$IMAGE_TMP'" || exit 12
settrans -ac $MNT $FSTRANS $IMAGE_TMP || exit 13

eval $ECHO "'# Copying $TREE into filesystem...'"
(cd $TREE; tar cf - .) | (cd $MNT; tar -x --same-owner -p -f -)

if [ -r "$TRANS_LIST" ]; then
  # create any delayed translators
  eval $ECHO "'# Creating translators...'"
  cat "$TRANS_LIST" |
    while read -a args; do
      op="${args[0]}"
      dst="${args[1]}"
      src="${args[2]}"
      set -- "${args[@]}"; shift 3; unset args; args=("$@")
      
      case $op in
	copytrans)
	  tr="`showtrans "${PFX}$src"`"
	  eval $ECHO "'settrans $MNT/$dst $tr'"
	  settrans "$MNT/$dst" $tr
	  ;;
	settrans)
	  eval $ECHO "'settrans $MNT/$dst ${args[*]}'"
	  settrans "$MNT/$dst" "${args[@]}"
	  ;;
	makedev)
	  dd="/`dirname $dst`"
	  eval $ECHO "'/sbin/MAKEDEV --devdir=$dd $MNT/$dst'"
	  /sbin/MAKEDEV --devdir=$dd "$MNT/$dst"
	  ;;
      esac
    done
fi
  
settrans -a $MNT

case "$COMPRESS" in
  yes)
    case "$QUIET" in
      yes)
        gzip -9 $IMAGE_TMP
	;;
      *)
	eval $ECHO "'# Compressing disk image...'"
        gzip -v9 $IMAGE_TMP 2>&1 | sed "s@$IMAGE_TMP\.gz@$IMAGE@g"
	;;
    esac
    mv $IMAGE_GZIP_TMP $IMAGE
    ;;
  *)
    mv $IMAGE_TMP $IMAGE
    ;;
esac

exit 0
