## Process this file with automake to generate Makefile.in

# Makefile.am, liboop, copyright 1999 Dan Egnor
#
# This is free software; you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License, version 2.1 or later.
# See the file COPYING for details.

AUTOMAKE_OPTIONS = foreign 1.2
lib_LTLIBRARIES = liboop.la
noinst_PROGRAMS = test

liboop_la_LDFLAGS = -version-info 0:0:0 # version:revision:age
liboop_la_SOURCES = sys.c test.c alloc.c
include_HEADERS = oop.h

LDADD = liboop.la
test_SOURCES = test.c

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
