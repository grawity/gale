#include "key_i.h"
#include "gale/misc.h"

struct graph {
	struct gale_text name;
	int flags;
	void *(*func)(oop_source *,struct gale_map *,int,int,void *);
	void *user;
	struct gale_time now;
	struct gale_map *map;
	int is_complete;
	int has_null;
	int out;
};

static gale_key_call found;

static void *found(oop_source *oop,struct gale_key *key,void *user) {
	struct graph * const g = (struct graph *) user;
	const struct gale_key_assertion * const ass = gale_key_public(key,g->now);

	if (NULL == ass)
		g->is_complete = 0;
	else {
		struct gale_group data = gale_key_data(ass);
		data = gale_group_find(data,g->name);
		while (!gale_group_null(data)) {
			struct gale_fragment frag = gale_group_first(data);
			if (frag_text == frag.type) {
				struct gale_data name;
				name = gale_text_as_data(frag.value.text);
				if (0 == name.l)
					g->has_null = 1;
				else if (NULL == gale_map_find(g->map,name)) {
					struct gale_key *key; 
					++(g->out);
					key = gale_key_handle(frag.value.text);
					gale_map_add(g->map,name,key);
					gale_key_search(oop,key,g->flags,found,g);
				}
			}
			data = gale_group_find(gale_group_rest(data),g->name);
		}
	}

	if (0 != --(g->out)) return OOP_CONTINUE;
	return g->func(oop,g->map,g->is_complete,g->has_null,g->user);
}

void key_i_graph(oop_source *oop,
	struct gale_key *root,int flags,struct gale_text name,
	void *(*func)(oop_source *,struct gale_map *,int,int,void *),void *user)
{
	struct graph *graph;
	gale_create(graph);
	graph->name = name;
	graph->flags = flags;
	graph->func = func;
	graph->user = user;
	graph->now = gale_time_now();
	graph->map = gale_make_map(0);
	graph->is_complete = 1;
	graph->has_null = 0;
	graph->out = 1;
	gale_key_search(oop,root,flags,found,graph);
}
