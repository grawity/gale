TOP = .
SUBDIRS = lib server gsend gsub gwatch include/gale
include $(TOP)/rules

ifdef ZEPHYR_LIB
SUBDIRS += gzgw
endif

ifdef DB_LIB
SUBDIRS += glog
endif
