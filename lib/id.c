#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "gale/all.h"

struct auth_id *lookup_id(struct gale_text spec) {
	struct gale_text tok = null_text;
	struct auth_id *id;

	if (gale_text_token(spec,'@',&tok) && gale_text_token(spec,'\0',&tok))
		init_auth_id(&id,spec);
	else {
		struct gale_text domain = 
			gale_text_from_local(getenv("GALE_DOMAIN"),-1);
		struct gale_text text = new_gale_text(spec.l + 1 + domain.l);
		gale_text_append(&text,spec);
		gale_text_append(&text,G_("@"));
		gale_text_append(&text,domain);
		init_auth_id(&id,text);
		free_gale_text(text);
		free_gale_text(domain);
	}

	return id;
}

struct gale_text id_category(struct auth_id *id,
                             struct gale_text pfx,struct gale_text sfx)
{
	struct gale_text user,name = auth_id_name(id);
	struct gale_text text = new_gale_text(name.l + pfx.l + sfx.l + 3);
	struct gale_text tok = null_text;

	gale_text_append(&text,G_("@"));
	gale_text_token(name,'@',&tok);
	user = tok;
	if (gale_text_token(name,'@',&tok)) {
		gale_text_append(&text,tok);
		gale_text_append(&text,G_("/"));
		gale_text_append(&text,pfx);
		gale_text_append(&text,G_("/"));
		gale_text_append(&text,user);
		gale_text_append(&text,G_("/"));
		gale_text_append(&text,sfx);
	} else {
		gale_text_append(&text,user);
		gale_text_append(&text,G_("/"));
		gale_text_append(&text,pfx);
		gale_text_append(&text,G_("/"));
	}

	return text;
}

struct gale_text dom_category(struct gale_text dom,struct gale_text pfx) {
	struct gale_text text,domain;

	if (dom.p) 
		domain = dom;
	else
		domain = gale_text_from_local(getenv("GALE_DOMAIN"),-1);

	text = new_gale_text(dom.l + pfx.l + 3);
	gale_text_append(&text,G_("@"));
	gale_text_append(&text,dom);
	gale_text_append(&text,G_("/"));
	gale_text_append(&text,pfx);
	gale_text_append(&text,G_("/"));

	if (!dom.p) free_gale_text(domain);
	return text;
}
