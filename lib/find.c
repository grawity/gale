#include "common.h"
#include "init.h"
#include "file.h"
#include "key.h"
#include "id.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_SIZE 65536
#define RETRY_TIME 1200

auth_hook *hook_find_public = NULL;
auth_hook *hook_find_private = NULL;

static int open_pub(struct auth_id *id,int fd,int flag,struct inode *inode) {
	int status = 0;
	struct gale_data key = null_data;
	struct auth_id *imp = NULL;

	if (fd < 0) return 0;
	status = _ga_load(fd,&key);

	if (status && key.l > 0) {
		_ga_import_pub(&imp,key,inode,flag);

		if (!imp)
			status = 0;
		else if (imp != id) {
			_ga_warn_id(G_("file named \"%\" contains key \"%\""),
			            id,imp);
			status = 0;
		} else if (!_ga_trust_pub(imp))
			status = 0;
	}

	close(fd);
	if (inode && !status) _ga_erase_inode(*inode);
	return status;
}

static int open_priv(struct auth_id *id,int fd) {
	int status = 0;
	struct gale_data key = null_data;
	struct auth_id *imp = NULL;

	if (fd < 0) return 0;
	status = _ga_load(fd,&key);

	if (status && key.l > 0) {
		_ga_import_priv(&imp,key);

		if (!imp)
			status = 0;
		else if (imp != id) {
			_ga_warn_id(G_("file named \"%\" contains key \"%\""),
			            id,imp);
			status = 0;
		}
	}

	close(fd);
	return status;
}

static int get(struct gale_text dir,struct gale_text fn,struct inode *inode) {
	struct gale_text path;
	int r;
	path = dir_file(dir,fn);
	r = _ga_read_file(path);
	gale_dprintf(11,"(auth) trying to read \"%s\"\n",
	             gale_text_to_local(path));
	if (inode && r >= 0) *inode = _ga_read_inode(r,fn);
	return r;
}

static void nop(char * const * argv) { (void) argv; }

int _ga_find_pub(struct auth_id *id) {
	char *argv[] = { "gkfind", NULL, NULL };
	pid_t pid;
	int fd,status;

	if (hook_find_public && hook_find_public(id)) return 1;

	argv[1] = gale_text_to_local(id->name);
	pid = gale_exec("gkfind",argv,NULL,&fd,nop);
	status = open_pub(id,fd,IMPORT_NORMAL,NULL);
	gale_wait(pid);
	return status;
}

int auth_id_public(struct auth_id *id) {
	int status;
	struct gale_time now;
	struct inode in;

	gale_diprintf(10,2,"(auth) \"%s\": looking for public key\n",
	             gale_text_to_local(id->name));

	if (_ga_trust_pub(id)) {
		gale_diprintf(10,-2,"(auth) \"%s\": we already had it\n",
		             gale_text_to_local(id->name));
		return 1;
	}

	now = gale_time_now();
	if (gale_time_compare(
		gale_time_diff(now,gale_time_seconds(RETRY_TIME)),
		id->find_time) < 0)
	{
		gale_diprintf(10,-2,"(auth) \"%s\": we searched recently\n",
		             gale_text_to_local(id->name));
		return 0;
	}

	id->find_time = now;
	in = _ga_init_inode();

	if (open_pub(id,get(_ga_dot_trusted,id->name,&in),IMPORT_TRUSTED,&in)
	||  open_pub(id,get(_ga_dot_local,id->name,&in),IMPORT_NORMAL,&in)
	||  open_pub(id,get(_ga_etc_trusted,id->name,&in),IMPORT_TRUSTED,&in)
	||  open_pub(id,get(_ga_etc_local,id->name,&in),IMPORT_NORMAL,&in)
	||  open_pub(id,get(_ga_etc_cache,id->name,&in),IMPORT_NORMAL,&in)) {
		gale_diprintf(10,-2,"(auth) \"%s\": done looking, found it\n",
		             gale_text_to_local(id->name));
		return 1;
	}

	gale_dprintf(11,"(auth) \"%s\": searching more desperately\n",
	             gale_text_to_local(id->name));
	status = _ga_find_pub(id);
	gale_diprintf(10,-2,"(auth) \"%s\": done looking (%s)\n",
	             gale_text_to_local(id->name),status ? "found" : "not found");
	return status;
}

int auth_id_private(struct auth_id *id) {
	int fd,status;
	pid_t pid;
	char *argv[] = { "gkfetch", NULL, NULL };

	if (id->private
	||  open_priv(id,get(_ga_dot_private,id->name,NULL))
	||  open_priv(id,get(_ga_etc_private,id->name,NULL)))
		return 1;

	if (hook_find_private && hook_find_private(id)) return 1;

	argv[1] = gale_text_to_local(id->name);
	pid = gale_exec("gkfetch",argv,NULL,&fd,nop);
	status = open_priv(id,fd);
	gale_wait(pid);
	return status;
}
