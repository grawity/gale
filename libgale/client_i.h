#ifndef CLIENT_I_H
#define CLIENT_I_H

#include "gale/misc.h"
#include "gale/key.h"

struct gale_location {
	struct gale_text *parts;
	int at_part,part_count;
	struct gale_key *key;
	struct gale_map *members;
	int members_null;
};

struct gale_location *client_i_get(struct gale_text name);

#endif
