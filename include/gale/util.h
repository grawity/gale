/* util.h -- gale miscellaneous utilities. */

#ifndef UTIL_H
#define UTIL_H

#include <sys/types.h>
#include "compat.h"

typedef u_int8_t byte;

/* handy data type for a counted buffer. */
struct gale_data {
	byte *p;
	size_t l;
};

/* preinitialized directories, set by gale_init. 

   dot_gale  ~/.gale
   home_dir  ~
   sys_dir   etc/gale */
extern struct gale_dir *dot_gale,*home_dir,*sys_dir;

/* Initialize gale stuff.  First parameter is the program name ("gsub");
   the next two are argc and argv. */
void gale_init(const char *,int argc,char * const *argv);
/* Restart ourselves -- re-exec() the program with the same argc and argv.
   Called automatically on SIGUSR1. */
void gale_restart(void);
/* Run a subprogram, with the name given by "prog" (will search PATH), and
   the given argv (NULL-terminated).  Returns the pid of the child, or -1 if
   an error happenned.  If "in" is non-NULL, a pipe will be established to
   the process' standard input and the fd (open for writing) returned in "in";
   similarly for "out", standard out, open for reading.  The last argument is
   a function to call (with the argument list) if the exec failed, to provide
   default functionality; if NULL, it will report an error and exit the sub-
   process instead. */
pid_t gale_exec(const char *prog,char * const *argv,int *in,int *out,
                void (*)(char * const *));

/* Memory management.  Programs have to define the first two themselves, with
   whatever allocation policy they want. */
void *gale_malloc(size_t size);
void gale_free(void *);
void *gale_realloc(void *,size_t);

/* Duplicate memory, strings, counted strings, etc. */
void *gale_memdup(const void *,int);
char *gale_strdup(const char *);
char *gale_strndup(const char *,int);

#endif
