#ifndef UTIL_H
#define UTIL_H

void gale_init(const char *);

void *gale_malloc(int size);
void gale_free(void *);

void *gale_memdup(const void *,int);
char *gale_strdup(const char *);
char *gale_strndup(const char *,int);

#endif
