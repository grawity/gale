## Process this file with automake to generate Makefile.in

# Makefile.am, liboop, copyright 1999 Dan Egnor
#
# This is free software; you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License, version 2.1 or later.
# See the file COPYING for details.

AUTOMAKE_OPTIONS = foreign 1.2
lib_LTLIBRARIES = liboop.la liboop-adns.la
noinst_PROGRAMS = test-oop test-adns

INCLUDES = -I$(srcdir)/foreign

liboop_la_LDFLAGS = -version-info 1:0:1 # version:revision:age
liboop_la_SOURCES = sys.c select.c alloc.c

liboop_adns_la_LDFLAGS = -version-info 0:0:0 # version:revision:age
liboop_adns_la_SOURCES = adns.c

include_HEADERS = oop.h oop-adns.h

LDADD = liboop-adns.la liboop.la -ladns
test_oop_SOURCES = test-oop.c
test_adns_SOURCES = test-adns.c

release: dist
	gzip -dc $(PACKAGE)-$(VERSION).tar.gz | bzip2 -9 \
	> $(PACKAGE)-$(VERSION).tar.bz2
	if test -d ../../gdist ; then \
	mv $(PACKAGE)-$(VERSION).* ../../gdist ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.gz ../../gdist/$(PACKAGE).tar.gz ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.bz2 ../../gdist/$(PACKAGE).tar.bz2 ; \
	fi

install-exec-local:
	$(PROG_LDCONFIG) || true
