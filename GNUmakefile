TOP = .
SUBDIRS = $(BUILD)
include $(TOP)/rules

tar: squeaky
	cd .. ; find gale -name CVS | \
	tar -X - -X gale/rsaref/exclude -cvzf - gale \
	> gdist/gale-$(GALE_VERSION).tar.gz ; \
	gzip -dc gdist/gale-$(GALE_VERSION).tar.gz | bzip2 -9 \
	> gdist/gale-$(GALE_VERSION).tar.bz2

squeaky: clean
	$(RM) config.cache include/gale/config.h config.log config.status defs

install:: config

config:
	@sh config.sh

domain:
	@sh domain.sh
