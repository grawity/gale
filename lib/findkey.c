#include "common.h"
#include "file.h"
#include "key.h"
#include "id.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define EXEC_RETRY 1200
#define CALL_RETRY 1200
#define CACHE_RETRY 5

static int elapsed(struct gale_time *last,int min) {
	struct gale_time now = gale_time_now();
	if (0 < gale_time_compare(*last,
		gale_time_diff(now,gale_time_seconds(min)))) return 0;

	*last = now;
	return 1;
}

static int unchanged(struct gale_text path,struct inode *inode) {
	if (0 != gale_text_compare(path,inode->name)) return 0;
	if (!_ga_inode_changed(*inode)) {
		gale_dprintf(12,"(auth) \"%s\": no change\n",
			     gale_text_to(gale_global->enc_console,path));
		return 1;
	}
	*inode = _ga_init_inode();
	return 0;
}

static int open_pub_fd(struct auth_id *id,int fd,struct gale_text n,int flag) {
	struct inode i = _ga_init_inode();
	struct gale_data key = null_data;
	int status;

	if (fd < 0) return 0;
	if (0 != n.l) i = _ga_read_inode(fd,n);
	status = _ga_load(fd,&key);
	close(fd);

	if (status) {
		struct auth_id *imp = NULL;
		_ga_import_pub(&imp,key,&i,flag);

		status = (imp == id && _ga_trust_pub(imp));
		if (NULL != imp && imp != id)
			_ga_warn_id(G_("file \"%\" contains \"%\""),id,imp);
	}

#if !SAFE_KEY
	if (!status) _ga_erase_inode(i);
#endif
	return status;
}

static int open_pub_file(
	struct auth_id *id,
	struct gale_text dir,struct gale_text fn,int flag) 
{
	struct gale_text path = dir_file(dir,fn);
	if (unchanged(path,&id->pub_inode)) return _ga_trust_pub(id);
	if (0 == id->pub_inode.name.l) id->pub_trusted = 0;
	return open_pub_fd(id,_ga_read_file(path),path,flag);
}

static void nop(char * const * argv) { (void) argv; }

int _ga_find_pub(struct auth_id *id) {
	char *argv[] = { "gkfind", NULL, NULL };
	pid_t pid;
	int fd,status;

	if (NULL != gale_global->find_public
	&&  elapsed(&id->pub_time_callback,CALL_RETRY)
	&&  gale_global->find_public(id)) return 1;

	if (!elapsed(&id->pub_time_gkfind,EXEC_RETRY)) return 0;
	argv[1] = gale_text_to(gale_global->enc_cmdline,id->name);
	pid = gale_exec("gkfind",argv,NULL,&fd,nop);
	status = open_pub_fd(id,fd,null_text,IMPORT_NORMAL);
	gale_wait(pid);
	return status;
}

int auth_id_public(struct auth_id *id) {
	int status;
	struct gale_global_data * const G = gale_global;

	gale_diprintf(10,2,"(auth) \"%s\": looking for public key\n",
	             gale_text_to(gale_global->enc_console,id->name));

	if (elapsed(&id->pub_time_cache,CACHE_RETRY)) {
		if (open_pub_file(id,G->dot_trusted,id->name,IMPORT_TRUSTED)
		||  open_pub_file(id,G->dot_local,id->name,IMPORT_NORMAL)
		||  open_pub_file(id,G->sys_trusted,id->name,IMPORT_TRUSTED)
		||  open_pub_file(id,G->sys_local,id->name,IMPORT_NORMAL)
		||  open_pub_file(id,G->sys_cache,id->name,IMPORT_NORMAL))
			return 1;
	} else 
		if (_ga_trust_pub(id)) return 1;

	status = _ga_find_pub(id);
	gale_diprintf(10,-2,"(auth) \"%s\": done looking (%s)\n",
	             gale_text_to(gale_global->enc_console,id->name),
	             status ? "found" : "not found");
	return status;
}

static int open_priv_fd(struct auth_id *id,int fd,struct gale_text path) {
	struct inode i = _ga_init_inode();
	struct gale_data key = null_data;
	int status;

	if (fd < 0) return 0;
	if (0 != path.l) i = _ga_read_inode(fd,path);
	status = _ga_load(fd,&key);
	close(fd);

	if (status) {
		struct auth_id *imp = NULL;
		_ga_import_priv(&imp,key,&i);

		status = (imp == id);
		if (NULL != imp && imp != id)
			_ga_warn_id(G_("file \"%\" contains \"%\""),id,imp);
	}

	return status;
}

static int open_priv_file(
	struct auth_id *id,
	struct gale_text dir,struct gale_text fn) 
{
	struct gale_text path = dir_file(dir,fn);
	if (unchanged(path,&id->priv_inode)) 
		return !gale_group_null(id->priv_data);
	return open_priv_fd(id,_ga_read_file(path),path);
}

static int get_private(struct auth_id *id) {
	int fd,status;
	pid_t pid;
	char *argv[] = { "gkfetch", NULL, NULL };

	if (elapsed(&id->priv_time_cache,CACHE_RETRY)) {
		if (open_priv_file(id,gale_global->dot_private,id->name)
		||  open_priv_file(id,gale_global->sys_private,id->name))
			return 1;
	} else
		if (!gale_group_null(id->priv_data)) return 1;

	if (NULL != gale_global->find_private
	&&  elapsed(&id->priv_time_callback,CALL_RETRY)
	&&  gale_global->find_private(id))
		return 1;

	if (!elapsed(&id->priv_time_gkfetch,EXEC_RETRY)) 
		return !gale_group_null(id->priv_data);

	argv[1] = gale_text_to(gale_global->enc_cmdline,id->name);
	pid = gale_exec("gkfetch",argv,NULL,&fd,nop);
	status = open_priv_fd(id,fd,null_text);
	gale_wait(pid);
	return status;
}

int auth_id_private(struct auth_id *id) {
	if (!get_private(id)) return 0;

	if (!auth_id_public(id)) {
		gale_alert(GALE_WARNING,
		G_("ignoring private key with no public counterpart"),0);
		return 0;
	}

	return 1;
}
