#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "gale/all.h"
#include "key.h"
#include "id.h"

static const int seen = 0;
static struct gale_text dirs[2];

static struct gale_text alias(struct gale_text spec,struct gale_map *seen) {
	struct gale_text next,next_alias;
	int i;

	if (NULL != gale_map_find(seen,gale_text_as_data(spec))) return spec;
	gale_map_add(seen,gale_text_as_data(spec),&seen);
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
		const char *fn = gale_text_to(gale_global->enc_filesys,
			dir_file(dirs[i],spec));
		char buf[1024]; /* arbitrary limit! */
		int num = readlink(fn,buf,sizeof(buf));
		if (num > 0) 
			return alias(gale_text_from(
				gale_global->enc_filesys,buf,num),seen);
	}

	next = _ga_signer(spec);
	if (!gale_text_compare(next,G_("ROOT"))) return spec;

	next_alias = alias(next,seen);
	if (!gale_text_compare(next_alias,next)) return spec;
	if (!gale_text_compare(next_alias,G_("ROOT"))) next_alias = null_text;

	return alias(
		gale_text_concat(2,gale_text_left(spec,-next.l),next_alias),
		seen);
}

static void redirect(struct auth_id **id,struct gale_map *seen) {
	struct gale_fragment f;
	struct gale_data name = gale_text_as_data(auth_id_name(*id));
	if (NULL != gale_map_find(seen,name)) {
		_ga_warn_id(G_("\"%\": redirection loop"),*id);
		return;
	}
	gale_map_add(seen,name,&seen);

	if (auth_id_public(*id)
	&& gale_group_lookup((*id)->pub_data,G_("key.redirect"),frag_text,&f)) {
		init_auth_id(id,f.value.text);
		redirect(id,seen);
	}
}

struct auth_id *lookup_id(struct gale_text spec) {
	struct gale_text dot = null_text,at = null_text;
	struct auth_id *id;

	dirs[0] = dir_file(gale_global->dot_gale,G_("auth/aliases"));
	dirs[1] = dir_file(gale_global->sys_dir,G_("auth/aliases"));
	spec = alias(spec,gale_make_map(0));

	/* If we already have an '@' or a '.', leave as-is. */
	if ((gale_text_token(spec,'.',&dot) && gale_text_token(spec,'\0',&dot))
	||  (gale_text_token(spec,'@',&at)  && gale_text_token(spec,'\0',&at)))
		init_auth_id(&id,spec);
	else
		init_auth_id(&id,gale_text_concat(3,
			spec,
			G_("@"),
			gale_var(G_("GALE_DOMAIN"))));

	redirect(&id,gale_make_map(0));
	return id;
}

static struct gale_text decolon(struct gale_text text) {
	struct gale_text tok = null_text;
	struct gale_text ret;
	gale_text_token(text,':',&tok);
	ret = tok;
	while (gale_text_token(text,':',&tok))
		ret = gale_text_concat(3,ret,G_("_"),tok);
	return ret;
}

struct gale_text id_category(struct auth_id *id,
                             struct gale_text pfx,struct gale_text sfx)
{
	struct gale_text user,name = auth_id_name(id);
	struct gale_text tok = null_text;

	gale_text_token(name,'@',&tok);
	user = tok;
	if (gale_text_token(name,'@',&tok))
		return gale_text_concat(8,
			G_("@"),decolon(tok),G_("/"), 
			pfx,G_("/"),decolon(user),G_("/"),sfx);
	else
		return gale_text_concat(5,
			G_("@"),decolon(user),G_("/"),pfx,G_("/"));
}

struct gale_text dom_category(struct gale_text dom,struct gale_text pfx) {
	struct gale_text domain;

	if (dom.l > 0) 
		domain = dom;
	else
		domain = gale_var(G_("GALE_DOMAIN"));

	return gale_text_concat(5,G_("@"),domain,G_("/"),pfx,G_("/"));
}
