#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

#include "gale/all.h"

/** Safely construct a pathname from a directory and a filename.
 *  This function combines the directory \a path and the filename \a file.
 *  This would be a simple concatenation (with a "/"), except that we also
 *  make sure that \a file doesn't contain any backreferences ("../").
 *  This makes it safe to construct filenames from network data in a
 *  directory "sandbox".
 *  \param path The directory to contain the file.
 *  \param file The filename relative to \a path.
 *  \return The full name of the file. */
struct gale_text dir_file(struct gale_text path,struct gale_text file) {
	struct gale_text r = null_text,part = null_text;
	if (0 == path.l) return file;

	while (gale_text_token(file,'/',&part)) {
		if (part.p + part.l < file.p + file.l) ++part.l;
		if (gale_text_compare(part,G_(".."))
		&&  gale_text_compare(part,G_("../")))
			r = gale_text_concat(2,r,part);
		else {
			gale_alert(GALE_WARNING,
			           G_("replaced .. with __ in filename"),0);
			r = gale_text_concat(3,r,G_("__"),
				gale_text_right(part,-2));
		}
		if ('/' == part.p[part.l - 1]) --part.l;
	}

	return gale_text_concat(3,path,G_("/"),r);
}

/** Search for a file in a list of directories.
 *  This function searches for a file named \a fn relative to each of the
 *  directories supplied as arguments.  (If \a f is nonzero, the current
 *  directory is also searched.)  If the file is found, its full pathname
 *  is returned.
 *  \param fn The filename to search for.
 *  \param f Nonzero to search the current directory 
 *           (and allow absolute filenames).
 *  \param t List of directories to search, terminated by ::null_text.
 *  \return The full pathname of the file, if it was found; ::null_text 
 *          otherwise. */
struct gale_text dir_search(struct gale_text fn,int f,struct gale_text t,...) {
	va_list ap;
	struct gale_text r = null_text;

	if (fn.l > 0 && fn.p[0] == '/') {
		if (access(gale_text_to(gale_global->enc_filesys,fn),F_OK)) 
			return null_text;
		else
			return fn;
	}

	if (f && !access(gale_text_to(gale_global->enc_filesys,fn),F_OK))
		return fn;

	va_start(ap,t);
	while (0 == r.l && 0 != t.l) {
		r = dir_file(t,fn);
		if (access(gale_text_to(gale_global->enc_filesys,r),F_OK)) 
			r.l = 0;
		t = va_arg(ap,struct gale_text);
	}
	va_end(ap);
	return r;
}

/** Create a directory.
 *  \param path Name of directory to create.
 *  \param mode Permissions to use (usually 0777) */
void make_dir(struct gale_text path,int mode) {
	struct stat buf;
	if (stat(gale_text_to(gale_global->enc_filesys,path),&buf) 
	|| !S_ISDIR(buf.st_mode))
		if (mode 
		&&  mkdir(gale_text_to(gale_global->enc_filesys,path),mode))
			gale_alert(GALE_WARNING,path,errno);
}

/** Find a subdirectory.
 *  Look for subdirectory \a sub of parent directory \a path, and create the
 *  subdirectory if it does not already exist.  
 *  \param path Parent directory to search.
 *  \param sub Subdirectory to find or create.
 *  \return The full name of the subdirectory. */
struct gale_text sub_dir(struct gale_text path,struct gale_text sub,int mode) {
	struct stat buf;
	struct gale_text ret = dir_file(path,sub);
	if ((stat(gale_text_to(gale_global->enc_filesys,ret),&buf) 
	|| !S_ISDIR(buf.st_mode)))
		if (mkdir(gale_text_to(gale_global->enc_filesys,ret),mode))
			gale_alert(GALE_WARNING,ret,errno);
	return ret;
}

/** Return the parent directory.
 *  \param path Name of a directory or file.
 *  \return The parent directory of \a path. */
struct gale_text up_dir(struct gale_text path) {
	while (path.l > 1 && path.p[--path.l] != '/') ;
	return path;
}
