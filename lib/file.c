#include "file.h"
#include "random.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#define MAX_SIZE 65536

static struct gale_text make_temp(struct gale_text path) {
	struct gale_text temp;
	byte buf[8];
	wch *crap;
	struct gale_data data;
	int i;

	data.p = buf;
	data.l = sizeof(buf);
	_ga_random(data);

	gale_create_array(crap,2*sizeof(buf));
	for (i = 0; i < sizeof(buf); ++i) {
		crap[2*i + 0] = 'a' + (buf[i] >> 4);
		crap[2*i + 1] = 'z' - (buf[i] & 0xF);
	}
	temp.p = crap;
	temp.l = 2*sizeof(buf);

	while (path.l > 0 && path.p[path.l - 1] != '/') --path.l;
	return gale_text_concat(3,path,G_("tmp."),temp);
}

struct inode _ga_init_inode(void) {
	struct inode i;
	i.name = null_text;
	i.device = 0;
	i.inode = 0;
	i.file_time = i.inode_time = 0;
	return i;
}

struct inode _ga_read_inode(int fd,struct gale_text name) {
	struct inode i = _ga_init_inode();
	struct stat buf;
	if (!fstat(fd,&buf)) {
		i.name = name;
		i.inode = buf.st_ino;
		i.device = buf.st_dev;
		i.file_time = buf.st_mtime;
		i.inode_time = buf.st_ctime;
	}
	return i;
}

int _ga_inode_changed(struct inode i) {
	struct stat buf;
	if (stat(gale_text_to_local(i.name),&buf)) return 1;
	return (buf.st_dev != i.device || buf.st_ino != i.inode
	   ||  buf.st_ctime != i.inode_time || buf.st_mtime != i.file_time);
}

int _ga_erase_inode(struct inode file) {
	struct stat buf;
	int status = 0;
	char *szname = gale_text_to_local(file.name);

	if (!szname || !*szname) return status;
	if (!lstat(szname,&buf) 
	&&  buf.st_dev == file.device
	&&  buf.st_ino == file.inode) {
		char *sztemp = gale_text_to_local(make_temp(file.name));
		if (!rename(szname,sztemp)) {
			if (!lstat(sztemp,&buf)
			&&  buf.st_dev == file.device
			&&  buf.st_ino == file.inode)
				status = 1;
			else
				link(sztemp,szname);
			unlink(sztemp);
		}
	}

	return status;
}

int _ga_read_file(struct gale_text fn) {
	struct stat buf,lbuf;
	char *sz = gale_text_to_local(fn);
	int fd = open(sz,O_RDWR);
	if (fd < 0 && (errno == EACCES || errno == EPERM)) 
		fd = open(sz,O_RDONLY);
	if (fd < 0) goto error;

	if (fstat(fd,&buf) || lstat(sz,&lbuf)) {
		gale_alert(GALE_WARNING,sz,errno);
		goto error;
	}
	if (buf.st_dev != lbuf.st_dev || buf.st_ino != lbuf.st_ino) {
		char *sz = gale_malloc(fn.l + 64);
		sprintf(sz,"\"%s\": symbolic link ignored",gale_text_to_local(fn));
		gale_alert(GALE_WARNING,sz,0);
		gale_free(sz);
		goto error;
	}
	if (!S_ISREG(buf.st_mode)) {
		char *sz = gale_malloc(fn.l + 64);
		sprintf(sz,"\"%s\": special file ignored",gale_text_to_local(fn));
		gale_alert(GALE_WARNING,sz,0);
		gale_free(sz);
		goto error;
	}

	gale_free(sz);
	return fd;

error:
	gale_free(sz);
	if (fd >= 0) close(fd);
	return -1;
}

int _ga_load(int fd,struct gale_data *data) {
	int status = 0;
	ssize_t r;
	size_t p;
	struct gale_data dat;

	data->p = NULL;
	data->l = 0;
	dat = *data;

	p = 0;
	do {
		dat.p = gale_realloc(dat.p,dat.l = dat.l ? dat.l * 2 : 2048);

		do
			r = read(fd,p + dat.p,dat.l - p);
		while (r < 0 && errno == EINTR);
		if (r < 0) {
			gale_alert(GALE_WARNING,"cannot read file",errno);
			goto error;
		}

		p += r;
		if (p > MAX_SIZE) {
			gale_alert(GALE_WARNING,"file exceeds maximum size",0);
			goto error;
		}
	} while (r > 0);

	if (p > 0) {
		dat.l = p;
		*data = dat;
		dat.p = NULL;
		status = 1;
	}

error:
	if (dat.p != NULL) gale_free(dat.p);
	return status;
}

int _ga_write_file(struct gale_text fn) {
	char *sz = gale_text_to_local(fn);
	int fd = open(sz,O_WRONLY | O_CREAT,0666);
	if (fd < 0) {
		gale_alert(GALE_WARNING,sz,errno);
		gale_free(sz);
		return -1;
	}
	fchmod(fd,0644);

	gale_free(sz);
	return fd;
}

int _ga_save(int fd,struct gale_data data) {
	ssize_t r = -1;

	while (data.l != 0) {
		do
			r = write(fd,data.p,data.l);
		while (r < 0 && errno == EINTR);
		if (r <= 0) {
			gale_alert(GALE_WARNING,"cannot write file",errno);
			return 0;
		}
		data.p += r;
		data.l -= r;
	}

	return 1;
}

int _ga_save_file(struct gale_text dir,
                  struct gale_text fn,
                  int mode,
                  struct gale_data data,
                  struct inode *inode) 
{
	struct gale_text name;
	struct gale_text temp;
	int fd,status;

	name = dir_file(dir,fn);
	temp = make_temp(name);
	fd = open(gale_text_to_local(temp),O_RDWR | O_CREAT | O_EXCL,0600);
	status = (fd >= 0 && _ga_save(fd,data));

	if (fd < 0) 
		gale_alert(GALE_WARNING,gale_text_to_local(temp),errno);
	else {
		fchmod(fd,mode);
		if (rename(gale_text_to_local(temp),gale_text_to_local(name))) {
			gale_alert(GALE_WARNING,gale_text_to_local(name),errno);
			unlink(gale_text_to_local(temp));
			status = 0;
		} else if (inode)
			*inode = _ga_read_inode(fd,fn);
		close(fd);
	}

	return status;
}
