#include "gale/crypto.h"
#include "gale/misc.h"
#include "gale/globals.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <utime.h>

struct gale_file_state {
	dev_t device;
	ino_t inode;
	struct gale_text name;
	time_t inode_time,file_time;
	off_t file_size;
};

/** Read data from a file descriptor.
 *  \param fd UNIX file descriptor to read from.
 *  \param size_limit Maximum size to read, or 0 for no limit.
 *  \return As much data as we could suck up.
 *  \sa gale_write_to(), gale_read_file() */
struct gale_data gale_read_from(int fd,int size_limit) {
	struct stat st;
	struct gale_data output;
	static int initial_size = 0;
	int alloc;

	if (!fstat(fd,&st)) initial_size = st.st_size;
	if (initial_size <= 0) initial_size = 1024;
	if (initial_size > size_limit && size_limit > 0) 
		initial_size = 1 + size_limit;

	output.p = gale_malloc((alloc = initial_size));
	output.l = 0;

	for (;;) {
		ssize_t r;
		do r = read(fd,output.p + output.l,alloc - output.l);
		while (r < 0 && EINTR == errno);

		if (0 == r) return output;
		if (0 > r) {
			gale_alert(GALE_WARNING,G_("cannot read file"),errno);
			return output;
		}

		output.l += r;
		if (output.l > size_limit && size_limit > 0) {
			gale_alert(GALE_WARNING,G_("file exceeds size limit"),0);
			output.l = size_limit;
			return output;
		}

		if (output.l == alloc)
			output.p = gale_realloc(output.p,alloc *= 2);
	}
}

/** Write data to a file descriptor.
 *  \param fd UNIX file descriptor to write to.
 *  \param data Data to write.
 *  \return Nonzero iff all the data was successfully written. 
 *  \sa gale_read_from(), gale_write_file() */
int gale_write_to(int fd,struct gale_data data) {
	while (data.l != 0) {
		ssize_t r;
		do r = write(fd,data.p,data.l);
		while (r < 0 && errno == EINTR);
		if (r <= 0) {
			gale_alert(GALE_WARNING,G_("cannot write file"),errno);
			return 0;
		}
		data.p += r;
		data.l -= r;
	}

	return 1;
}

void create(
	struct gale_file_state **state,
	struct stat *buf,struct gale_text name) 
{
	if (NULL == state) return;
	gale_create(*state);
	(*state)->name = name;
	(*state)->device = buf->st_dev;
	(*state)->inode = buf->st_ino;
	(*state)->inode_time = buf->st_ctime;
	(*state)->file_time = buf->st_mtime;
	(*state)->file_size = buf->st_size;
}

/** Read data from a disk file.
 *  \param name Filename to read.
 *  \param size_limit Maximum size to read, or 0 for no limit.
 *  \param do_paranoia If nonzero, will refuse to read linked files.
 *  \param state If not NULL, will store state information about the file. 
 *  \return As much data as we could suck up. 
 *  \sa gale_read_from(), gale_write_file() */
struct gale_data gale_read_file(
	struct gale_text name,
	int size_limit,int do_paranoia,
	struct gale_file_state **state) 
{
	struct stat buf;
	struct gale_data output = null_data;
	char *sz = gale_text_to(gale_global->enc_filesys,name);
	int fd = -1;

	if (NULL != state) *state = NULL;

	do fd = open(sz,O_RDONLY);
	while (fd < 0 && EINTR == errno);
	if (fd < 0) goto cleanup;

	if (do_paranoia || NULL != state) {
		if (lstat(sz,&buf)) {
			gale_alert(GALE_WARNING,name,errno);
			goto cleanup;
		}
		create(state,&buf,name);
	}

	if (do_paranoia) {
		struct stat fbuf;

		if (fstat(fd,&fbuf)) {
			gale_alert(GALE_WARNING,name,errno);
			goto cleanup;
		}
		if (buf.st_dev != fbuf.st_dev || buf.st_ino != fbuf.st_ino) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("\""),name,G_("\": symlink ignored")),0);
			goto cleanup;
		}
		if (1 != buf.st_nlink) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("\""),name,G_("\": hard link ignored")),0);
			goto cleanup;
		}
		if (!S_ISREG(buf.st_mode)) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("\""),name,G_("\": weird file ignored")),0);
			goto cleanup;
		}
	}

	output = gale_read_from(fd,size_limit);

cleanup:
	if (fd >= 0) close(fd);
	return output;
}

static struct gale_text temp_name(struct gale_text name) {
	struct gale_data data;
	struct gale_text temp;
	wch *crap;
	int i;

	data = gale_crypto_random(8);
	gale_create_array(crap,2*data.l);
	for (i = 0; i < data.l; ++i) {
		crap[2*i + 0] = 'a' + (data.p[i] >> 4);
		crap[2*i + 1] = 'z' - (data.p[i] & 0xF);
	}

	temp.p = crap;
	temp.l = 2*data.l;
	while (name.l > 0 && name.p[name.l - 1] != '/') --name.l;
	return gale_text_concat(3,name,G_("tmp."),temp);
}

/** Write data to a disk file.
 *  \param name Filename to write.
 *  \param data Data to write to the file.
 *  \param is_private If nonzero, turn off public read access.
 *  \param state If not NULL, will store state information about the file. 
 *  \return Nonzero iff all the data was successfully written.
 *  \sa gale_write_to(), gale_read_file() */
int gale_write_file(
	struct gale_text name,
	struct gale_data data,
	int is_private,
	struct gale_file_state **state)
{
	const char *sztemp;
	int fd;

	if (NULL != state) *state = NULL;
	sztemp = gale_text_to(gale_global->enc_filesys,temp_name(name));

	do fd = open(sztemp,O_WRONLY | O_CREAT | O_EXCL,0600);
	while (fd < 0 && EINTR == errno);
	if (fd < 0) {
		gale_alert(GALE_WARNING,
			gale_text_from(gale_global->enc_filesys,sztemp,-1),
			errno);
		return 0;
	}

	if (!gale_write_to(fd,data)) {
		close(fd);
		return 0;
	}

	if (NULL != state) {
		struct stat buf;
		if (fstat(fd,&buf)) {
			gale_alert(GALE_WARNING,G_("fstat"),errno);
			close(fd);
			return 0;
		}
		create(state,&buf,name);
	}

	if (fchmod(fd,is_private ? 0600 : 0644)) {
		gale_alert(GALE_WARNING,G_("fchmod"),errno);
		close(fd);
		return 0;
	}

	close(fd);

	if (rename(sztemp,gale_text_to(gale_global->enc_filesys,name))) {
		gale_alert(GALE_WARNING,name,errno);
		unlink(sztemp);
		return 0;
	}

	return 1;
}

static int compare(const struct stat *buf,const struct gale_file_state *since) {
	return NULL == since
	    || buf->st_dev != since->device || buf->st_ino != since->inode
	    || buf->st_mtime != since->file_time
	    || buf->st_size != since->file_size;
}

/** Erase a disk file.
 *  \param which State information previously collected by
 *         gale_read_file() or gale_write_file().
 *  \return Nonzero if the file has not changed and was successfully erased. 
 *  \sa gale_file_changed() */
int gale_erase_file(const struct gale_file_state *which) {
	struct stat buf;
	const char *szname,*sztemp;
	errno = 0;

	/* This introduces a race condition, but it's hard to avoid that.
	   We try our best below... */
	if (NULL == which || gale_file_changed(which)) return 0;

	szname = gale_text_to(gale_global->enc_filesys,which->name);
	if ('\0' == *szname) return 0;

	sztemp = gale_text_to(gale_global->enc_filesys,temp_name(which->name));
	if (rename(szname,sztemp)) return 0;

	if (!lstat(sztemp,&buf) && !compare(&buf,which))
		return !unlink(sztemp);

	link(sztemp,szname);
	unlink(sztemp);
	return 0;
}

/** Check to see if a disk file has changed.
 *  \param since State information previously collected by
 *         gale_read_file() or gale_write_file().
 *  \return Nonzero iff the file has changed since then. */
int gale_file_changed(const struct gale_file_state *since) {
	struct stat buf;
	if (stat(gale_text_to(gale_global->enc_filesys,since->name),&buf)) 
		return NULL == since;
	else
		return compare(&buf,since);
}

/** Return a file's timestamp.
 *  \param state State information previously collected by
 *         gale_read_file() or gale_write_file().
 *  \return The last observed modification time of the file. */ 
struct gale_time gale_get_file_time(const struct gale_file_state *which) {
	struct timeval tv = { 0, 0 };
	struct gale_time output;
	tv.tv_sec = which->file_time;
	gale_time_from(&output,&tv);
	return output;
}

/** Set a file's timestamp.
 *  \param state State information previously collected by
 *         gale_read_file() or gale_write_file().
 *  \param time Updated timestamp. */
void gale_set_file_time(struct gale_file_state *which,struct gale_time time) {
	const char *name;
	struct utimbuf ut;

	if (NULL == which) return;

	{
		struct timeval tv;
		gale_time_to(&tv,time);
		ut.actime = ut.modtime = tv.tv_sec;
	}

	name = gale_text_to(gale_global->enc_filesys,which->name);

	{
		/* Race condition; but we can't do much with the Unix API. */
		struct stat buf;
		if (stat(name,&buf) || compare(&buf,which)) return;
		if (!utime(name,&ut) && !stat(name,&buf)) {
			which->file_time = buf.st_mtime;
			which->inode_time = buf.st_ctime;
			return;
		}
	}

	{
		/* That failed; now try making a copy. */
		struct stat buf;
		struct gale_file_state *state;
		struct gale_data copy = gale_read_file(
			which->name,which->file_size,1,&state);

		/* Yet another race condition, sigh. */
		if (copy.l != which->file_size || stat(name,&buf) 
		||  compare(&buf,which)
		||  compare(&buf,state)) return;
		if (gale_write_file(which->name,copy,
			!(buf.st_mode & S_IROTH),&state))
		{
			*which = *state;
			return;
		}
	}
}
