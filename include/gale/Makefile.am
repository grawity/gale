## Process this file with automake to generate Makefile.in

sysdepincdir = $(exec_prefix)/include/gale

pkginclude_HEADERS = \
	all.h auth.h client.h compat.h core.h gsubrc.h misc.h types.h
sysdepinc_HEADERS = config.h

DISTCLEANFILES = config.h

dist-hook:
	rm $(distdir)/config.h
