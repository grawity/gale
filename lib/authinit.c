#include "init.h"
#include "common.h"

#include <sys/types.h>
#include <sys/stat.h>

struct gale_text 
	_ga_dot_private,_ga_dot_trusted,_ga_dot_local,_ga_dot_auth,
	_ga_etc_private,_ga_etc_trusted,_ga_etc_local,_ga_etc_cache;

void _ga_init(void) {
	static int init = 0;
	if (init) return;
	init = 1;

	_ga_dot_auth = sub_dir(dot_gale,G_("auth"),0700);
	_ga_dot_trusted = sub_dir(_ga_dot_auth,G_("trusted"),0777);
	_ga_dot_private = sub_dir(_ga_dot_auth,G_("private"),0700);
	_ga_dot_local = sub_dir(_ga_dot_auth,G_("local"),0777);

	_ga_etc_trusted = sub_dir(sys_dir,G_("auth"),0777);
	_ga_etc_private = sub_dir(_ga_etc_trusted,G_("private"),0777);
	_ga_etc_cache = sub_dir(_ga_etc_trusted,G_("cache"),0777);
	_ga_etc_local = sub_dir(_ga_etc_trusted,G_("local"),0777);
	_ga_etc_trusted = sub_dir(_ga_etc_trusted,G_("trusted"),0777);

	/* chmod(gale_text_to_local(dir_file(_ga_etc_cache,G_("."))),01777); */
	chmod(gale_text_to_local(dir_file(_ga_etc_local,G_("."))),01777);
}
