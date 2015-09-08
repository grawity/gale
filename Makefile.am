## Process this file with automake to generate Makefile.in

ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign 1.2
bin_SCRIPTS = gale-config gale-install
EXTRA_DIST = gale.spec iconvtest.c bootstrap Doxyfile

if BUILD_LIBOOP
liboop_dir = liboop
else
liboop_dir =
endif

SUBDIRS = ${liboop_dir} \
	libgale server gsend gsub kutils gdomain include

release: dist
	gzip -dc $(PACKAGE)-$(VERSION).tar.gz | bzip2 -9 \
	> $(PACKAGE)-$(VERSION).tar.bz2
	if test -d ../gdist ; then \
	mv $(PACKAGE)-$(VERSION).* ../gdist ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.gz ../gdist/$(PACKAGE).tar.gz ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.bz2 ../gdist/$(PACKAGE).tar.bz2 ; fi
	@echo '** NOW TAG THE CVS REPOSITORY! **'

install-data-local:
	mkdir -p "$(DESTDIR)$(sysconfdir)/gale/auth/trusted"
	mkdir -p "$(DESTDIR)$(sysconfdir)/gale/auth/private"
	mkdir -p "$(DESTDIR)$(sysconfdir)/gale/auth/local"
	mkdir -p "$(DESTDIR)$(sysconfdir)/gale/auth/cache"
	chmod 1777 "$(DESTDIR)$(sysconfdir)/gale/auth/local"
	chmod 777 "$(DESTDIR)$(sysconfdir)/gale/auth/cache"
	$(INSTALL_DATA) "$(srcdir)/kutils/ROOT" \
		"$(DESTDIR)$(sysconfdir)/gale/auth/trusted/ROOT"
	$(INSTALL_DATA) "$(srcdir)/COPYING" \
		"$(DESTDIR)$(sysconfdir)/gale/COPYING"
	-chmod -R a+rX "$(DESTDIR)$(sysconfdir)/gale" 2>/dev/null
	@echo ""
	@echo "*** now run $(DESTDIR)$(bindir)/gale-install ... even for an upgrade ***"

install-exec-local:
	$(PROG_LDCONFIG) || true
	if test "x$(bindir)" != "x$(sbindir)"; then \
		ln -s -f "$(sbindir)/gksign" "$(DESTDIR)$(bindir)/gksign"; \
	fi
