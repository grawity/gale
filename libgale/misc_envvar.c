#include "gale/core.h"
#include "gale/misc.h"
#include "gale/globals.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

struct gale_environ { char **ptr; };

struct gale_environ *global;

static void set_environ(char **ptr) {
	if (NULL == global) global = gale_malloc_safe(sizeof(*global));
	environ = global->ptr = ptr;
}

static struct gale_encoding *enc(void) {
	if (NULL == gale_global) return NULL;
	return gale_global->enc_environ;
}

struct gale_text gale_var(struct gale_text name) {
	return gale_text_from(enc(),getenv(gale_text_to(enc(),name)),-1);
}

void gale_set(struct gale_text name,struct gale_text value) {
	char *text = gale_text_to(enc(),
		gale_text_concat(3,name,G_("="),value));
	char **envp;
	size_t len = name.l + 1;

	for (envp = environ; *envp && strncmp(*envp,text,len); ++envp) ;
	if (*envp) {
		*envp = text;
		set_environ(environ);
	} else {
		size_t num = envp - environ;
		char **new_env;
		gale_create_array(new_env,num + 2);
		memcpy(new_env,environ,num * sizeof(*new_env));
		new_env[num] = text;
		new_env[num + 1] = NULL;
		set_environ(new_env);
	}
}

struct gale_environ *gale_save_environ(void) {
	struct gale_environ *env;
	size_t num;
	for (num = 0; environ[num]; ++num) ;
	gale_create(env);
	gale_create_array(env->ptr,num + 1);
	memcpy(env->ptr,environ,sizeof(*environ) * (num + 1));
	return env;
}

void gale_restore_environ(struct gale_environ *env) {
	set_environ(env->ptr);
}
