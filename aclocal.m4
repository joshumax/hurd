dnl These modifications are to allow for an empty cross compiler tree.
dnl In the situation that cross-linking is impossible, the variable
dnl `cross_linkable' will be substituted with "yes".

dnl
AC_DEFUN(hurd_PROG_CC,
[AC_BEFORE([$0], [AC_PROG_CPP])dnl
AC_CHECK_PROG(CC, gcc, gcc)
if test -z "$CC"; then
  AC_CHECK_PROG(CC, cc, cc, , , /usr/ucb/cc)
  test -z "$CC" && AC_MSG_ERROR([no acceptable cc found in \$PATH])
fi

hurd_PROG_CC_WORKS
AC_PROG_CC_GNU

if test $ac_cv_prog_gcc = yes; then
  GCC=yes
dnl Check whether -g works, even if CFLAGS is set, in case the package
dnl plays around with CFLAGS (such as to build both debugging and
dnl normal versions of a library), tasteless as that idea is.
  ac_test_CFLAGS="${CFLAGS+set}"
  ac_save_CFLAGS="$CFLAGS"
  CFLAGS=
dnl  AC_PROG_CC_G
  if test "$ac_test_CFLAGS" = set; then
    CFLAGS="$ac_save_CFLAGS"
dnl # This doesn't work on Linux (libc-4.5.26): Because of differences between
dnl # the shared and the static libraries there are less symbols available
dnl # without -g than with -g. It is therefore better to run the configuration
dnl # without -g and to add -g afterwards than the contrary. So don't add
dnl # -g to the CFLAGS now.
dnl  elif test $ac_cv_prog_cc_g = yes; then
dnl    CFLAGS="-g -O"
  else
dnl    CFLAGS="-O"
    # Add "-O" to both the CC and CPP commands, to eliminate possible confusion
    # that results from __OPTIMIZE__ being defined for CC but not CPP.
changequote(, )dnl
    if echo "$CC " | grep ' -O[1-9 ]' > /dev/null 2>&1; then
changequote([, ])dnl
      : # already optimizing
    else
      CC="$CC -O"
      ac_cv_prog_CC="$CC"
    fi
  fi
else
  GCC=
dnl # See above.
dnl   test "${CFLAGS+set}" = set || CFLAGS="-g"
fi
])

AC_DEFUN(hurd_PROG_CC_WORKS,
[AC_MSG_CHECKING([whether the C compiler ($CC $CFLAGS $LDFLAGS) works])
AC_LANG_SAVE
AC_LANG_C
AC_TRY_COMPILER([main(){return(0);}], ac_cv_prog_cc_works, ac_cv_prog_cc_cross)
AC_LANG_RESTORE
AC_MSG_RESULT($ac_cv_prog_cc_works)
if test $ac_cv_prog_cc_works = no; then
 cross_linkable=no
 ac_cv_prog_cc_cross=yes
 # AC_MSG_ERROR([installation or configuration problem: C compiler cannot create executables.])
else
 cross_linkable=yes
fi
AC_MSG_CHECKING([whether the C compiler ($CC $CFLAGS $LDFLAGS) is a cross-compiler])
AC_MSG_RESULT($ac_cv_prog_cc_cross)
AC_SUBST(cross_linkable)
cross_compiling=$ac_cv_prog_cc_cross
])
