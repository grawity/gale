TOP = .
SUBDIRS = $(BUILD)
include $(TOP)/rules

tar: squeaky
	cd .. ; find gale -name CVS | \
	tar -X - -X gale/rsaref/exclude -cvzf gale.tgz gale

squeaky: clean
	$(RM) config.cache include/gale/config.h config.log config.status defs

install:: config

config:
	@sh config.sh

domain:
	@sh domain.sh
