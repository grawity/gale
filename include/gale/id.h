/* id.h -- gale id management */

#ifndef GALE_ID
#define GALE_ID

/* Gale ID.  All pointers are NUL-terminated, owned strings. */
struct gale_id {
	char *name;     /* Name of id, e.g. "egnor@ofb.net" */
	char *user;     /* username, e.g. "egnor" */
	char *domain;   /* domain, e.g. "ofb.net" */
	char *comment;  /* comment, e.g. "Dan Egnor" */
};

extern struct gale_id *user_id; /* Initialized to our own ID */

/* Free an ID structure and all strings therein. */
void free_id(struct gale_id *);

/* Break apart and look up an ID by name. */
struct gale_id *lookup_id(const char *);

/* Return prefix / domain / user / suffix in a newly gale_malloc()'d string. */
char *id_category(struct gale_id *,const char *prefix,const char *suffix);

#endif
