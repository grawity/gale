TOP = .
SUBDIRS = lib auth server gsend gsub gwatch gkeys gdomain include/gale
include $(TOP)/rules

ifdef ZEPHYR_LIB
SUBDIRS += gzgw
endif

ifdef DB_LIB
SUBDIRS += glog
endif

tar: clean
	cd .. ; find gale -name CVS | tar -X - -cvzf gale.tgz gale
