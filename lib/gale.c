#include "gale/misc.h"
#include "gale/core.h"

#include <string.h>

void *gale_realloc(void *s,size_t len) {
	void *n = s ? gale_memdup(s,len) : gale_malloc(len);
	if (s) gale_free(s);
	return n;
}

void *gale_memdup(const void *s,int len) {
	void *r = gale_malloc(len);
	memcpy(r,s,len);
	return r;
}

char *gale_strndup(const char *s,int len) {
	char *r = gale_memdup(s,len + 1);
	r[len] = '\0';
	return r;
}

char *gale_strdup(const char *s) {
	return gale_strndup(s,strlen(s));
}
