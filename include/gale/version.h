/* version.h -- gale version */

#ifndef GALE_VERSION_H
#define GALE_VERSION_H

/* The makefiles define this based on the "version" file. */
#ifndef GALE_VERSION
#error You must define GALE_VERSION.
#endif

/* A banner, suitable for usage messages. */
#define GALE_BANNER \
	("Gale version " ## GALE_VERSION ## ", copyright 1997 Dan Egnor")

#endif
