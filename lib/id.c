#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>

#include "gale/id.h"
#include "gale/util.h"

void free_id(struct gale_id *id) {
	gale_free(id->user);
	gale_free(id->domain);
	gale_free(id->comment);
	gale_free(id);
}

struct gale_id *lookup_id(const char *spec) {
	struct gale_id *id = gale_malloc(sizeof(*id));
	char *at = strchr(spec,'@');
	if (at) {
		id->user = gale_strndup(spec,at - spec);
		id->domain = gale_strdup(at + 1);
		id->name = gale_strdup(spec);
	} else {
		id->user = gale_strdup(spec);
		id->domain = gale_strdup(getenv("GALE_DOMAIN"));
		id->name = gale_malloc(strlen(id->user)+strlen(id->domain)+2);
		sprintf(id->name,"%s@%s",id->user,id->domain);
	}

	id->comment = NULL;

	if (!strcmp(id->domain,getenv("GALE_DOMAIN"))) {
		struct passwd *pwd = getpwnam(id->user);
		if (pwd) {
			strtok(pwd->pw_gecos,",");
			id->comment = gale_strdup(pwd->pw_gecos);
		}
	}
	return id;
}

char *id_category(struct gale_id *id,const char *prefix,const char *suffix) {
	char *tmp = gale_malloc(strlen(id->user) + strlen(id->domain) + 
	                        strlen(prefix) + strlen(suffix) + 4);
	sprintf(tmp,"%s/%s/%s/%s",prefix,id->domain,id->user,suffix);
	return tmp;
}
