dnl Macros used to build the Wine configure script
dnl
dnl Copyright 2002 Alexandre Julliard
dnl
dnl This library is free software; you can redistribute it and/or
dnl modify it under the terms of the GNU Lesser General Public
dnl License as published by the Free Software Foundation; either
dnl version 2.1 of the License, or (at your option) any later version.
dnl
dnl This library is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl Lesser General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public
dnl License along with this library; if not, write to the Free Software
dnl Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
dnl

dnl **** Get the ldd program name; used by WINE_GET_SONAME ****
dnl
dnl Usage: WINE_PATH_LDD
dnl
AC_DEFUN([WINE_PATH_LDD],[AC_PATH_PROG(LDD,ldd,true,/sbin:/usr/sbin:$PATH)])

dnl **** Extract the soname of a library ****
dnl
dnl Usage: WINE_GET_SONAME(LIBRARY, FUNCTION, [OTHER_LIBRARIES])
dnl
AC_DEFUN([WINE_GET_SONAME],
[AC_REQUIRE([WINE_PATH_LDD])
AC_CACHE_CHECK([for -l$1 soname], ac_cv_lib_soname_$1,
[ac_get_soname_save_LIBS=$LIBS
LIBS="-l$1 $3 $LIBS"
  AC_LINK_IFELSE([AC_LANG_CALL([], [$2])],
  [ac_cv_lib_soname_$1=`$ac_cv_path_LDD conftest$ac_exeext | grep lib$1\\.so | sed 's/^[[ 	]]*\([[^ 	]]*\)[[ 	]]*=>.*$/\1/'`
  if test "x$ac_cv_lib_soname_$1" = "x"
  then
     ac_cv_lib_soname_$1="lib$1.so"
  fi],
  [ac_cv_lib_soname_$1="lib$1.so"])
  LIBS=$ac_get_soname_save_LIBS])
if test "x$ac_cv_lib_soname_$1" != xNONE
then AC_DEFINE_UNQUOTED(AS_TR_CPP(SONAME_LIB$1),"$ac_cv_lib_soname_$1",
                        [Define to the soname of the lib$1 library.])dnl
fi])

dnl **** Check if a structure contains a specified member ****
dnl
dnl Usage: WINE_CHECK_STRUCT_MEMBER(struct,member,[includes,[action-if-found,[action-if-not-found]]])
dnl
AC_DEFUN([WINE_CHECK_STRUCT_MEMBER],
[AC_CACHE_CHECK([for $2 in struct $1], ac_cv_c_$1_$2,
 AC_TRY_COMPILE([$3],[struct $1 s; s.$2 = 0],ac_cv_c_$1_$2="yes",ac_cv_c_$1_$2="no"))
AS_IF([ test "x$ac_cv_c_$1_$2" = "xyes"],[$4],[$5])
])

dnl **** Check for reentrant libc ****
dnl
dnl Usage: WINE_CHECK_ERRNO(errno-name,[action-if-yes,[action-if-no]])
dnl
dnl For cross-compiling we blindly assume that libc is reentrant. This is
dnl ok since non-reentrant libc is quite rare (mostly old libc5 versions).
dnl
AC_DEFUN([WINE_CHECK_ERRNO],
[AC_CACHE_CHECK([for reentrant libc: $1],[wine_cv_libc_r_$1],
  [AC_TRY_RUN([int myerrno = 0;
char buf[256];
int *$1(){return &myerrno;}
main(){connect(0,buf,255); exit(!myerrno);}],
  wine_cv_libc_r_$1=yes, wine_cv_libc_r_$1=no,
  wine_cv_libc_r_$1=yes)])
AS_IF([test "$wine_cv_libc_r_$1" = "yes"],[$2],[$3])])

dnl **** Link C code with an assembly file ****
dnl
dnl Usage: WINE_TRY_ASM_LINK(asm-code,includes,function,[action-if-found,[action-if-not-found]])
dnl
AC_DEFUN([WINE_TRY_ASM_LINK],
[ac_try_asm_link_saved_libs=$LIBS
LIBS="conftest_asm.s $LIBS"
cat > conftest_asm.s <<EOF
$1
EOF
AC_TRY_LINK([$2],[$3],[$4],[$5])
rm -f conftest_asm.s
LIBS=$ac_try_asm_link_saved_libs])

dnl **** Check if we can link an empty program with special CFLAGS ****
dnl
dnl Usage: WINE_TRY_CFLAGS(flags,[action-if-yes,[action-if-no]])
dnl
AC_DEFUN([WINE_TRY_CFLAGS],
[ac_wine_try_cflags_saved=$CFLAGS
CFLAGS="$CFLAGS $1"
AC_TRY_LINK([],[],[$2],[$3])
CFLAGS=$ac_wine_try_cflags_saved])

dnl **** Check for ln ****
dnl
dnl Usage: WINE_PROG_LN
dnl
AC_DEFUN([WINE_PROG_LN],
[AC_MSG_CHECKING([whether ln works])
rm -f conf$$ conf$$.file
echo >conf$$.file
if ln conf$$.file conf$$ 2>/dev/null; then
  AC_SUBST(LN,ln)
  AC_MSG_RESULT([yes])
else
  AC_SUBST(LN,["cp -p"])
  AC_MSG_RESULT([no, using $LN])
fi
rm -f conf$$ conf$$.file])

dnl **** Create non-existent directories from config.status ****
dnl
dnl Usage: WINE_CONFIG_EXTRA_DIR(dirname)
dnl
AC_DEFUN([WINE_CONFIG_EXTRA_DIR],
[AC_CONFIG_COMMANDS([$1],[test -d "$1" || (AC_MSG_NOTICE([creating $1]) && mkdir "$1")])])
