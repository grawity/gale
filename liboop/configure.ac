dnl Process this file with autoconf to produce a configure script.
AC_INIT(INSTALL)
AM_INIT_AUTOMAKE(liboop,0.9)
AC_CANONICAL_HOST

dnl Use libtool for shared libraries
AM_PROG_LIBTOOL

dnl Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
PROG_LDCONFIG=:

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

AC_CHECK_HEADERS(poll.h sys/select.h sys/socket.h)

AC_CHECK_LIB(adns,adns_init,[
  ADNS_LIBS="-ladns"
  AC_DEFINE(HAVE_ADNS)
])

AC_CHECK_LIB(readline,rl_callback_handler_install,[
AC_CHECK_HEADER(readline/readline.h,[
  READLINE_LIBS="-lreadline"
  AC_DEFINE(HAVE_READLINE)
])])

save_libs="$LIBS"
save_cppflags="$CPPFLAGS"
AC_CHECK_PROG(PROG_GLIB_CONFIG,glib-config,glib-config)
if test -n "$PROG_GLIB_CONFIG" ; then
  GLIB_INCLUDES="`glib-config --cflags`"
  GLIB_LIBS="`glib-config --libs`"
  LIBS="$save_libs $GLIB_LIBS"
  CPPFLAGS="$save_cppflags $GLIB_INCLUDES"
  AC_CHECK_FUNC(g_main_set_poll_func,[
  AC_CHECK_HEADER(glib.h,[AC_DEFINE(HAVE_GLIB)])])
fi
LIBS="$save_libs"
CPPFLAGS="$save_cppflags"

for version in 8.4 8.3 8.2 8.1 8.0 ; do
  CPPFLAGS="$save_cppflags -I/usr/include/tcl$version"
  AC_CHECK_LIB(tcl$version,Tcl_Main,[
  AC_CHECK_HEADER(tcl.h,[
    AC_DEFINE(HAVE_TCL)
    TCL_INCLUDES="-I/usr/include/tcl$version"
    TCL_LIBS="-ltcl$version"
    break
  ])])
done
CPPFLAGS="$save_cppflags"

AC_CHECK_LIB(wwwcore,HTEvent_setRegisterCallback,[
  AC_DEFINE(HAVE_WWW)
  # This is just a little unpleasant.
  WWW_LIBS=" \
  -lwwwapp -lwwwcache -lwwwcore -lwwwdir -lwwwfile -lwwwftp -lwwwgopher \
  -lwwwhtml -lwwwhttp -lwwwinit -lwwwmime -lwwwmux -lwwwnews -lwwwstream \
  -lwwwtelnet -lwwwtrans -lwwwutils -lwwwzip -lmd5"
  WWW_INCLUDES="-I/usr/include/w3c-libwww" # the libwww RPM puts headers here
],,-lwwwutils)

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
AC_SUBST(TCL_INCLUDES)
AC_SUBST(TCL_LIBS)
AC_SUBST(ADNS_LIBS)
AC_SUBST(WWW_INCLUDES)
AC_SUBST(WWW_LIBS)
AC_SUBST(READLINE_LIBS)
AC_OUTPUT(Makefile)
