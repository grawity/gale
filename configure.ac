dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.13)
AC_INIT(gale,1.1happy)
AC_CONFIG_SRCDIR(INSTALL)
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS(include/gale/config.h:config.h.in)
AC_CANONICAL_HOST
AC_PROG_CC
AM_PROG_LIBTOOL
AM_MAINTAINER_MODE

AC_ARG_ENABLE(adns,
[  --enable-adns           use asynchronous DNS resolver],
[case "${enableval}" in
  yes) use_adns=true ;;
  no)  use_adns=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-adns]) ;;
esac],[use_adns=true])
if $use_adns ; then
  AC_DEFINE(HAVE_ADNS, 1, [The ADNS resolver library is available.])
fi

AC_ARG_WITH(socks,
[  --with-socks            use SOCKS firewall proxy (requires libsocks)],
[case "${withval}" in
  yes) use_socks=true ;;
  no)  use_socks=false ;;
  *) AC_MSG_ERROR([bad value ${withval} for --with-socks]) ;;
esac],[use_socks=false])

dnl Configure subpackages
AC_CONFIG_SUBDIRS(liboop)

no_wacky_libs=""
GALE_LIBS="\${top_builddir}/libgale/libgale.la"

dnl Checks for programs.
AC_PROG_INSTALL
AC_PROG_YACC
PROG_LDCONFIG=:

dnl Add prefix to LDFLAGS and CPPFLAGS to help find previously installed foo.
test NONE != "$exec_prefix" && LDFLAGS="$LDFLAGS -L${exec_prefix}/lib"
if test NONE != "$prefix" ; then
  CPPFLAGS="$CPPFLAGS -I${prefix}/include"
  test NONE = "$exec_prefix" && LDFLAGS="$LDFLAGS -L${prefix}/lib"
fi

dnl People often put stuff in /usr/local.
if test /usr/local != "$exec_prefix" -a /usr/local != "$prefix" ; then
  CPPFLAGS="$CPPFLAGS -I/usr/local/include"
  LDFLAGS="$LDFLAGS -L/usr/local/lib"
fi

dnl Add special GCC warnings, but don't enable them yet.
warning_flags="$CFLAGS"
error_flags="$CFLAGS"
if test yes = "$GCC" ; then
  warning_flags="-Wall -Wno-comment $CFLAGS"
  error_flags="-Werror $warning_flags"
fi

dnl System type checks.
case "$host" in
  *-linux-*)
    AC_PATH_PROG(PROG_LDCONFIG, ldconfig, :, $PATH:/usr/sbin:/sbin)
    ;;
  *-sgi-irix6*)
    if test -n "$LPATH" ; then
      LDFLAGS="-Wl,-rpath,$LPATH $LDFLAGS"
    fi
    no_wacky_libs=yes
    AC_DEFINE(OS_IRIX, 1, [This is SGI IRIX.])
    ;;
  *-hp-hpux*)
    AC_DEFINE(OS_HPUX, 1, [This is Hewlett-Packard HP-UX.])
    ;;
  *-*-solaris2*)
    AC_DEFINE(OS_SOLARIS, 1, [This is Sun Solaris.])
    ;;
  *-*-*bsd*)
    AC_DEFINE(OS_BSD, 1, [This is a BSD-based operating system.])
    ;;
esac

dnl Checks for libraries.
if test -z "$no_wacky_libs" ; then
  AC_CHECK_LIB(nsl,main)
  AC_CHECK_LIB(socket,socket)
fi

termcap_lib=""
AC_CHECK_LIB(termcap,tgetent,[termcap_lib="-ltermcap"],[
  AC_CHECK_LIB(ncurses,tgetent)])
AC_CHECK_LIB(resolv,res_query)

test -d /usr/local/ssl/lib && LDFLAGS="$LDFLAGS -L/usr/local/ssl/lib"
AC_CHECK_LIB(crypto,EVP_OpenInit,[GALE_LIBS="$GALE_LIBS -lcrypto"],[
  AC_MSG_ERROR([cannot find OpenSSL crypto library])
])

# This adds the option of compiling without using the iconv library,
# which has proved troublesome for compilation on some platforms
AC_ARG_ENABLE(iconv,
  [ --disable-iconv   Disable compilation with iconv],
  [case "${enableval}" in
     yes | no ) WITH_ICONV="${enableval}" ;;
     *) AC_MSG_ERROR(bad value ${enableval} for --disable-iconv) ;;
  esac],
  [WITH_ICONV="yes"]
)

dnl Make sure we register this option with Automake, so we know whether to
dnl descend into iconv for more configuration or not
AM_CONDITIONAL([WITH_ICONV], [test "x$WITH_ICONV" = "xyes"])

# Define ICONV in config.h if we're going to compile against it
if test "x$WITH_ICONV" = "xyes"; then
  dnl Make sure 'iconv' supports multibyte UTF-8 conversion.
  iconv_lib=""
  AC_CHECK_LIB(iconv,iconv,[iconv_lib="-liconv"])
  AC_MSG_CHECKING([for working iconv])
  save_libs="$LIBS"
  save_flags="$CFLAGS"
  LIBS="$LIBS $iconv_lib"
  CFLAGS="$error_flags $save_flags -DICONV_CONST="
  AC_TRY_RUN([#include "${srcdir}/iconvtest.c"],[
    GALE_LIBS="$GALE_LIBS $iconv_lib"
    AC_MSG_RESULT([yes, no const])
    AC_DEFINE(HAVE_ICONV, 1, [iconv(3) is available and functional.])
    AC_DEFINE(ICONV_CONST, [], [The second argument to iconv(3) is const.])
  ],[
    CFLAGS="$warning_flags $save_flags -DICONV_CONST=const"
    AC_TRY_RUN([#include "${srcdir}/iconvtest.c"],[
      GALE_LIBS="$GALE_LIBS $iconv_lib"
      AC_MSG_RESULT([yes, const])
      AC_DEFINE(HAVE_ICONV, 1, [iconv(3) is available and functional.])
      AC_DEFINE(ICONV_CONST, const, [The second argument to iconv(3) is const.])
    ],[AC_MSG_RESULT(no)],
    [AC_MSG_RESULT([unknown, assuming no])])
  ],[AC_MSG_RESULT([unknown, assuming no])])
  LIBS="$save_libs"
  CFLAGS="$save_flags"
fi

AC_CHECK_LIB(dl,dlopen,[
  GALE_LIBS="$GALE_LIBS -ldl"
  AC_DEFINE(HAVE_LIBDL, 1, [The dynamic loader library is available.])])
AC_CHECK_LIB(readline,readline,[
  GALE_LIBS="$GALE_LIBS -lreadline"
  AC_DEFINE(HAVE_LIBREADLINE, 1, [The GNU readline library is available.])],
  [],$termcap_lib)

if test x$use_socks = xtrue ; then
AC_CHECK_LIB(socks,SOCKSinit,[
  GALE_LIBS="$GALE_LIBS -lsocks"
  AC_DEFINE(HAVE_SOCKS, 1, [The SOCKS firewall proxy is available.]) 
],[
  AC_MSG_ERROR([cannot find SOCKS library])
])
fi

AC_CHECK_LIB(gc,GC_malloc,[GALE_LIBS="$GALE_LIBS -lgc"],[
  AC_MSG_ERROR([cannot find Boehm GC library, see INSTALL directions])
])

AC_MSG_CHECKING([for local copy of liboop])
if test -f "${srcdir}/liboop/oop.h"; then
  AC_MSG_RESULT(yes)
  CPPFLAGS="-I\${top_srcdir}/liboop $CPPFLAGS"
  build_liboop=true
  if $use_adns; then
    AC_CHECK_LIB(adns,adns_init,[:],[
      AC_MSG_ERROR([cannot find adns library, try configure --disable-adns])
    ])
    GALE_LIBS="$GALE_LIBS \${top_builddir}/liboop/liboop-adns.la"
  fi
  GALE_LIBS="$GALE_LIBS \${top_builddir}/liboop/liboop.la"
else
  AC_MSG_RESULT(no)
  AC_CHECK_LIB(oop,oop_sys_new,[:],[
    AC_MSG_ERROR([cannot find liboop library, see INSTALL directions])
  ])
  if $use_adns; then
    AC_CHECK_LIB(oop-adns,oop_adns_new,[:],[
      AC_MSG_ERROR([cannot find liboop-adns library, was liboop built for adns?])
    ],-loop)
    AC_CHECK_LIB(adns,adns_init,[:],[
      AC_MSG_ERROR([cannot find adns library, try configure --disable-adns])
    ])
    GALE_LIBS="$GALE_LIBS -loop-adns -loop -ladns"
  else
    GALE_LIBS="$GALE_LIBS -loop"
  fi
fi

GALE_LIBS="$GALE_LIBS $termcap_lib"

dnl Checks for header files.
AC_CHECK_HEADERS(sys/bitypes.h sys/select.h curses.h term.h dlfcn.h readline/readline.h getopt.h rune.h wchar.h)

test -d /usr/local/ssl/include && CPPFLAGS="$CPPFLAGS -I/usr/local/ssl/include"
AC_CHECK_HEADERS(openssl/evp.h,,[AC_MSG_ERROR([cannot find OpenSSL headers])])

test -d /usr/include/gc && CPPFLAGS="$CPPFLAGS -I/usr/include/gc"
AC_CHECK_HEADERS(gc_backptr.h)
AC_CHECK_HEADERS(gc.h,,[
  AC_MSG_ERROR([cannot find Boehm GC headers, see INSTALL directions])
])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(long)

dnl Checks for library functions.

dnl Output.

CFLAGS_NOWARN="$CFLAGS"
CFLAGS="$warning_flags"
CPPFLAGS="-I\${top_srcdir}/include -I\${top_builddir}/include $CPPFLAGS"
CPPFLAGS="-DGALE_SYS_DIR=\\\"${sysconfdir}/gale\\\" $CPPFLAGS"

AC_SUBST(CFLAGS_NOWARN)
AC_SUBST(ZEPHYR_LIBS)
AC_SUBST(GALE_LIBS)
AC_SUBST(PROG_LDCONFIG)

AM_CONDITIONAL(BUILD_LIBOOP,test x$build_liboop = xtrue)

AC_OUTPUT(
	gale-config
	gale-install
	Makefile 
	libgale/Makefile 
	gsub/Makefile
	gsend/Makefile
	kutils/Makefile
	server/Makefile
	gdomain/Makefile
	include/Makefile 
	include/gale/Makefile
)
