#ifndef GALE_ID
#define GALE_ID

struct gale_id {
	char *name;
	char *user;
	char *domain;
	char *comment;
};

extern struct gale_id *user_id;

void free_id(struct gale_id *);
struct gale_id *lookup_id(const char *);
char *id_category(struct gale_id *,const char *prefix,const char *suffix);

#endif
