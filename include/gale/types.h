/* types.h -- basic types used by gale */

#ifndef GALE_TYPES_H
#define GALE_TYPES_H

#include <stddef.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
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

/* tagged variant values */
struct gale_fragment;

struct gale_group {
	const struct gale_fragment *list;
	size_t len;
	const struct gale_group *next;
};

enum gale_fragment_type { 
	frag_text, frag_data, frag_time, frag_number, frag_group 
};

struct gale_fragment {
	struct gale_text name;
	enum gale_fragment_type type;
	union {
		struct gale_text text;
		struct gale_data data;
		struct gale_time time;
		struct gale_group group;
		s32 number;
	} value;
};

/* callback */
struct gale_call {
	void (*invoke)(void *system,void *user);
	void *user;
};

#endif
