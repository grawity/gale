dnl Process this file with autoconf to produce a configure script.
AC_INIT(liboop,1.0)
AC_CONFIG_SRCDIR(INSTALL)
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE
AC_CANONICAL_HOST
AM_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_INSTALL
PROG_LDCONFIG=:

AC_ARG_WITH(adns, AC_HELP_STRING(--without-adns,disable ADNS adapter))
AC_ARG_WITH(readline, AC_HELP_STRING(--without-readline,disable readline adapter))
AC_ARG_WITH(glib, AC_HELP_STRING(--without-glib,disable GLib adapter))
AC_ARG_WITH(tcl, AC_HELP_STRING(--without-tcl,disable Tcl/Tk adapter))
AC_ARG_WITH(libwww, AC_HELP_STRING(--with-libwww,build libwww adapter))

dnl System type checks.
case "$host" in
  *-linux-*)
    AC_PATH_PROG(PROG_LDCONFIG, ldconfig, :, $PATH:/usr/sbin:/sbin)
    no_wacky_libs=yes
    ;;
  *-sgi-irix6*)
    if test -n "$LPATH" ; then
      LDFLAGS="-Wl,-rpath,$LPATH $LDFLAGS"
    fi
    no_wacky_libs=yes
    ;;
esac

AC_CHECK_HEADERS(poll.h sys/select.h sys/socket.h string.h strings.h)

if test xno != x$with_adns; then
  AC_CHECK_LIB(adns,adns_init,[
    LIBOOP_LIBS="liboop-adns.la $LIBOOP_LIBS"
    ADNS_LIBS="-ladns"
    AC_DEFINE(HAVE_ADNS)
  ])
fi

if test xno != x$with_readline; then
  AC_CHECK_LIB(readline,rl_callback_handler_install,[
  AC_CHECK_HEADER(readline/readline.h,[
    LIBOOP_LIBS="liboop-rl.la $LIBOOP_LIBS"
    READLINE_LIBS="-lreadline"
    AC_DEFINE(HAVE_READLINE)
  ])])
fi

if test xno != x$with_glib; then
  save_libs="$LIBS"
  save_cppflags="$CPPFLAGS"
  AC_CHECK_PROG(PROG_GLIB_CONFIG,glib-config,glib-config)
  if test -n "$PROG_GLIB_CONFIG" ; then
    GLIB_INCLUDES="`glib-config --cflags`"
    GLIB_LIBS="`glib-config --libs`"
    LIBS="$save_libs $GLIB_LIBS"
    CPPFLAGS="$save_cppflags $GLIB_INCLUDES"
    AC_CHECK_FUNC(g_main_set_poll_func,[
    AC_CHECK_HEADER(glib.h,[
      LIBOOP_LIBS="liboop-glib.la $LIBOOP_LIBS"
      AC_DEFINE(HAVE_GLIB)
    ])])
  fi
  LIBS="$save_libs"
  CPPFLAGS="$save_cppflags"

  PKG_CHECK_MODULES(GLIB2,glib-2.0 >= 2.0,[
    LIBOOP_LIBS="liboop-glib2.la $LIBOOP_LIBS"
    AC_DEFINE(HAVE_GLIB)
    ],[:])
fi

if test xno != x$with_tcl; then
  for version in 8.6 8.5 8.4 8.3 8.2 8.1 8.0 ; do
    CPPFLAGS="$save_cppflags -I/usr/include/tcl$version"
    AC_CHECK_LIB(tcl$version,Tcl_Main,[
    AC_CHECK_HEADER(tcl.h,[
      LIBOOP_LIBS="liboop-tcl.la $LIBOOP_LIBS"
      AC_DEFINE(HAVE_TCL)
      TCL_INCLUDES="-I/usr/include/tcl$version"
      TCL_LIBS="-ltcl$version"
      break
    ])])
  done
  CPPFLAGS="$save_cppflags"
fi

if test xyes = x$with_libwww; then
  save_libs="$LIBS"
  save_cppflags="$CPPFLAGS"
  AC_CHECK_PROG(PROG_WWW_CONFIG,libwww-config,libwww-config)
  if test -n "$PROG_WWW_CONFIG" ; then
    WWW_INCLUDES="`libwww-config --cflags`"
    WWW_LIBS="`libwww-config --libs`"
    LIBS="$save_libs $WWW_LIBS"
    CPPFLAGS="$save_cppflags $WWW_INCLUDES"
    AC_CHECK_FUNC(HTEvent_setRegisterCallback,[
      LIBOOP_LIBS="liboop-www.la $LIBOOP_LIBS"
      AC_DEFINE(HAVE_WWW)
    ])
  fi
  LIBS="$save_libs"
  CPPFLAGS="$save_cppflags"
fi

if test -z "$no_wacky_libs" ; then
  AC_CHECK_LIB(resolv,res_query)
  AC_SEARCH_LIBS(gethostbyname,nsl)
  AC_SEARCH_LIBS(socket,socket)
fi

test yes = "$GCC" && 
CFLAGS="-Wall -Wno-comment -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wwrite-strings $CFLAGS"' $(EXTRA_CFLAGS)'

AC_SUBST(PROG_LDCONFIG)
AC_SUBST(GLIB_INCLUDES)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB2_CFLAGS)
AC_SUBST(GLIB2_LIBS)
AC_SUBST(TCL_INCLUDES)
AC_SUBST(TCL_LIBS)
AC_SUBST(ADNS_LIBS)
AC_SUBST(WWW_INCLUDES)
AC_SUBST(WWW_LIBS)
AC_SUBST(READLINE_LIBS)
AC_SUBST(LIBOOP_LIBS)
AC_OUTPUT([Makefile liboop.pc liboop-glib2.pc])
