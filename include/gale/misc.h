/** \file
 *  Miscellaneous utility functions.
 *  This is code that doesn't have to do with gale \e per \e se, but which is
 *  generally useful (and, in some cases, required by the API). */

#ifndef GALE_MISC_H
#define GALE_MISC_H

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "gale/types.h"
#include "gale/core.h"
#include "gale/client.h" /* REVIEW */
#include "oop.h"

/** \name Terminal Output */
/*@{*/

/** Flags to control the formatting of gale_print() output. */
enum gale_print_flags {
	/** Highlight text. */
	gale_print_bold = 1,
	/** Output text at the left margin. */
	gale_print_clobber_left = 2,
	/** Erase after the end of the line. */
	gale_print_clobber_right = 4
};

/** Safely output a string to the console. 
 *  \param fp Usually stdout or stderr. 
 *  \param attr Flags to use (any combination of ::gale_print_flags).
 *  \param str The string to output. */
void gale_print(FILE *fp,int attr,struct gale_text str);

/** Write a string to the console and advance to a new line.
 *  \sa gale_print() */
void gale_print_line(FILE *fp,int attr,struct gale_text);

/** \deprecated Use gale_text_width() instead. */
int gale_column(int start,wch);

/** Measure the width of a string, counting doublewidth CJK glyphs.
 *  \param str The string to measure.
 *  \return The number of character positions occupied by the string. */
int gale_text_width(struct gale_text str);

/** Ring the terminal bell.
 *  \param fp Usually stdout or stderr. */
void gale_beep(FILE *fp);

/** Get the width of a terminal. 
 *  \param fp Usually stdout or stderr.
 *  \return The number of character positions per line. */
int gale_columns(FILE *fp);
/*@}*/

/** \name Process Management */
/*@{*/
/** Restart the running program.
 *  Re-exec() this program with the same argc and argv it was originally
    started with.  This function is called automatically on SIGUSR1. */
void gale_restart(void);

/** Run a subprogram.
 *  Create a subprocess and execute the specified program.  If \a in or
 *  \a out is not NULL, a pipe will be established to the the process' 
 *  standard input or standard output, respectively.  If the program cannot
 *  be found or executed and a \a fallback function is supplied, it will
 *  be called instead with the argument list supplied.
 *  \param prog The name of the program to execute (will search $PATH).
 *  \param argv The arguments to use (including argv[0] and terminated by NULL).
 *  \param in If non-NULL, receives a pipe file descriptor open for writing.
 *  \param out If non-NULL, receives a pipe file descriptor open for reading.
 *  \param fallback If non-NULL, function to call if the program can't be found.
 *  \return The process ID of the subprogram.
 *  \sa Make sure to call gale_wait(), or you'll get zombies.
 *  \todo Change the argument types to gale_text. */
pid_t gale_exec(const char *prog,char * const *argv,int *in,int *out,
                void (*fallback)(char * const *));

/** Wait for a subprogram to exit.
 *  Make sure to call this after using gale_exec(), or you'll get zombies. 
 *  \param pid The process to wait for.
 *  \return The process' return code. */
int gale_wait(pid_t pid);

/** There can be only one.
 *  If do_kill is nonzero, look for other processes of the same type with
 *  the same \a class and kill them.  In any case, register ourselves (with
 *  temp files in ~/.gale) so we can be killed when our time has come.
 *  \param class The "group" to join.  Usually the terminal name.
 *  \param do_kill Nonzero to kill all other processes in this group. */
void gale_kill(struct gale_text class,int do_kill);

/** Register a cleanup function.
 *  The \a cleanup function will be called, if at all possible, when the
 *  program exits (normally or via most signals). 
 *  \param cleanup The function to call on exit. */
void gale_cleanup(void (*cleanup)(void *),void *);

/** Call cleanup functions.
 *  Perform all cleanup functions "early".  Call this if you're anticipating
 *  abrupt termination. */
void gale_do_cleanup();

/** Deathpolling.
 *  Watch the file descriptor \a fd (which should be a terminal).
 *  If !isatty(fd) ever fails, raise(SIGHUP). 
 *  \param oop The liboop event source to use for polling.
 *  \param fd The file descriptor to watch (usually 1). */
void gale_watch_tty(oop_source *oop,int fd);

void gale_daemon(oop_source *);
void gale_detach();
/*@}*/

/** \name Memory Management */
/*@{*/

/** The zero-length block of memory. */
extern const struct gale_data null_data;

/** Allocate memory. 
 *  \sa ::gale_create, ::gale_create_array */
void *gale_malloc(size_t size);
/** Allocate memory that will never contain pointers. */
void *gale_malloc_atomic(size_t size);
/** Allocate memory that will never be garbage-collected. */
void *gale_malloc_safe(size_t size);
/** Resize a block of memory.
 *  \param mem The original block of memory.
 *  \param len The new size.
 *  \return The new location of the memory.
 *  \sa ::gale_resize_array */
void *gale_realloc(void *mem,size_t len);
/** Free a block of memory.
 *  You should not need to call this except for memory obtained with
 *  gale_malloc_safe(). */
void gale_free(void *);

/** Register a finalizer to be called when memory is garbage collected.
 *  \param ptr The memory block to watch.
 *  \param fun The function to call when the memory is about to be collected.
 *  \param user An extra pointer to pass to the finalizer function. */
void gale_finalizer(void *ptr,void (*fun)(void *ptr,void *user),void *user);

/** Check the heap.
 *  For debugging.  Call this if you're worried the heap might be corrupt. */
void gale_check_mem(void);

/** Allocate an object.
 *  \param x Uninitialized pointer to an object (will be set).
 *  \sa gale_malloc() */
#define gale_create(x) ((x) = gale_malloc(sizeof(*(x))))
/** Allocate an array of objects.
 *  \param x Uninitialized pointer to an array of objects (will be set).
 *  \param count The length of the array (in objects).
 *  \sa gale_malloc() */
#define gale_create_array(x,count) ((x) = gale_malloc(sizeof(*(x)) * (count)))
/** Resize an array of objects.
 *  \param x Pointer to an array of objects (will be modified in place).
 *  \param count The new length of the array (in objects).
 *  \sa gale_realloc() */
#define gale_resize_array(x,count) ((x) = gale_realloc(x,sizeof(*(x)) * (count)))

/** Duplicate a region of memory. 
 *  \param data Memory block to copy.
 *  \return A fresh copy of \a data. */
struct gale_data gale_data_copy(struct gale_data data);

/** Compare two memory blocks, a la memcmp().
 *  \return Less than zero if \a a \< \a b, zero if \a a == \a b, or
 *  greater than zero if \a a \> \a b. */
int gale_data_compare(struct gale_data a,struct gale_data b);

struct gale_ptr;

/** Create a weak pointer.
 *  A 'weak pointer' doesn't prevent the object it points to from being
 *  garbage-collected.  The weak pointer is set to NULL when the object 
 *  it points to goes away.
 *  \param ptr The object to create a weak pointer to.
 *  \return A weak pointer to the object. 
 *  \sa gale_get_ptr() */
struct gale_ptr *gale_make_weak(void *ptr);

/** Create a normal pointer that looks like a weak pointer.
 *  This is useful to allow code which can operate on weak pointers or
 *  normal pointers.
 *  \param ptr An ordinary pointer to the object. 
 *  \return A pointer to the object that looks like a weak pointer. 
 *  \sa gale_make_weak(), gale_get_ptr() */
struct gale_ptr *gale_make_ptr(void *);

/** Dereference a weak pointer.
 *  \param wp A weak pointer.
 *  \return A pointer to the object referenced by the weak pointer, or
 *          NULL if the object has been garbage-collected. 
 *  \sa gale_make_weak() */
void *gale_get_ptr(struct gale_ptr *wp);
/*@}*/

/** \name String Processing 
 *  Functions for manipulating ::gale_text objects.
 *  Strings in Gale are represented by constant, garbage-collected arrays
 *  of wchar_t.  The functions here create and manipulate these arrays. */
/*@{*/

/** The empty string. */
extern const struct gale_text null_text;
struct gale_encoding;

/** Use this macro for literal Unicode text strings.
 *  \code struct gale_text foostr = G_("foo"); \endcode */
#define G_(x) (_gale_text_literal(L##x,sizeof(L##x) / sizeof(wch) - 1))

/** \internal */
struct gale_text _gale_text_literal(const wchar_t *,size_t len); /* internal */

/** Concatenate text strings.  
 *  The first argument \a count is the number of strings passed.
 *  \param count The number of strings to concatenate.
 *  \return The concatenated string.
 *  \sa gale_text_concat_array()
 *  \code 
 *  struct gale_text stuff = G_("hi hi");
 *  struct gale_text foobar = gale_text_concat(3,G_("foo ["),stuff,G_("] bar"));
 *  assert(0 == gale_text_compare(foobar,G_("foo [hi hi] bar")));
 *  \endcode */
struct gale_text gale_text_concat(int count,...);

/** Concatenate an array of text strings.
 *  \param count The number of members in \a array.
 *  \param array The strings to concatenate, in order.
 *  \return The concatenated string.
 *  \sa gale_text_concat() */
struct gale_text gale_text_concat_array(int count,struct gale_text *array);

/** Extract the leftmost substring of \a len characters.  
 *  If \a len is larger than the length of the string, the entire string 
 *  is returned.  If \a len is negative, all but the rightmost \a -len 
 *  characters are returned.  If \a -len is larger than the length of the 
 *  string, the empty string is returned. 
 *  \param str The string to extract from.
 *  \param len The number of characters to extract. 
 *  \return The leftmost \a len characters from \a str. */
struct gale_text gale_text_left(struct gale_text,int len);

/** Extract the rightmost substring of \a len characters.  
 *  If \a len is larger than the length of the string, the entire string 
 *  is returned.  If \a len is negative, all but the leftmost \a -len 
 *  characters are returned.  If \a -len is larger than the length of the 
 *  string, the empty string is returned. 
 *  \param str The string to extract from.
 *  \param len The number of characters to extract. 
 *  \return The rightmost \a len characters from \a str. */
struct gale_text gale_text_right(struct gale_text,int len);

/** Divide a string into tokens using the separator character 'sep'.
 *  Set \a token to null_text initially, then call
 *  gale_text_token(str,sep,&token) repeatedly.  The function will return a
 *  nonzero value as long as there is an additional token, and set \a token 
 *  to the contents of that token.  A string with N occurrences of the
 *  separator character contains 1+N tokens.
 *
 *  This example will output 'foo', 'bar', and 'bat':
 *  \code
 *  struct gale_text str = G_("foo bar bat"),token = null_text;
 *  while (gale_text_token(str,' ',&token)) gale_print_line(stdout,0,token);
 *  \endcode
 *  \param string The string to tokenize.
 *  \param sep The separator character to use.
 *  \param token Pointer to the variable to receive the next token.
 *  \return Nonzero if a token was returned, or zero if there are no more. */
int gale_text_token(struct gale_text string,wch sep,struct gale_text *token);

/** Compare two strings, a la strcmp().
 *  \return Less than zero if \a a \< \a b, zero if \a a == \a b, or
 *  greater than zero if \a a \> \a b. */
int gale_text_compare(struct gale_text a,struct gale_text b);

/** Parse a number.  
 *  \param str The string to parse.
 *  \return The number in \a str, or zero if unsuccessful. 
 *  \sa gale_text_from_number() */
int gale_text_to_number(struct gale_text str);

/** Create a text representation of a number.  
 *  \param n The number to format.
 *  \param base The numeric base to use (0 < \a base <= 36).
 *  \param pad The minimum field width.  Zeroes will be added if necessary.
 *  \return The formatted representation of this number. 
 *  \sa gale_text_to_number() */
struct gale_text gale_text_from_number(int n,int base,int pad);

/** View the contexts of a text string as opaque binary data.
 *  This is typically used with gale_map structures. 
 *  \sa gale_text_from_data() */
struct gale_data gale_text_as_data(struct gale_text);

/** Convert opaque binary data back into a text string. 
 *  \sa gale_text_as_data() */
struct gale_text gale_text_from_data(struct gale_data);

/** Initialize a character encoding translator.
 *  We use the iconv library, so \a enc can be any encoding supported by
 *  iconv ("UTF-8", "ISO-8859-1", "BIG5", etc). 
 *  Normally, you don't create gale_encoding objects yourself, but use the
 *  ones in ::gale_global (see globals.h). 
 *  \sa gale_text_from(), gale_text_to() */
struct gale_encoding *gale_make_encoding(struct gale_text enc);

/** Convert some character encoding into a Unicode string.
 *  \param enc The character encoding to use.
 *  \param str The string to convert.
 *  \param len The length of the string, or -1 if NUL-terminated. 
 *  \return The string in Unicode format.
 *  \sa gale_make_encoding(), gale_text_to() */
struct gale_text gale_text_from(struct gale_encoding *enc,const char *str,int len);

/** Convert a Unicode string to some character encoding.
 *  \param enc The character encoding to use.
 *  \param str The Unicode string to convert.
 *  \return The encoded, NUL-terminated string.
 *  \sa gale_make_encoding(), gale_text_from() */
char *gale_text_to(struct gale_encoding *enc,struct gale_text str);
/*@}*/

/** \name Map */
/*@{*/

struct gale_map;

/** Create a key-value lookup table.
 *  \param weak If nonzero, create a 'weak map' using weak pointers.
 *  Objects in a weak map will not be kept alive by the map alone.
 *  If they are garbage collected, they will be removed from the map.
 *  \return The new, empty map.
 *  \sa gale_map_add(), gale_map_find(), gale_map_walk() */
struct gale_map *gale_make_map(int weak);

/** Add a key-value pair to a map.
 *  \param map The map to add to.
 *  \param key The key to use.  If another entry exists with the same key,
 *  it will be replaced by this entry.
 *  \param data The data value to add.  If \a data is NULL, any entry
 *  with this \a key will be removed from the map. */
void gale_map_add(struct gale_map *map,struct gale_data key,void *data);

/** Find an entry by its key.
 *  \param map The map to search.
 *  \param key The key to look for.
 *  \return The data associated with that key, or NULL if none was found. */
void *gale_map_find(struct gale_map *map,struct gale_data key);

/** Traverse a map in order.
 *  This function finds the first key/data entry where key > \a *after.
 *  If \a after is NULL, it finds the first entry in the map.
 *  This example counts the number of keys in a map:
 *  \code
 *  struct gale_map *map = ...;
 *  struct gale_data key = null_data;
 *  int count = 0;
 *  while (gale_map_walk(map,&key,&key,NULL)) ++count;
 *  \endcode
 *  \param map The map to traverse.
 *  \param after The key to search for.
 *  \param key Pointer to a variable to return the entry's key in.
 *  \param data Pointer to a variable to return the entry's data in.
 *  \return Nonzero if an entry was found, zero otherwise. */
int gale_map_walk(struct gale_map *map,const struct gale_data *after,
                  struct gale_data *key,void **data);
/*@}*/

/** \name Time Processing */
/*@{*/
struct timeval; /* from Unix */

/** The "beginning of time". */
struct gale_time gale_time_zero(void);
/** The current time. */
struct gale_time gale_time_now(void);
/** The "end of time". */
struct gale_time gale_time_forever(void);
/** A time representing \a sec seconds from the epoch. */
struct gale_time gale_time_seconds(int sec);

/** Compare two time values.
 *  \return Less than zero if \a a \< \a b, zero if \a a == \a b, or
 *  greater than zero if \a a \> \a b. */
int gale_time_compare(struct gale_time a,struct gale_time b);

/** Subtract two time values.
 *  \return A relative time value representing \a a - \a b. */
struct gale_time gale_time_diff(struct gale_time a,struct gale_time b);

/** Add two time values.
 *  \return A time value representing \a a + \a b. */
struct gale_time gale_time_add(struct gale_time a,struct gale_time b);

/** Convert a time value to the Unix timeval structure.
 *  \param tv An uninitialized timeval structure to write.
 *  \param time The time value to convert. */
void gale_time_to(struct timeval *tv,struct gale_time time);

/** Convert a Unix timeval structure to a time value.
 *  \param time An uninitialized time value to write.
 *  \param tv The timeval structure to convert. */
void gale_time_from(struct gale_time *time,const struct timeval *tv);
/*@}*/

/** \name Fragment and Group Management 
 *  Functions for manipulating ::gale_group and ::gale_fragment objects.
 *  Gale messages (and other data structures) are built out of ::gale_fragment
 *  objects (which are named values) and ::gale_group objects (which are
 *  collections of fragments).  These are similar to LISP 'alists'. */
/*@{*/

/** Return the empty group. */
struct gale_group gale_group_empty(void);

/** Add a new fragment to an existing group.
 *  \param group Pointer to the group to expand.
 *  \param frag Fragment to add to the group.
 *  \sa gale_group_replace() */
void gale_group_add(struct gale_group *group,struct gale_fragment frag);

/** Concatenate two groups.
 *  \param a Pointer to the group to incorporate the other group.
 *  \param b Group containing fragments to add to the other group. */
void gale_group_append(struct gale_group *a,struct gale_group b);

/** You probably don't need this, use gale_group_lookup() instead. */
struct gale_group gale_group_find(struct gale_group,struct gale_text name);

/** Remove a fragment from a group.
 *  \param group Pointer to the group to be altered.
 *  \param name Name of the fragment to remove.
 *  \return Nonzero if any fragments matched the name (and were removed). */
int gale_group_remove(struct gale_group *group,struct gale_text name);

/** Add a fragment to an existing group, replacing any others with the 
 *  same name.
 *  \param group Pointer to the group to be altered.
 *  \param frag Fragment to add to the group. 
 *  \sa gale_group_add() */
void gale_group_replace(struct gale_group *group,struct gale_fragment frag);

/** Return nonzero if the specified group is empty. */
int gale_group_null(struct gale_group);

/** Return the first fragment in the specified group.  (LISP 'car'.) */
struct gale_fragment gale_group_first(struct gale_group);

/** Return the group consisting of all the fragments except the first.
 *  (LISP 'cdr'.) */
struct gale_group gale_group_rest(struct gale_group);

/** Do some mysterious thing. */
void gale_group_prefix(struct gale_group *,struct gale_group tail);

/** Find a fragment in a group by name.
 *  \param group The group to search.
 *  \param name The name of the fragment to find.
 *  \param type The type of the fragment to find.
 *  \param frag Pointer to an uninitialized fragment.
 *  \return Nonzero if the fragment was found (and stored in \a frag). */
int gale_group_lookup(
	struct gale_group group,struct gale_text name,
        enum gale_fragment_type type,
	struct gale_fragment *frag);

/** Return a human-readable version of the fragment (for debugging). */
struct gale_text gale_print_fragment(struct gale_fragment,int indent);

/** Return a human-readable version of the group (for debugging). */
struct gale_text gale_print_group(struct gale_group,int indent);

/** Compare two fragments.
 *  \return Less than zero if \a a \< \a b, zero if \a a == \a b, or
 *  greater than zero if \a a \> \a b. */
int gale_fragment_compare(struct gale_fragment a,struct gale_fragment b);

/** Compare two groups.
 *  \return Less than zero if \a a \< \a b, zero if \a a == \a b, or
 *  greater than zero if \a a \> \a b. */
int gale_group_compare(struct gale_group a,struct gale_group b);
/*@}*/

/** \name Data Serialization 
 *  Pack and unpack data structures.
 *  Most libgale clients will never have to use these functions directly.
 *  They are used to serialize and deserialize data from binary streams
 *  (mostly to support network protocols).  For a particular data encoding 
 *  "type", there are usually three functions defined:
 *  - int gale_unpack_type(struct gale_data *,type *)
 *  - void gale_pack_type(struct gale_data *,type);
 *  - size_t gale_type_size(type)
 * 
 *  The gale_unpack_type function converts binary data into the specified
 *  type, modifying the ::gale_data structure to start at the end of the
 *  decoded data, returning nonzero iff the operation was successful.  
 *  The gale_pack_type function does the opposite, converting
 *  the specified type into binary data.  Space must be preallocated in the
 *  ::gale_data structure for the data; the gale_type_size function supplies
 *  a conservative (but usually accurate) estimate of the amount of space
 *  needed to store a particular value. */
/*@{*/
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

int gale_unpack_text(struct gale_data *,struct gale_text *);
void gale_pack_text(struct gale_data *,struct gale_text);
#define gale_text_size(t) (gale_text_len_size(t) + gale_u32_size())

int gale_unpack_text_len(struct gale_data *,size_t len,
                         /*in,out*/ struct gale_text *);
void gale_pack_text_len(struct gale_data *,struct gale_text);
#define gale_text_len_size(t) ((t).l * gale_wch_size())

int gale_unpack_time(struct gale_data *,struct gale_time *);
void gale_pack_time(struct gale_data *,struct gale_time);
#define gale_time_size() (sizeof(u32) * 4)

int gale_unpack_fragment(struct gale_data *,struct gale_fragment *);
void gale_pack_fragment(struct gale_data *,struct gale_fragment);
size_t gale_fragment_size(struct gale_fragment);

int gale_unpack_group(struct gale_data *,struct gale_group *);
void gale_pack_group(struct gale_data *,struct gale_group);
size_t gale_group_size(struct gale_group);
/*@}*/

/** \name File and Directory Manipulation */
/*@{*/
/* global.h has these preinitialized pathnames, set by gale_init. 
   dot_gale  -> ~/.gale
   home_dir  -> ~
   sys_dir   -> etc/gale */

void make_dir(struct gale_text path,int mode);
struct gale_text sub_dir(struct gale_text path,struct gale_text sub,int mode);
struct gale_text up_dir(struct gale_text path);
struct gale_text dir_file(struct gale_text path,struct gale_text file);
struct gale_text dir_search(struct gale_text name,int cwd,struct gale_text,...);

struct gale_data gale_read_from(int fd,int size_limit);
int gale_write_to(int fd,struct gale_data data);

struct gale_file_state;
struct gale_data gale_read_file(
	struct gale_text name,
	int size_limit,int do_paranoia,
	struct gale_file_state **state);
int gale_write_file(
	struct gale_text name,
	struct gale_data data,
	int is_private,
	struct gale_file_state **state);

int gale_erase_file(const struct gale_file_state *state);
int gale_file_changed(const struct gale_file_state *since);

/*@}*/

/** \name Error Reporting */
/*@{*/
/** Degree of error severity for gale_alert(). */
enum gale_error { GALE_NOTICE, GALE_WARNING, GALE_ERROR };

/** Report an error; terminate if \a severity is GALE_ERROR.
 *  \param severity The severity of the error (from ::gale_error).
 *  \param str The error message to report.
 *  \param err If nonzero, a system errno value to look up. */
void gale_alert(int severity,struct gale_text str,int err);

/** Function type for a user-defined error handler. 
 *  \param severity The severity of the error.
 *  \param str The error message.
 *  \param user A user-defined parameter.
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_on_error() */
typedef void *gale_call_error(int severity,struct gale_text msg,void *user);

struct old_gale_message;

/** Function type for a user-defined error handler that accepts a
 *  formatted ::old_gale_message. 
 *  \param oop The liboop source used for dispatch.
 *  \param puff The ::old_gale_message containing the error.
 *  \param user A user-defined parameter.
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_on_error_message() */
typedef void *gale_call_error_message(oop_source *oop,struct old_gale_message *puff,void *user);

/** Set a different error handler. 
 *  The function \a func will be called when an error is reported. 
 *  \param oop The liboop source used for dispatch.
 *  \param func The function to call when there's an error.
 *  \param user The user-defined parameter to pass the function. */
void gale_on_error(oop_source *oop,gale_call_error *func,void *user);

/** Set an error handler that accepts a formatted ::old_gale_message.
 *  The function \a func will be called when an error is reported. 
 *  \param oop The liboop source used for dispatch.
 *  \param func The function to call when there's an error.
 *  \param user The user-defined parameter to pass the function. */
void gale_on_error_message(oop_source *oop,gale_call_error_message *func,void *user);

/** Transmit error messages on the specified ::gale_link.
 *  \param oop The liboop source used for dispatch.
 *  \param link The link to transmit error messages on. */
void gale_set_error_link(oop_source *oop,struct gale_link *link);

/** Format a ::old_gale_message from error text. 
 *  \param body The message to report.
 *  \return A ::old_gale_message containing the specified error message. */
struct old_gale_message *gale_error_message(struct gale_text body);
/*@}*/

/** \name Debugging Support */
/*@{*/
struct gale_report;

/** Function type for a report generator.
 *  Report generators are used to provide dumps of the internal
 *  state of the system for debugging purposes.
 *  \param user A user-defined parameter. 
 *  \return An internal state dump. */
typedef struct gale_text gale_report_call(void *user);

/** Create a new report container.  
 *  This report container is used to create a logically grouped "subreport"
 *  of another container.  The outermost report container is defined in 
 *  global.h. 
 *  \param outer The parent report container.
 *  \return The new report container. */
struct gale_report *gale_make_report(struct gale_report *outer);

/** Add a report handler to a report container.
 *  \param outer The report container.
 *  \param func The report generator function.
 *  \param user The user-defined parameter to pass to the generator. 
 *  \sa gale_report_remove() */
void gale_report_add(struct gale_report *outer,gale_report_call *func,void *user);

/** Remove a report handler from a report container.
 *  \param outer The report container.
 *  \param func The report generator function.
 *  \param user The user-defined parameter specified to pass to the generator. 
 *  \sa gale_report_add() */
void gale_report_remove(struct gale_report *,gale_report_call *,void *);

/** Generate a report from all the report generators registered with a
 *  given report container.  This is usually called automatically by SIGUSR2
 *  and used to write a "report" file in ~/.gale. 
 *  \param outer The report container. 
 *  \return The full report text. */
struct gale_text gale_report_run(struct gale_report *);

/** Debugging printf.  Will only output if \a gale_debug > \a level. 
 *  \todo Fix this to be less gross! */
void gale_dprintf(int level,const char *fmt,...);

/** Debugging printf.  Will add \a indent levels of indentation.
 *  \todo Fix this to be less gross! */
void gale_diprintf(int level,int indent,const char *fmt,...);
/*@}*/

/** \name Connection Management
 *  Asynchronous TCP connection management for liboop. */
/*@{*/

/** Callback when a connection completes. 
 *  \param fd File descriptor of completed connection (-1 if failed).
 *  \param hostname Name of remote host. 
 *  \param addr IP address of remote host.
 *  \param found_local Nonzero if any of the specified addresses were local.
 *  \param user User-supplied parameter. 
 *  \return Liboop continuation code (usually OOP_CONTINUE). */
typedef void *gale_connect_call(int fd,
	struct gale_text hostname,struct sockaddr_in addr,
	int found_local,void *user);

struct gale_text gale_connect_text(struct gale_text host,struct sockaddr_in);

struct gale_connect;
struct gale_connect *gale_make_connect(
	oop_source *source,struct gale_text host,int avoid_local_port,
	gale_connect_call *,void *);

void gale_abort_connect(struct gale_connect *);
/*@}*/

#endif
