#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "gale/all.h"

struct auth_id *lookup_id(const char *spec) {
	char *at = strchr(spec,'@');
	struct auth_id *id;

	if (at) init_auth_id(&id,spec);
	else {
		const char *domain = getenv("GALE_DOMAIN");
		char *tmp = gale_malloc(strlen(domain) + strlen(spec) + 2);
		sprintf(tmp,"%s@%s",spec,domain);
		init_auth_id(&id,tmp);
		gale_free(tmp);
	}

	return id;
}

struct gale_text id_category(struct gale_id *id,const char *pfx,const char *sfx)
{
	const char *name = auth_id_name(id);
	char *tmp = gale_malloc(strlen(name) + strlen(pfx) + strlen(sfx) + 4);
	const char *at = strchr(name,'@');
	struct gale_text text;

	if (at)
		sprintf(tmp,"@%s/%s/%.*s/%s",at+1,pfx,at - name,name,sfx);
	else
		sprintf(tmp,"@%s/%s/",name,pfx);
	text = gale_text_from_local(tmp,-1);
	gale_free(tmp);
	return text;
}

struct gale_text dom_category(const char *dom,const char *pfx) {
	char *tmp;
	struct gale_text text;

	if (!dom) dom = getenv("GALE_DOMAIN");
	tmp = gale_malloc(strlen(pfx) + strlen(dom) + 4);
	sprintf(tmp,"@%s/%s/",dom,pfx);

	text = gale_text_from_local(tmp,-1);
	gale_free(tmp);
	return text;
}
