TOP = ..
include $(TOP)/rules

all:: mkdir/build
	$(MAKE) -C build -f ../install/unix/makefile rsaref.a

clean::
	$(RM) -r build
