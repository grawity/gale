## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = foreign 1.2
noinst_SCRIPTS = common.sh config.sh domain.sh
bin_SCRIPTS = gale-config gale-install
EXTRA_DIST = common.sh config.sh domain.sh

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

SUBDIRS = @subdirs@ ${gc_dir} ${rsaref_dir} \
	lib server gsend gsub gwatch kutils gdomain \
	${gzgw_dir} include

release: dist
	gzip -dc $(PACKAGE)-$(VERSION).tar.gz | bzip2 -9 \
	> $(PACKAGE)-$(VERSION).tar.bz2
	if test -d ../gdist ; then \
	mv $(PACKAGE)-$(VERSION).* ../gdist ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.gz ../gdist/$(PACKAGE).tar.gz ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.bz2 ../gdist/$(PACKAGE).tar.bz2 ; fi

install-data-local:
	mkdir -p "$(sysconfdir)/gale/auth/trusted"
	cp "$(srcdir)/kutils/ROOT" "$(sysconfdir)/gale/auth/trusted"
	$(INSTALL_DATA) "$(srcdir)/COPYING" "$(sysconfdir)/gale/COPYING"
	@echo ""
	@echo "*** run $(bindir)/gale-install to complete installation ***"

install-exec-local:
	$(PROG_LDCONFIG) || true
