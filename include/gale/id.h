/* id.h -- gale id management stuff. */

#ifndef GALE_ID
#define GALE_ID

#include "gale/gauth.h"

extern struct auth_id *user_id; /* Initialized to our own ID */

/* Look up an ID by the local naming conventions. */
struct auth_id *lookup_id(const char *);

/* Locate the public key, locally or remotely.  (Potentially) very slow! */
int find_id(struct auth_id *);

/* Return prefix / domain / user / suffix in a newly gale_malloc()'d string. */
char *id_category(struct auth_id *,const char *prefix,const char *suffix);

/* For compatibility.  Deprecated. */
#define gale_id auth_id
#define free_id(x) free_auth_id(x)

#endif
