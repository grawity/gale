#ifndef FILE_H
#define FILE_H

#include "common.h"

#include <sys/stat.h>
#include <unistd.h>

struct inode {
	dev_t device;
	ino_t inode;
	struct gale_text name;
	time_t inode_time,file_time;
};

struct inode _ga_init_inode(void);
struct inode _ga_read_inode(int fd,struct gale_text name);
int _ga_inode_changed(struct inode);
int _ga_erase_inode(struct inode);

int _ga_read_file(struct gale_text fn);
int _ga_write_file(struct gale_text fn);

int _ga_load(int fd,struct gale_data *data);
int _ga_save(int fd,struct gale_data data);

int _ga_save_file(struct gale_text dir,struct gale_text fn,int mode,
                  struct gale_data,struct inode *);

#endif
