#include "client_i.h"
#include "key_i.h"

#include "gale/key.h"
#include "gale/client.h"
#include "gale/misc.h"

#include <assert.h>

struct find {
	struct gale_location *loc;
	gale_call_location *func;
	void *user;
	struct gale_map *map;
	struct gale_time now;
	int count,flags;
};

static gale_key_call on_key;
static void *on_graph(oop_source *,struct gale_map *,int,int,void *);

static void find_key(oop_source *oop,struct find *find) {
	int i;
	++(find->count);
	for (i = find->loc->at_part - 1; i >= 0; i -= 2) {
		if (0 != find->count) ++(find->count);
		gale_key_search(oop,
			gale_key_handle(gale_text_concat(3,
				gale_text_concat_array(i,find->loc->parts),
				G_("*"),
				gale_text_concat_array(
				    find->loc->part_count - find->loc->at_part,
				    find->loc->parts + find->loc->at_part))),
			find->flags,on_key,find);
	}

	gale_key_search(oop,gale_key_handle(gale_text_concat_array(
		find->loc->part_count,
		find->loc->parts)),find->flags,on_key,find);
}

static void follow_key(oop_source *oop,const struct find *find) {
	struct find *next;
	struct gale_fragment frag;
	struct gale_location *base;
	struct gale_text name = gale_key_name(find->loc->key);
	struct gale_data name_data = gale_text_as_data(name);

	assert(NULL != find->func && 0 == find->count);

	if (!gale_group_lookup(
		gale_key_data(gale_key_public(find->loc->key,find->now)),
		G_("key.redirect"),frag_text,&frag)
	|| (NULL != find->map && NULL != gale_map_find(find->map,name_data)))
	{
		key_i_graph(oop,
			find->loc->key,
			find->flags,
			G_("key.member"),
			on_graph,(void *) find);
		return;
	}

	gale_create(next);
	*next = *find;

	if (NULL == next->map) next->map = gale_make_map(0);
	gale_map_add(next->map,name_data,find->loc->key);

	base = client_i_get(gale_key_name(find->loc->key));
	next->loc = client_i_get(frag.value.text);
	if (base != find->loc) {
		assert(base->at_part <= find->loc->at_part
		    && base->at_part > 0 && !gale_text_compare(
			G_("*"),
			base->parts[base->at_part - 1]));

		next->loc = client_i_get(gale_text_concat(4,
			gale_text_concat_array(
				next->loc->at_part,
				next->loc->parts),
			G_("."),
			gale_text_concat_array(
				find->loc->at_part - base->at_part + 1,
				find->loc->parts + base->at_part - 1),
			gale_text_concat_array(
				next->loc->part_count - next->loc->at_part,
				next->loc->parts + next->loc->at_part)));
	}

	find_key(oop,next);
}

static void *on_graph(oop_source *oop,
	struct gale_map *map,
	int is_complete,int has_null,void *x) 
{
	struct find *find = (struct find *) x;
	if (!is_complete && (find->flags | search_slow) != find->flags) {
		find->flags |= search_slow;
		follow_key(oop,find);
		return OOP_CONTINUE;
	}

	find->loc->members = map;
	find->loc->members_null = has_null;
	gale_map_add(find->loc->members,
		gale_text_as_data(gale_key_name(find->loc->key)),
		find->loc->key);
	return find->func(gale_location_name(find->loc),find->loc,find->user);
}

static void *on_key(oop_source *oop,struct gale_key *key,void *user) {
	struct find *find = (struct find *) user;
	assert(NULL != key);

	if (NULL != gale_key_public(key,find->now)) {
		if (NULL == find->loc->key) 
			find->loc->key = key;
		else 
		if (gale_key_name(key).l < gale_key_name(find->loc->key).l
		||  NULL == gale_key_public(find->loc->key,find->now)) {
			gale_alert(GALE_WARNING,gale_text_concat(5,
				G_("now using \""),gale_key_name(key),
				G_("\" instead of \""),
				gale_key_name(find->loc->key),G_("\"")),0);
			find->loc->key = key;
		}

		if (find->loc->key == key && 0 != find->count) {
			find->count = 0;
			follow_key(oop,find);
		}
	}

	if (0 == find->count || 0 != --(find->count))
		return OOP_CONTINUE;

	if ((find->flags | search_slow) != find->flags) {
		find->flags |= search_slow;
		find_key(oop,find);
		return OOP_CONTINUE;
	}

	return find->func(gale_location_name(find->loc),NULL,find->user);
}

/** Look up a Gale location address without alias expansion.
 *  This function is like gale_find_location(), but accepts only canonical
 *  location names, and skips all alias expansion steps.
 *  \param oop Liboop event source to use.
 *  \param name Name of the location to look up (e.g. "pub.food@ofb.net").
 *  \param func Function to call when location lookup completes.
 *  \param user User-defined parameter to pass the function.
 *  \sa gale_find_location() */
void gale_find_exact_location(oop_source *oop,
        struct gale_text name,
        gale_call_location *func,void *user)
{
	struct find *find;
	gale_create(find);
	find->loc = client_i_get(name);
	find->func = func;
	find->user = user;
	find->map = NULL;
	find->flags = search_all & ~search_slow; /* ~search_private? */
	find->now = gale_time_now();
	find->count = 0;

	if (NULL == gale_key_public(find->loc->key,find->now)) 
		find->loc->key = NULL;
	find_key(oop,find);
}

/** Find a location's name.
 *  This is approximately the opposite of gale_find_exact_location().
 *  \param loc Location to examine.
 *  \return The name of the location (e.g. "pub.food@ofb.net"). */
struct gale_text gale_location_name(struct gale_location *loc) {
	return gale_text_concat_array(loc->part_count,loc->parts);
}

/** Find the key of a location.
 *  This function will return the key responsible for controlling a
 *  location's behavior.  (For example, the key for "pub.food.bitter@ofb.net"
 *  might be "pub.*@ofb.net".)
 *  \param loc Location to find key for.
 *  \return Key. */
struct gale_key *gale_location_key(struct gale_location *loc) {
	return loc->key;
}

/** Determine if we can receive messages sent to a location.
 *  Effectively, this is true if we hold the private key for a location
 *  (or the location is public).
 *  \param loc Location to examine.
 *  \return Nonzero if we can subscribe to this location. */
int gale_location_receive_ok(struct gale_location *loc) {
	struct gale_data key = null_data;
	void *datum;

	if (loc->members_null
	||  NULL != gale_key_private(loc->key)) return 1;

	while (gale_map_walk(loc->members,&key,&key,&datum)) {
		struct gale_location *member = (struct gale_location *) datum;
		if (NULL != gale_key_private(member->key)) return 1;
	}

	return 0;
}

/** Determine if we can send messages to a location.
 *  \param loc Location to examine.
 *  \return Nonzero if we can send messages to this location. */
int gale_location_send_ok(struct gale_location *loc) {
	return 1; /* TODO */
}
