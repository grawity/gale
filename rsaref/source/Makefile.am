## Process this file with automake to generate Makefile.in

lib_LIBRARIES = librsaref.a
librsaref_a_SOURCES = \
	desc.c digit.c md2c.c md5c.c nn.c prime.c rsa.c r_encode.c r_dh.c \
	r_enhanc.c r_keygen.c r_random.c r_stdlib.c
noinst_HEADERS = \
	des.h digit.h global.h md2.h md5.h nn.h prime.h r_random.h rsa.h \
	rsaref.h

INCLUDES = "-DPROTOTYPES=1"
DISTFILES = $(DIST_COMMON) $(TEXINFOS) $(EXTRA_DIST)
