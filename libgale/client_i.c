#include "client_i.h"
#include "gale/client.h"
#include "gale/misc.h"

static struct gale_map **location_map = NULL;

struct gale_location *client_i_get(struct gale_text name) {
	struct gale_location *ret;
	struct gale_data ndata = gale_text_as_data(name);
	int at,last;

	if (NULL == location_map) {
		location_map = gale_malloc_safe(sizeof(*location_map));
		*location_map = gale_make_map(1);
	}

	ret = (struct gale_location *) gale_map_find(*location_map,ndata);
	if (NULL != ret) return ret;

	gale_create(ret);
	ret->part_count = 1;
	for (at = 0; at < name.l; ++at)
		if ('.' == name.p[at] || '@' == name.p[at])
			ret->part_count += 2;

	gale_create_array(ret->parts,ret->part_count);
	ret->at_part = -1;
	ret->part_count = 0;
	last = 0;
	for (at = 0; at < name.l; ++at) {
		if ('.' != name.p[at] && '@' != name.p[at]) continue;

		ret->parts[ret->part_count].p = name.p + last;
		ret->parts[ret->part_count].l = at - last;
		ret->part_count++;

		if ('@' == name.p[at])
			ret->at_part = ret->part_count;

		last = at + 1;
		ret->parts[ret->part_count].p = name.p + at;
		ret->parts[ret->part_count].l = last - at;
		ret->part_count++;
	}

	ret->parts[ret->part_count].p = name.p + last;
	ret->parts[ret->part_count].l = at - last;
	ret->part_count++;

	ret->key = NULL;
	ret->members = NULL;
	ret->members_null = 0;

	gale_map_add(*location_map,ndata,ret);
	return ret;
}
