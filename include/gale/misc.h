/* misc.h -- stuff used in gale that doesn't have to do with gale /per se/. */

#ifndef GALE_MISC_H
#define GALE_MISC_H

#include <string.h>
#include <stddef.h>
#include "gale/types.h"

/* -- process management --------------------------------------------------- */

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
/* Wait for the subprogram to exit and return its return code.  You should call
   this to avoid zombies. */
int gale_wait(pid_t pid);

/* Look for other processes of the same type on the current tty and kill them
   (if do_kill is 1); register ourselves (with temp files) to get killed in
   a similar manner if appropriate.  The string is an identifier for the
   program type e.g. "gsub". */
void gale_kill(const char *,int do_kill);
/* Register a cleanup function.  This will get called, if at all possible, when
   the program exists, including most signals. */
void gale_cleanup(void (*)(void));
/* Perform all the cleanup functions "early". */
void gale_do_cleanup();

/* -- memory management ---------------------------------------------------- */

/* You must define these two! */
void *gale_malloc(size_t size);
void gale_free(void *);

/* Duplicate memory, strings, counted strings, etc. */
void *gale_memdup(const void *,int);
char *gale_strdup(const char *);
char *gale_strndup(const char *,int);

/* Not really safe. */
void *gale_realloc(void *,size_t);

/* -- text buffer manipulation --------------------------------------------- */

extern const struct gale_text null_text;

struct gale_text new_gale_text(size_t alloc);
void free_gale_text(struct gale_text);
#define _G(x) (_gale_text_literal(L##x))
struct gale_text _gale_text_literal(const wchar_t *);

void gale_text_append(struct gale_text *,struct gale_text);

struct /*owned*/ gale_text gale_text_dup(struct gale_text);
struct gale_text gale_text_left(struct gale_text,int);
struct gale_text gale_text_right(struct gale_text,int);
int gale_text_token(struct gale_text string,wch sep,struct gale_text *token);
int gale_text_compare(struct gale_text,struct gale_text);

typedef /*owned*/ struct gale_text gale_text_from(const char *,int len);
typedef /*owned*/ char *gale_text_to(struct gale_text);

gale_text_from gale_text_from_local,gale_text_from_latin1,gale_text_from_utf8;
gale_text_to gale_text_to_local,gale_text_to_latin1,gale_text_to_utf8;

char *gale_text_hack(struct gale_text);

/* -- time functions ------------------------------------------------------- */

struct timeval;

struct gale_time gale_time_zero(void);
struct gale_time gale_time_now(void);
struct gale_time gale_time_forever(void);

int gale_time_less(struct gale_time,struct gale_time);

void gale_time_to(struct timeval *,struct gale_time);
void gale_time_from(struct gale_time *,struct timeval *);

/* -- data interchange conversion ------------------------------------------ */

extern const struct gale_data null_data;

int gale_unpack_copy(struct gale_data *,void *,size_t);
int gale_unpack_compare(struct gale_data *,const void *,size_t);
void gale_pack_copy(struct gale_data *,const void *,size_t);
#define gale_copy_size(s) (s)

int gale_unpack_skip(struct gale_data *);
void gale_pack_skip(struct gale_data *,size_t);
#define gale_skip_size(sz) ((sz) + gale_u32_size())

int gale_unpack_rle(struct gale_data *,void *,size_t);
void gale_pack_rle(struct gale_data *,const void *,size_t);
#define gale_rle_size(s) (((s)+127)/128+(s))

int gale_unpack_u32(struct gale_data *,u32 *);
void gale_pack_u32(struct gale_data *,u32);
#define gale_u32_size() (sizeof(u32))

int gale_unpack_wch(struct gale_data *,wch *);
void gale_pack_wch(struct gale_data *,wch);
#define gale_wch_size() (sizeof(u16))

/* ANSI; deprecated! */
int gale_unpack_str(struct gale_data *,const char **);
void gale_pack_str(struct gale_data *,const char *);
#define gale_str_size(t) (strlen(t) + 1)

int gale_unpack_text(struct gale_data *,/*owned*/ struct gale_text *);
void gale_pack_text(struct gale_data *,struct gale_text);
#define gale_text_size(t) (gale_text_len_size(t) + gale_u32_size())

int gale_unpack_text_len(struct gale_data *,size_t len,
                         /*in,out*/ struct gale_text *);
void gale_pack_text_len(struct gale_data *,struct gale_text);
#define gale_text_len_size(t) ((t).l * gale_wch_size())

int gale_unpack_time(struct gale_data *,struct gale_time *);
void gale_pack_time(struct gale_data *,struct gale_time);
#define gale_time_size() (sizeof(u32) * 4)

/* -- directory management stuff ------------------------------------------- */

/* The dir object represents a directory (y'know, in the filesystem). */
struct gale_dir;

/* preinitialized directories, set by gale_init. 
   dot_gale  -> ~/.gale
   home_dir  -> ~
   sys_dir   -> etc/gale */
extern struct gale_dir *dot_gale,*home_dir,*sys_dir;

/* (Attempt to) create a directory if it does not exist, with the specified
   name and mode.  Create and return a directory object for that directory. */
struct gale_dir *make_dir(const char *,int mode);

/* Duplicate an existing directory object; make a new one that refers to the
   same place. */
struct gale_dir *dup_dir(struct gale_dir *);
/* Destroy a directory object. */
void free_dir(struct gale_dir *);

/* Walk to a subdirectory (creating it with the specified mode if necessary).
   The directory object now refers to the subdirectory. */
void sub_dir(struct gale_dir *,const char *,int mode);
/* Walk up to the parent directory. */
void up_dir(struct gale_dir *);

/* Construct a filename in the given directory.  Takes a directory object and
   a filename relative to that directory.  It will make sure the filename
   contains no "../" (for safety), glue together the directory's location and
   the filename, and return a pointer to the results.  The pointer is to a
   static location in the directory object, and will be invalidated by the
   next call to dir_file on the same dir object. */
const char *dir_file(struct gale_dir *,const char *);

/* Search for a file in several directories.  Takes the filename, a flag (cwd)
   indicating whether the current directory should be searched as well (1 for
   yes, 0 for no), and a list of directories (end with NULL).  Will return the
   full filename (as from dir_file) if it finds such a file in any of the
   specified directories, NULL otherwise. */
const char *dir_search(const char *,int cwd,struct gale_dir *,...);

/* -- error reporting ------------------------------------------------------ */

/* Types of error severity. */
enum { GALE_NOTICE, GALE_WARNING, GALE_ERROR };

/* Error handler function. */
typedef void gale_error_f(int severity,char *msg);

/* The prefix to prepend to error names -- by default the program name */
extern const char *gale_error_prefix;

/* The error handler to use -- set up to a default by gale_init, but you can
   change this for custom error processing. */
extern gale_error_f *gale_error_handler;

/* Standard error handlers. */
gale_error_f gale_error_syslog,gale_error_stderr;

/* Report an error.  If GALE_ERROR, the program will terminate, otherwise it
   will continue. */
void gale_alert(int severity,const char *,int err);

/* -- debugging support ---------------------------------------------------- */

extern int gale_debug;    /* debugging level (starts out zero) */

/* Debugging printf.  Will only output if gale_debug > level. */
void gale_dprintf(int level,const char *fmt,...);
/* Daemonize (go into the background).  If keep_tty is true (1), don't detach
   from the tty (gsub does this), otherwise do (like most daemons). */
void gale_daemon(int keep_tty);

#endif
