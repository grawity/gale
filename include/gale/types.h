/* types.h -- basic types used by gale */

#ifndef GALE_TYPES_H
#define GALE_TYPES_H

#include <stddef.h>
#include <sys/types.h>
#include "gale/compat.h"
#include "gale/config.h"

#if SIZEOF_INT == 4
typedef unsigned int u32;
typedef signed int s32;
#elif SIZEOF_LONG == 4
typedef unsigned long u32;
typedef signed long s32;
#elif SIZEOF_SHORT == 4
typedef unsigned short u32;
typedef signed short s32;
#else
#error Cannot find 32-bit data type!
#endif

#if SIZEOF_INT == 2
typedef unsigned int u16;
#elif SIZEOF_LONG == 2
typedef unsigned long u16;
#elif SIZEOF_SHORT == 2
typedef unsigned short u16;
#else
#error Cannot find 16-bit data type!
#endif

typedef unsigned char u8;

typedef u8 byte;	/* alias */
typedef wchar_t wch;	/* wide char */

/* handy data type for a counted buffer. */
struct gale_data {
	byte *p;
	size_t l;
};

/* counted buffer of (unicode) text. */
struct gale_text {
	const wch *p;
	size_t l;	/* in wch's */
};

/* sufficiently high precision time */
struct gale_time {
	s32 sec_high;
	u32 sec_low;
	u32 frac_high,frac_low;
};

#endif
