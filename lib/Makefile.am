## Process this file with automake to generate Makefile.in

lib_LTLIBRARIES = libgale.la

# version:revision:age
# current as of 0.91b
libgale_la_LDFLAGS = -version-info 8:0:1

libgale_la_SOURCES = \
	authgrp.c client.c connect.c find.c id.c init.c global.c \
	input.c kill.c libgale.c link.c message.c output.c debug.c \
	standard.c cache.c \
	crypt.c customs.c file.c gen.c \
	keyid.c findkey.c privkey.c pubkey.c random.c sign.c \
	dir.c error.c errpuff.c exec.c alloc.c server.c text.c time.c \
	pack.c term.c \
	fragment.c var.c weaktree.c

noinst_HEADERS = buffer.h \
	common.h file.h id.h key.h pack.h random.h sign.h
