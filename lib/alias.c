#include "gale/misc.h"
#include "gale/auth.h"
#include "gale/client.h"
#include "gale/globals.h"

#include <assert.h>
#include <unistd.h>

struct lookup {
	oop_source *oop;
	struct gale_map *visited;
	gale_call_location *func;
	void *user;
};

static void *on_exact(struct gale_text name,struct gale_location *loc,void *x) {
	struct lookup *lookup = (struct lookup *) x;
	struct gale_fragment frag;

	if (NULL != gale_map_find(lookup->visited,gale_text_as_data(name))) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("location loop in \""),name,G_("\"")),0);
		return lookup->func(name,loc,lookup->user);
	}

	if (NULL == loc
	||  !gale_group_lookup(
		gale_location_public_data(loc),
		G_("key.redirect"),frag_text,&frag))
		return lookup->func(name,loc,lookup->user);

	gale_map_add(lookup->visited,gale_text_as_data(name),loc);
	gale_find_exact_location(lookup->oop,frag.value.text,on_exact,lookup);
	return OOP_CONTINUE;
}

static void look_in(
	struct gale_text name,struct gale_text dir,
	struct gale_text *out)
{
	char buf[1024]; /* BUG: arbitrary limit! */
	int num;

	if (0 != out->l) return;
	num = readlink(gale_text_to(
		gale_global->enc_filesys,dir_file(dir,name)),
		buf,sizeof(buf));
	if (num > 0) *out = gale_text_from(gale_global->enc_filesys,buf,num);
}

static struct gale_text look(struct gale_text name,struct gale_map *mark) {
	struct gale_text ret = null_text;

	if (NULL != gale_map_find(mark,gale_text_as_data(name))) 
		return null_text;

	gale_map_add(mark,gale_text_as_data(name),mark);
	look_in(name,dir_file(gale_global->dot_gale,G_("aliases")),&ret);
	look_in(name,dir_file(gale_global->sys_dir,G_("aliases")),&ret);
	return ret;
}

void gale_find_location(oop_source *oop,
	struct gale_text name,
	gale_call_location *func,void *user)
{
	struct gale_map *visited = gale_make_map(0);
	struct gale_text local = null_text,domain;
	struct lookup *lookup;

	if (0 == name.l) {
		gale_find_exact_location(oop,name,func,user);
		return;
	}

	/* TODO: validate syntax */
	gale_text_token(name,'@',&local);
	domain = local;
	gale_text_token(name,'\0',&domain);

	while (0 == domain.l) {
		struct gale_text first = null_text,rest,alias;
		gale_text_token(local,'.',&first);
		rest = first;
		gale_text_token(local,'\0',&rest);

		alias = look(first,visited);
		if (0 == alias.l)
			/* TODO: validate */
			domain = gale_var(G_("GALE_DOMAIN")); 
		else {
			/* TODO: validate */
			struct gale_text a_local = null_text,a_domain;
			gale_text_token(alias,'@',&a_local);
			a_domain = a_local;
			gale_text_token(alias,'\0',&a_domain);

			domain = a_domain;
			local = (0 == rest.l) ? a_local
			      : gale_text_concat(3,a_local,G_("."),rest);
		}
	}

	for (;;) {
		struct gale_text last,rest,alias;
		const wch *p = domain.p + domain.l;
		while (domain.p != p && *--p != '.') ;
		rest = gale_text_left(domain,p - domain.p);
		last = (0 == rest.l) ? domain
		     : gale_text_right(domain,-rest.l - 1);

		alias = look(gale_text_concat(2,G_("@"),last),visited);
		if (0 == alias.l) break;
		/* TODO: validate */
		if ('@' == alias.p[0]) alias = gale_text_right(alias,-1);
		domain = (0 == rest.l) ? alias
		       : gale_text_concat(3,rest,G_("."),alias);
	}

	gale_create(lookup);
	lookup->oop = oop;
	lookup->func = func;
	lookup->user = user;
	lookup->visited = gale_make_map(0);
	gale_find_exact_location(oop,
		gale_text_concat(3,local,G_("@"),domain),on_exact,lookup);
}
