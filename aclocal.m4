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

AC_DEFUN([hurd_MIG_RETCODE], [dnl
# See if mig groks `retcode'.
AC_CACHE_CHECK(whether $MIG supports the retcode keyword, hurd_cv_mig_retcode,
[cat > conftest.defs <<\EOF
#include <mach/std_types.defs>
#include <mach/mach_types.defs>
subsystem foobar 1000;
type reply_port_t = polymorphic | MACH_MSG_TYPE_PORT_SEND_ONCE
	ctype: mach_port_t;
simpleroutine foobar_reply (
	reply_port: reply_port_t;
	err: kern_return_t, RetCode);
EOF
if AC_TRY_COMMAND([CC="${CC}" ${MIG-false} -n conftest.defs 1>&AC_FD_CC]); then
  hurd_cv_mig_retcode=yes
else
  hurd_cv_mig_retcode=no
fi
rm -f conftest*])
if test $hurd_cv_mig_retcode = yes; then
  AC_DEFINE(HAVE_MIG_RETCODE)
fi])

dnl The following check is based on a similar check in GNU inetutils 1.4.0.
dnl
dnl hurd_LIB_NCURSESW -- check for, and configure, ncursesw
dnl
dnl If libncursesw is found to exist on this system and the --disable-ncursesw
dnl flag wasn't specified, defines LIBNCURSESW with the appropriate linker
dnl specification, and possibly defines NCURSESW_INCLUDE with the appropriate
dnl -I flag to get access to ncursesw include files.
dnl
AC_DEFUN([hurd_LIB_NCURSESW], [
  AC_ARG_ENABLE(ncursesw,   [  --disable-ncursesw      Do not use ncursesw],
              , enable_ncursesw=yes)
  if test "$enable_ncursesw" = yes; then
    AC_CHECK_LIB(ncursesw, initscr, LIBNCURSESW="-lncursesw")
    if test "$LIBNCURSESW"; then
      AC_ARG_WITH(ncursesw-include-dir,
[  --with-ncursesw-include-dir=DIR
                          Set directory containing the include files for
                          use with -lncursesw, when it isn't installed as
                          the default curses library.  If DIR is "none",
                          then no special ncursesw include files are used.
  --without-ncursesw-include-dir
                          Equivalent to --with-ncursesw-include-dir=none])dnl
      if test "${with_ncursesw_include_dir+set}" = set; then
        AC_MSG_CHECKING(for ncursesw include dir)
        case "$with_ncursesw_include_dir" in
          no|none)
            hurd_cv_includedir_ncursesw=none;;
          *)
            hurd_cv_includedir_ncursesw="$with_ncursesw_include_dir";;
        esac
        AC_MSG_RESULT($hurd_cv_includedir_ncursesw)
      else
        AC_CACHE_CHECK(for ncursesw include dir,
                       hurd_cv_includedir_ncursesw,
          for D in $includedir $prefix/include /local/include /usr/local/include /include /usr/include; do
            if test -d $D/ncursesw; then
              hurd_cv_includedir_ncursesw="$D/ncursesw"
              break
            fi
            test "$hurd_cv_includedir_ncursesw" \
                  || hurd_cv_includedir_ncursesw=none
          done)
      fi
      if test "$hurd_cv_includedir_ncursesw" = none; then
        NCURSESW_INCLUDE=""
      else
        NCURSESW_INCLUDE="-I$hurd_cv_includedir_ncursesw"
      fi
    fi
  fi
  AC_SUBST(NCURSESW_INCLUDE)
  AC_SUBST(LIBNCURSESW)])dnl

