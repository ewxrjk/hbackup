# Process this file with autoconf to produce a configure script.
#
# This file is part of hbackup
# Copyright (C) 2009 Richard Kettlewell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
AC_INIT([hbackup], [0.DEV], [richard+vcs@sfere.greenend.org.uk])
AC_CONFIG_AUX_DIR([config.aux])
AM_INIT_AUTOMAKE([hbackup], [0.DEV])
AC_CONFIG_SRCDIR([nhbackup.cc])
AM_CONFIG_HEADER([config.h])
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
AC_SET_MAKE
AC_PATH_PROG([FINK],[fink],[none],[$PATH:/sw/bin])
if test "x$FINK" != xnone; then
  AC_CACHE_CHECK([fink install directory],[rjk_cv_finkprefix],[
    rjk_cv_finkprefix="`echo "$FINK" | sed 's,/bin/fink$,,'`"
  ])
  CPPFLAGS="${CPPFLAGS} -I ${rjk_cv_finkprefix}/include"
  LDFLAGS="${LDFLAGS} -L${rjk_cv_finkprefix}/lib"
fi
AC_CHECK_LIB(pcre, pcre_compile,
	     [AC_SUBST(LIBPCRE,[-lpcre])],
	     [missing_libraries="$missing_libraries libpcre"])
AC_CHECK_FUNC(iconv_open, [:],
              [RJK_CHECK_LIB(iconv, iconv_open, [#include <iconv.h>],
                            [AC_SUBST(LIBICONV,[-liconv])],
                            [missing_functions="$missing_functions iconv_open"])])
if test ! -z "$missing_libraries"; then
  AC_MSG_ERROR([missing libraries:$missing_libraries])
fi
AC_DEFINE([_GNU_SOURCE], [1], [use GNU extensions if available])
AC_DEFINE([_FILE_OFFSET_BITS], [64], [use 64-bit off_t])

if test "$GXX" = yes; then
  CXX="${CXX} -Wall -W -Wmissing-declarations -Werror"
fi

if test "$GCC" = yes; then
  CC="${CC} -Wall -W -Wmissing-declarations -Werror"
fi

AH_BOTTOM([#ifdef __GNUC__
# define attribute(x) __attribute__(x)
#else
# define attribute(x)
#endif])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

# Local Variables:
# indent-tabs-mode:nil
# End:

