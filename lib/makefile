TOP = ..
LIB_TARGETS = libgale.a
include $(TOP)/rules

libgale.a: message.o link.o gale.o client.o header.o chdir.o auth.o connect.o server.o
	$(AR) ru $@ $^
