## Process this file with automake to generate Makefile.in

# Makefile.am, liboop, copyright 1999 Dan Egnor
#
# This is free software; you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License, version 2.1 or later.
# See the file COPYING for details.

AUTOMAKE_OPTIONS = foreign 1.2
lib_LTLIBRARIES = liboop.la liboop-adns.la liboop-glib.la liboop-www.la liboop-rl.la
INCLUDES = $(GLIB_INCLUDES) $(WWW_INCLUDES)

# versions updated as of 0.7
liboop_la_LDFLAGS = -version-info 3:0:0 # version:revision:age
liboop_la_SOURCES = sys.c select.c signal.c alloc.c

liboop_adns_la_LDFLAGS = -version-info 1:0:1
liboop_adns_la_SOURCES = adns.c

liboop_glib_la_LDFLAGS = -version-info 0:0:0
liboop_glib_la_SOURCES = glib.c

liboop_www_la_LDFLAGS = -version-info 0:0:0
liboop_www_la_SOURCES = www.c

liboop_rl_la_LDFLAGS = -version-info 0:0:0
liboop_rl_la_SOURCES = readline.c

include_HEADERS = oop.h oop-adns.h oop-glib.h oop-www.h oop-rl.h

noinst_PROGRAMS = test-oop

test_oop_SOURCES = test-oop.c
test_oop_LDADD = $(ADNS_LIBS) $(GLIB_LIBS) $(WWW_LIBS) $(READLINE_LIBS) liboop.la

release: dist
	gzip -dc $(PACKAGE)-$(VERSION).tar.gz | bzip2 -9 \
	> $(PACKAGE)-$(VERSION).tar.bz2
	if test -d ../../gdist ; then \
	mv $(PACKAGE)-$(VERSION).* ../../gdist ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.gz ../../gdist/$(PACKAGE).tar.gz ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.bz2 ../../gdist/$(PACKAGE).tar.bz2 ; \
	fi
	@echo '** NOW TAG THE CVS REPOSITORY! **'

install-exec-local:
	$(PROG_LDCONFIG) || true
