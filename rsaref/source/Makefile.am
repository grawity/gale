## Process this file with automake to generate Makefile.in

lib_LTLIBRARIES = librsaref.la
librsaref_la_SOURCES = \
	desc.c digit.c md2c.c md5c.c nn.c prime.c rsa.c r_encode.c r_dh.c \
	r_enhanc.c r_keygen.c r_random.c r_stdlib.c
include_HEADERS = rsaref.h global.h md2.h md5.h des.h
noinst_HEADERS = digit.h nn.h prime.h r_random.h rsa.h

INCLUDES = "-DPROTOTYPES=1"
DISTFILES = $(DIST_COMMON) $(TEXINFOS) $(EXTRA_DIST)
