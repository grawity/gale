TOP = ..
LIB_TARGETS = libgale.a
include $(TOP)/rules

libgale.a: message.o link.o gale.o client.o header.o dir.o auth.o connect.o server.o error.o
	$(AR) ru $@ $^
