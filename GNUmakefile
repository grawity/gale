TOP = .
SUBDIRS = $(BUILD)
include $(TOP)/rules

tar: squeaky
	cd .. ; find gale -name CVS | tar -X - -cvzf gale.tgz gale

squeaky: clean
	$(RM) config.cache include/gale/config.h config.log config.status defs

install::
	@echo ""
	@echo "Don't forget to make authinstall ..."

authinstall:
	@cd auth ; ./config.sh
