TOP = .
SUBDIRS = $(BUILD)
include $(TOP)/rules

tar: clean
	cd .. ; find gale -name CVS | tar -X - -cvzf gale.tgz gale

squeaky: clean
	$(RM) config.cache include/gale/config.h config.log config.status defs
