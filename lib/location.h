#ifndef LOCATION_H
#define LOCATION_H

struct gale_location_callback {
	gale_call_location *func;
	void *user;
	struct gale_location_callback *next;
};

struct gale_location {
	int scheduled;
	int successful;
	struct gale_text name;
	struct gale_location *root;
	struct gale_location_callback *list;

	struct auth_id *key;
	struct gale_text routing;
};

#endif
