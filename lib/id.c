#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gale/id.h"

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

char *id_category(struct gale_id *id,const char *pfx,const char *sfx) {
	const char *name = auth_id_name(id);
	char *tmp = gale_malloc(strlen(name) + strlen(pfx) + strlen(sfx) + 3);
	const char *at = strchr(name,'@');

	if (at)
		sprintf(tmp,"%s/%s/%.*s/%s",pfx,at+1,at - name,name,sfx);
	else
		sprintf(tmp,"%s/%s/%s",pfx,name,sfx);

	return tmp;
}
