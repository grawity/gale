/* dir.h -- gale directory management stuff */

#ifndef GALE_DIR_H
#define GALE_DIR_H

/* The dir object represents a directory (y'know, in the filesystem). 
   See util.h for some predefined directories. */
struct gale_dir;

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

#endif
