#ifndef GALE_VERSION_H
#define GALE_VERSION_H

#ifndef GALE_VERSION
#error You must define GALE_VERSION.
#endif

#define GALE_BANNER \
	("Gale version " ## GALE_VERSION ## ", copyright 1997 Dan Egnor")

#endif
