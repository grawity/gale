## Process this file with automake to generate Makefile.in

SUBDIRS = include
lib_LIBRARIES = libgc.a
include_HEADERS = gc.h
libgc_a_SOURCES =

DIST_COMMON = Makefile.am Makefile.in

libgc.a:
	$(MAKE) -f Makefile.gc gc.a srcdir="${srcdir}" CC="${CC}"
	mv gc.a libgc.a

clean-local:
	$(MAKE) -f Makefile.gc clean srcdir="${srcdir}" CC="${CC}"

dist-hook:
	$(RM) $(distdir)/README
