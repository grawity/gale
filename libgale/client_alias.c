#include "gale/client.h"
#include "gale/misc.h"
#include "gale/globals.h"
#include "client_i.h"

#include <assert.h>

static struct gale_location *look_in(struct gale_text name,struct gale_text dir)
{
        char buf[1024]; /* BUG: arbitrary limit! */
        const int num = readlink(gale_text_to(
                gale_global->enc_filesys,dir_file(dir,name)),
                buf,sizeof(buf));
        if (num <= 0) return NULL;
	return client_i_get(gale_text_from(gale_global->enc_filesys,buf,num));
}

static struct gale_location *look(struct gale_text name,struct gale_map *mark) {
	struct gale_location *ret = NULL;
	if (NULL != gale_map_find(mark,gale_text_as_data(name))) return ret;

	gale_map_add(mark,gale_text_as_data(name),mark);

        ret = look_in(name,dir_file(gale_global->dot_gale,G_("aliases")));
	if (NULL == ret)
		ret = look_in(name,
			dir_file(gale_global->sys_dir,G_("aliases")));
	return ret;
}

/** Look up a Gale location address.
 *  Start looking up a Gale location address in the background and return
 *  immediately.  When the lookup is complete (whether it succeeded or
 *  failed), the supplied callback is invoked.
 *  \param oop Liboop event source to use.
 *  \param name Name of the location to look up (e.g. "pub.food").
 *  \param func Function to call when location lookup completes.
 *  \param user User-defined parameter to pass the function.
 *  \sa gale_location_name(), gale_find_exact_location() */
void gale_find_location(oop_source *oop,
        struct gale_text name,
        gale_call_location *func,void *user)
{
	struct gale_map * const mark = gale_make_map(0);
	struct gale_location *loc = client_i_get(name);

	if (0 == loc->part_count) {
		gale_find_exact_location(oop,name,func,user);
		return;
	}

	while (loc->at_part < 0) {
		struct gale_location * const alias = look(loc->parts[0],mark);
		if (NULL == alias) {
			loc = client_i_get(gale_text_concat(3,
				gale_location_name(loc),
				G_("@"),gale_var(G_("GALE_DOMAIN"))));
			assert(loc->at_part >= 0);
		} else {
			int alias_at = (alias->at_part < 0) 
			             ? alias->part_count 
			             : alias->at_part;
			loc = client_i_get(gale_text_concat(3,
				gale_text_concat_array(alias_at,alias->parts),
				gale_text_concat_array(
					loc->part_count - 1,
					loc->parts + 1),
				gale_text_concat_array(
					alias->part_count - alias_at,
					alias->parts + alias_at)));
		}
	}

	for (;;) {
		struct gale_location * const alias = look(gale_text_concat(2,
			G_("@"),loc->parts[loc->part_count - 1]),mark);
		if (NULL == alias) break;
		loc = client_i_get(gale_text_concat(2,
			gale_text_concat_array(loc->part_count - 1,loc->parts),
			gale_text_concat_array(
				alias->part_count - alias->at_part - 1,
				alias->parts + alias->at_part + 1)));
	}

	gale_find_exact_location(oop,gale_location_name(loc),func,user);
}
