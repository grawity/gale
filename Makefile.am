## Process this file with automake to generate Makefile.in

SUBDIRS = @BUILD@
AUTOMAKE_OPTIONS = foreign 1.2
noinst_SCRIPTS = common.sh config.sh domain.sh
EXTRA_DIST = common.sh config.sh domain.sh
DIST_SUBDIRS = \
	auth gdomain gkeys gsend gsub gwatch lib rsaref server \
	gzgw glog glsql include gc

release: dist
	gzip -dc $(PACKAGE)-$(VERSION).tar.gz | bzip2 -9 \
	> $(PACKAGE)-$(VERSION).tar.bz2
	if test -d ../gdist ; then \
	mv $(PACKAGE)-$(VERSION).* ../gdist ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.gz ../gdist/$(PACKAGE).tar.gz ; \
	ln -sf $(PACKAGE)-$(VERSION).tar.bz2 ../gdist/$(PACKAGE).tar.bz2 ; fi

install-data-local: config

config: common.sh config.sh
	sysconfdir=$(sysconfdir) ; \
	sbindir=$(sbindir) ; \
	bindir=$(bindir) ; \
	srcdir=$(srcdir) ; \
	export srcdir sysconfdir sbindir bindir ; \
	sh $(srcdir)/config.sh

domain: common.sh domain.sh
	sysconfdir=$(sysconfdir) ; \
	sbindir=$(sbindir) ; \
	bindir=$(bindir) ; \
	srcdir=$(srcdir) ; \
	export srcdir sysconfdir sbindir bindir ; \
	sh $(srcdir)/domain.sh
