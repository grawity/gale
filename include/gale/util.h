#ifndef UTIL_H
#define UTIL_H

extern struct gale_dir *dot_gale,*home_dir,*sys_dir;

void gale_init(const char *,int argc,char * const *argv);
void gale_restart(void);

void *gale_malloc(size_t size);
void gale_free(void *);

void *gale_memdup(const void *,int);
char *gale_strdup(const char *);
char *gale_strndup(const char *,int);

#endif
