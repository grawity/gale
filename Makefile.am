## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = foreign 1.2
bin_SCRIPTS = gale-config gale-install
EXTRA_DIST = gale.spec iconvtest.c prepare

if BUILD_GZGW
gzgw_dir = gzgw
else
gzgw_dir =
endif

if BUILD_RSAREF
rsaref_dir = rsaref
else
rsaref_dir =
endif

if BUILD_GC
gc_dir = gc
else
gc_dir =
endif

if BUILD_GLXML
glxml_dir = glxml
else
glxml_dir =
endif

if BUILD_LIBOOP
liboop_dir = liboop
else
liboop_dir =
endif

SUBDIRS = ${gc_dir} ${rsaref_dir} ${liboop_dir} \
	lib server gsend gsub gwatch kutils gdomain \
	${gzgw_dir} ${glxml_dir} include

release: dist
	gzip -dc $(PACKAGE)-$(VERSION).tar.gz | bzip2 -9 \
	> $(PACKAGE)-$(VERSION).tar.bz2
	if test -d ../gdist ; then \
	mv $(PACKAGE)-$(VERSION).* ../gdist ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.gz ../gdist/$(PACKAGE).tar.gz ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.bz2 ../gdist/$(PACKAGE).tar.bz2 ; fi
	@echo '** NOW TAG THE CVS REPOSITORY! **'

dist-hook:
	test ! -f $(srcdir)/lib/.deps/alloc.P

install-data-local:
	mkdir -p "$(sysconfdir)/gale/auth/trusted"
	mkdir -p "$(sysconfdir)/gale/auth/private"
	mkdir -p "$(sysconfdir)/gale/auth/local"
	mkdir -p "$(sysconfdir)/gale/auth/cache"
	chmod 1777 "$(sysconfdir)/gale/auth/local"
	chmod 777 "$(sysconfdir)/gale/auth/cache"
	$(INSTALL_DATA) "$(srcdir)/kutils/ROOT" "$(sysconfdir)/gale/auth/trusted/ROOT"
	$(INSTALL_DATA) "$(srcdir)/COPYING" "$(sysconfdir)/gale/COPYING"
	-chmod -R a+rX "$(sysconfdir)/gale"
	@echo ""
	@echo "*** now run $(bindir)/gale-install ... even for an upgrade ***"

install-exec-local:
	$(PROG_LDCONFIG) || true
	test "x$(bindir)" != "x$(sbindir)" && \
	ln -s "$(sbindir)/gksign" "$(bindir)/gksign.tmp.$$$$" && \
	mv "$(bindir)/gksign.tmp.$$$$" "$(bindir)/gksign"
