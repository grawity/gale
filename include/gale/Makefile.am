## Process this file with automake to generate Makefile.in

sysdepincdir = $(exec_prefix)/include/gale

pkginclude_HEADERS = \
	all.h auth.h client.h compat.h core.h crypto.h \
	gsubrc.h key.h misc.h types.h globals.h
sysdepinc_HEADERS = config.h

DISTCLEANFILES = config.h
EXTRA_DIST = config.h.dox

dist-hook:
	rm $(distdir)/config.h
